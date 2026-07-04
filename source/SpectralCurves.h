#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// The three painted per-band "matrices" (Attenuation / Delay / Feedback), stored as per-edit-band
// curves. Each curve is a row of `numEditBands` control points the user paints;
// the processor fans them onto the (up to 1024) analysis bins via an edit mask.
//
// Lock-free by design: the editor writes individual points on the message thread
// and the audio thread reads them every block. Per-element relaxed atomics make
// that safe without ever blocking the audio thread.
struct SpectralCurves
{
    static constexpr int numEditBands = 160;   // columns of the edit grid

    enum Matrix { Attenuation = 0, Delay = 1, Feedback = 2, numMatrices = 3 };

    enum Mask { Linear = 0, Adapted2x = 1, Adapted4x = 2, Logarithmic = 3 };

    // Maps an analysis bin onto the edit grid under the given mask. Masks trade
    // editable resolution for musical focus: Linear spreads every bin evenly
    // across all 160 columns; the Adapted masks coarsen the grid into groups of
    // 2 or 4 columns and give the lowest bins a group EACH (1:1), packing the
    // remaining bins into the groups left over — fine control where music is
    // dense, broad strokes up top; Logarithmic warps the whole axis.
    static int binToEditBand (int bin, int activeBins, int mask) noexcept
    {
        const int lastBin = std::max (1, activeBins - 1);
        bin = std::clamp (bin, 0, lastBin);

        switch (mask)
        {
            case Adapted2x:
            case Adapted4x:
            {
                const int span   = mask == Adapted2x ? 2 : 4;   // columns per group
                const int groups = numEditBands / span;         // 80 / 40
                const int fine   = std::min (groups * 3 / 8, activeBins);   // 30 / 15

                int group = bin;
                if (bin >= fine)
                {
                    const int coarseBins = std::max (1, activeBins - fine);
                    group = fine + (bin - fine) * (groups - fine) / coarseBins;
                }
                return std::clamp (group, 0, groups - 1) * span;
            }
            case Logarithmic:
                return (int) std::lround ((numEditBands - 1)
                                          * std::log (1.0f + (float) bin)
                                          / std::log (1.0f + (float) lastBin));
            default:
                return (int) std::lround ((numEditBands - 1) * (float) bin / (float) lastBin);
        }
    }

    // The curves ARE the parameters (there are no global delay/feedback/gain
    // knobs behind them). A fresh instance is a clean passthrough: Attenuation
    // flat at unity, Delay and Feedback flat at zero.
    SpectralCurves() { reset(); }

    static constexpr float defaultFor (int matrix) noexcept
    {
        return matrix == Attenuation ? 1.0f : 0.0f;
    }

    void reset() noexcept
    {
        for (int m = 0; m < numMatrices; ++m)
            fill (m, defaultFor (m));
    }

    float get (int matrix, int band) const noexcept
    {
        return values[(size_t) matrix][(size_t) band].load (std::memory_order_relaxed);
    }

    void set (int matrix, int band, float v) noexcept
    {
        values[(size_t) matrix][(size_t) band].store (v, std::memory_order_relaxed);
    }

    void fill (int matrix, float v) noexcept
    {
        for (auto& point : values[(size_t) matrix])
            point.store (v, std::memory_order_relaxed);
    }

    // Copy points from another curve set (atomics aren't copyable directly):
    // one matrix, or all three. Used by the per-column stereo copy controls.
    void copyMatrixFrom (const SpectralCurves& other, int matrix) noexcept
    {
        for (int b = 0; b < numEditBands; ++b)
            values[(size_t) matrix][(size_t) b].store (other.get (matrix, b), std::memory_order_relaxed);
    }

    void copyFrom (const SpectralCurves& other) noexcept
    {
        for (int m = 0; m < numMatrices; ++m)
            copyMatrixFrom (other, m);
    }

    // Per-band selection flags, one set per matrix: transient edit state that
    // the "Sel" modulation destinations act on (offsets apply only to selected
    // bands). Same lock-free contract as the values; not serialised.
    bool isSelected (int matrix, int band) const noexcept
    {
        return selection[(size_t) matrix][(size_t) band].load (std::memory_order_relaxed);
    }

    void setSelected (int matrix, int band, bool on) noexcept
    {
        selection[(size_t) matrix][(size_t) band].store (on, std::memory_order_relaxed);
    }

    void clearSelection (int matrix) noexcept
    {
        for (auto& band : selection[(size_t) matrix])
            band.store (false, std::memory_order_relaxed);
    }

    // --- Destructive curve operations (the EDIT card's transformers) ----------

    // One pass of a 3-tap smoothing kernel; repeated clicks smear further.
    void smooth (int matrix) noexcept
    {
        auto& m = values[(size_t) matrix];
        std::array<float, numEditBands> tmp {};
        for (int b = 0; b < numEditBands; ++b)
            tmp[(size_t) b] = m[(size_t) b].load (std::memory_order_relaxed);

        for (int b = 0; b < numEditBands; ++b)
        {
            const float l = tmp[(size_t) (b > 0 ? b - 1 : 0)];
            const float r = tmp[(size_t) (b < numEditBands - 1 ? b + 1 : numEditBands - 1)];
            m[(size_t) b].store (0.25f * l + 0.5f * tmp[(size_t) b] + 0.25f * r,
                                 std::memory_order_relaxed);
        }
    }

    // Snap every value to the nearest multiple of `step` (e.g. the tempo grid).
    void quantize (int matrix, float step) noexcept
    {
        if (step <= 0.0f)
            return;
        for (int b = 0; b < numEditBands; ++b)
            set (matrix, b, std::min (1.0f, std::round (get (matrix, b) / step) * step));
    }

    // Flatten the curve to its mean value.
    void average (int matrix) noexcept
    {
        float sum = 0.0f;
        for (int b = 0; b < numEditBands; ++b)
            sum += get (matrix, b);
        fill (matrix, sum / (float) numEditBands);
    }

    // Mirror the value axis (v -> 1 - v).
    void flipValues (int matrix) noexcept
    {
        for (int b = 0; b < numEditBands; ++b)
            set (matrix, b, 1.0f - get (matrix, b));
    }

    // Mirror the frequency axis (low <-> high bands).
    void reverseBands (int matrix) noexcept
    {
        for (int b = 0; b < numEditBands / 2; ++b)
        {
            const int   o  = numEditBands - 1 - b;
            const float lo = get (matrix, b), hi = get (matrix, o);
            set (matrix, b, hi);
            set (matrix, o, lo);
        }
    }

    // Write a waveform across the frequency axis. Shapes: 0 sine, 1 triangle,
    // 2 square, 3 saw down, 4 saw up, 5 random (seeded, one value per band).
    void generate (int matrix, int shape, float amp, float cycles, unsigned seed) noexcept
    {
        unsigned rng = seed;
        for (int b = 0; b < numEditBands; ++b)
        {
            const float ph = cycles * (float) b / (float) (numEditBands - 1);
            float t = ph - std::floor (ph);
            if (t == 0.0f && ph > 0.0f)
                t = 1.0f;   // sample exact cycle boundaries as the ramp's endpoint
            float w = 0.0f;
            switch (shape)
            {
                case 0:  w = 0.5f - 0.5f * std::cos (6.2831853f * t); break;
                case 1:  w = 1.0f - std::abs (2.0f * t - 1.0f);       break;
                case 2:  w = t < 0.5f ? 1.0f : 0.0f;                  break;
                case 3:  w = 1.0f - t;                                break;
                case 4:  w = t;                                       break;
                default: rng = rng * 1664525u + 1013904223u;
                         w = (float) (rng >> 8) / (float) (1u << 24); break;
            }
            set (matrix, b, std::min (1.0f, amp * w));
        }
    }

private:
    std::array<std::array<std::atomic<float>, numEditBands>, numMatrices> values;
    std::array<std::array<std::atomic<bool>,  numEditBands>, numMatrices> selection {};
};
