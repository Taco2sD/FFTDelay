#pragma once

#include <array>
#include <atomic>

// The three painted per-band "matrices" (Attenuation / Delay / Feedback), stored as per-edit-band
// curves. Each curve is a row of `numEditBands` control points the user paints;
// the processor fans them onto the (up to 1024) analysis bins via an edit mask.
//
// Lock-free by design: the editor writes individual points on the message thread
// and the audio thread reads them every block. Per-element relaxed atomics make
// that safe without ever blocking the audio thread.
struct SpectralCurves
{
    static constexpr int numEditBands = 160;   // the original's edit resolution

    enum Matrix { Attenuation = 0, Delay = 1, Feedback = 2, numMatrices = 3 };

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

    // Copy every point from another curve set (atomics aren't copyable directly).
    // Used by the stereo Copy-Up / Copy-Down controls.
    void copyFrom (const SpectralCurves& other) noexcept
    {
        for (int m = 0; m < numMatrices; ++m)
            for (int b = 0; b < numEditBands; ++b)
                values[(size_t) m][(size_t) b].store (other.get (m, b), std::memory_order_relaxed);
    }

    // One pass of a 3-tap smoothing kernel — the "Smooth (Smth)" control, which
    // interpolates/smears a matrix a little more with each repeated click.
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

private:
    std::array<std::array<std::atomic<float>, numEditBands>, numMatrices> values;
};
