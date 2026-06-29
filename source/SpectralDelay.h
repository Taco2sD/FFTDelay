#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include <vector>

// Single-channel spectral (STFT) delay engine.
//
// It chops the incoming signal into overlapping windowed frames, transforms each
// to the frequency domain (FFT), pushes the spectrum into a ring buffer of past
// frames (the "delay line"), mixes in a delayed-and-fed-back spectrum, then
// transforms back and overlap-adds to reconstruct the output.
//
// Delay / feedback / attenuation are PER BAND: each analysis bin reads from its
// own tap offset, has its own feedback amount, and its own input gain. Those
// three arrays are the Attenuation / Delay / Feedback "matrices" (see
// docs/architecture.md). The engine outputs the pure WET signal; dry/wet mixing
// and output gain happen in the processor against a latency-compensated dry path.
class SpectralDelay
{
public:
    void prepare (double sampleRate);
    void reset();

    // Realtime-safe: no allocation, no locks. Returns the wet (processed) sample.
    float processSample (float input) noexcept;

    // Apply any pending settings changes (FFT size / overlap / window). Call once
    // per block on the audio thread, before the per-sample loop. Realtime-safe
    // (it may clear preallocated buffers, but never allocates).
    void applyPendingSettings() noexcept;

    // --- Analysis settings (deferred; take effect on the next applyPendingSettings) ---
    void setFftOrder   (int order) noexcept;   // clamped to [minFftOrder, maxFftOrder]
    void setOverlap    (int ov)    noexcept;   // 0 = none, 1 = 2x, 2 = 3x
    void setWindowType (int type)  noexcept;   // 0 Hann, 1 Triangle, 2 Hamming, 3 Rectangle

    void setFreeze (bool shouldFreeze) noexcept { frozen = shouldFreeze; }

    // Input Modulation: a frequency-domain treatment applied to the analysed
    // spectrum before the matrices. algo 0 = none; 1..11 select a frequency-domain treatment.
    // c1/c2/c3 are the (up to three) generic controls, each 0..1.
    void setInputMod (int algo, float c1, float c2, float c3) noexcept
    {
        imAlgo = algo; imC1 = c1; imC2 = c2; imC3 = c3;
    }

    // --- Per-band matrices. Each setter copies exactly maxBins values (realtime
    //     safe). The processor builds these from its edit curves every block.
    void setBandGain         (const float* linearGain)  noexcept;  // attenuation (linear)
    void setBandDelaySeconds (const float* seconds)      noexcept;  // delay matrix
    void setBandFeedback     (const float* amount)       noexcept;  // 0..1 per band

    // Active bin count, for mapping the processor's curves onto analysis bins.
    int getActiveNumBins() const noexcept { return activeFftSize / 2 + 1; }

    static constexpr int minFftOrder = 6;             // 2^6  = 64
    static constexpr int maxFftOrder = 10;            // 2^10 = 1024
    static constexpr int maxFftSize  = 1 << maxFftOrder;
    static constexpr int maxFrameSize = 2 * maxFftSize;   // interleaved complex (re,im)
    static constexpr int maxBins     = maxFftSize / 2 + 1;

    // Worst-case algorithmic latency the host should compensate for: one full
    // max-size FFT frame. Kept constant so the reported latency never changes
    // when the user switches FFT size.
    static constexpr int getLatencySamples() noexcept { return maxFftSize; }

    // Latest analysed magnitude spectrum (per bin, ~0..1) for the UI display.
    // Written on the audio thread once per frame; copied by the processor.
    const std::array<float, maxBins>& getMagnitudes() const noexcept { return magnitudes; }

private:
    void processFrame() noexcept;
    void recomputeFraming() noexcept;   // window, hop, normalisation, active FFT
    void applyInputModulation (int bins) noexcept;   // frequency-domain treatments on fftData

    // --- Input Modulation parameters + per-frame state ---
    int   imAlgo = 0;
    float imC1 = 0.5f, imC2 = 0.5f, imC3 = 0.5f;
    double   imFoamPhase  = 0.0;
    double   imPulsePhase = 0.0;
    uint32_t imRng = 0x9e3779b9u;
    std::array<float, maxBins>      imHoleTimer {};   // Deterioration hold timers
    std::array<float, maxFrameSize> imPrev {};        // TimeSponge previous spectrum
    std::array<float, maxFrameSize> imScratch {};     // shift/scramble workspace

    std::array<float, maxBins>      magnitudes {};    // analysed spectrum for the UI

    // Pre-built FFTs, one per supported order, so switching size never allocates.
    std::array<std::unique_ptr<juce::dsp::FFT>, maxFftOrder + 1> ffts;
    juce::dsp::FFT* activeFft = nullptr;

    double sr = 44100.0;

    // Active framing (derived from the settings below in recomputeFraming()).
    int   activeOrder   = maxFftOrder;
    int   activeFftSize = maxFftSize;
    int   overlap       = 2;            // 0/1/2 -> hop = N/(overlap+1)
    int   windowType    = 0;            // Hann
    int   hopSize       = maxFftSize / 3;
    float windowCorrection = 1.0f;

    // Pending settings + dirty flag (written by setters, consumed on apply).
    int  pendingOrder    = maxFftOrder;
    int  pendingOverlap  = 2;
    int  pendingWindow   = 0;
    bool settingsDirty   = true;

    bool frozen = false;

    // Per-band matrices, indexed by analysis bin (0 .. maxBins-1).
    std::array<float, maxBins> bandGain     {};   // attenuation, linear
    std::array<float, maxBins> bandFeedback {};   // 0..1
    std::array<float, maxBins> bandDelayFr  {};   // delay, in frames (float)

    std::array<float, maxFftSize> window {};

    // Sliding circular input/output buffers (length = active FFT size, sized to max).
    std::array<float, maxFftSize> inputFifo  {};
    std::array<float, maxFftSize> outputFifo {};
    int fifoPos    = 0;
    int hopCounter = 0;

    std::array<float, maxFrameSize> fftData {};   // scratch for one transform

    // Ring buffer of past spectra = the spectral delay line.
    static constexpr int numFramesMax = 2048;     // bounds max delay & memory
    std::vector<float> spectralRing;               // numFramesMax * maxFrameSize
    int writeFrame = 0;
};
