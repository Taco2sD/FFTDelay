#include "SpectralDelay.h"
#include <cmath>

void SpectralDelay::prepare (double sampleRate)
{
    sr = sampleRate;

    // Build one FFT per supported order up front so switching size at runtime
    // never allocates on the audio thread.
    for (int order = minFftOrder; order <= maxFftOrder; ++order)
        ffts[(size_t) order] = std::make_unique<juce::dsp::FFT> (order);

    // Size the spectral ring pool so EVERY (FFT size, overlap) combination can
    // hold maxDelaySeconds: frames needed scale with 1/hop, frame size with N,
    // so the product is bounded — compute the exact worst case once.
    size_t poolFloats = 0;
    for (int order = minFftOrder; order <= maxFftOrder; ++order)
        for (int ov = 0; ov <= 2; ++ov)
        {
            const int    n      = 1 << order;
            const int    hop    = juce::jmax (1, n / (ov + 1));
            const size_t frames = (size_t) std::ceil (maxDelaySeconds * sr / hop) + 2;
            poolFloats = juce::jmax (poolFloats, frames * (size_t) (2 * n));
        }
    spectralRing.assign (poolFloats, 0.0f);

    bandGain.fill (1.0f);
    bandFeedback.fill (0.0f);
    bandDelayFr.fill (0.0f);

    settingsDirty = true;
    applyPendingSettings();
    reset();
}

void SpectralDelay::reset()
{
    inputFifo.fill (0.0f);
    outputFifo.fill (0.0f);
    if (! spectralRing.empty())
        std::fill (spectralRing.begin(), spectralRing.end(), 0.0f);
    fifoPos    = 0;
    hopCounter = 0;
    writeFrame = 0;

    imHoleTimer.fill (0.0f);
    imPrev.fill (0.0f);
    imScratch.fill (0.0f);
    magnitudes.fill (0.0f);
    outMagnitudes.fill (0.0f);
    imFoamPhase  = 0.0;
    imPulsePhase = 0.0;

    alignFifo.fill (0.0f);
    alignPos = 0;
}

void SpectralDelay::setFftOrder (int order) noexcept
{
    order = juce::jlimit (minFftOrder, maxFftOrder, order);
    if (order != pendingOrder) { pendingOrder = order; settingsDirty = true; }
}

void SpectralDelay::setOverlap (int ov) noexcept
{
    ov = juce::jlimit (0, 2, ov);
    if (ov != pendingOverlap) { pendingOverlap = ov; settingsDirty = true; }
}

void SpectralDelay::setWindowType (int type) noexcept
{
    type = juce::jlimit (0, 3, type);
    if (type != pendingWindow) { pendingWindow = type; settingsDirty = true; }
}

void SpectralDelay::setBandGain (const float* g) noexcept
{
    std::copy (g, g + maxBins, bandGain.begin());
}

void SpectralDelay::setBandFeedback (const float* f) noexcept
{
    std::copy (f, f + maxBins, bandFeedback.begin());
}

void SpectralDelay::setBandDelaySeconds (const float* seconds) noexcept
{
    // Convert seconds -> WHOLE frames at the current hop (spectral delays only
    // exist in frame quanta). 0 is a genuine zero-delay passthrough tap.
    const float framesPerSecond = (float) (sr / (double) hopSize);
    for (int b = 0; b < maxBins; ++b)
        bandDelayFr[(size_t) b] = juce::jlimit (0.0f, (float) (ringFrames - 1),
                                                (float) std::lround (seconds[b] * framesPerSecond));
}

void SpectralDelay::applyPendingSettings() noexcept
{
    if (! settingsDirty)
        return;

    activeOrder = pendingOrder;
    overlap     = pendingOverlap;
    windowType  = pendingWindow;
    settingsDirty = false;

    recomputeFraming();

    // The bin layout and meaning just changed; flush so stale spectra don't bleed
    // through with the wrong interpretation.
    reset();
}

void SpectralDelay::recomputeFraming() noexcept
{
    activeFftSize = 1 << activeOrder;
    activeFft     = ffts[(size_t) activeOrder].get();
    hopSize       = juce::jmax (1, activeFftSize / (overlap + 1));

    // Reinterpret the fixed pool for this frame size (see prepare()).
    frameStride = 2 * activeFftSize;
    ringFrames  = juce::jmax (2, (int) (spectralRing.size() / (size_t) frameStride));

    const int N = activeFftSize;
    for (int i = 0; i < N; ++i)
    {
        const float t = (float) i / (float) N;                 // [0,1)
        float w = 1.0f;
        switch (windowType)
        {
            case 0: w = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi * t)); break; // Hann
            case 1: w = 1.0f - std::abs (2.0f * t - 1.0f);                                break; // Triangle
            case 2: w = 0.54f - 0.46f * std::cos (juce::MathConstants<float>::twoPi * t); break; // Hamming
            case 3: default: w = 1.0f;                                                    break; // Rectangle
        }
        window[(size_t) i] = w;
    }

    // Overlap-add reconstruction gain for analysis*synthesis windowing (window^2
    // summed over the hops that cover one output sample). For COLA-satisfying
    // setups this is constant; otherwise we use the average so the level stays
    // sane even when the user picks a deliberately "wrong" overlap.
    double acc = 0.0;
    for (int n = 0; n < hopSize; ++n)
    {
        double s = 0.0;
        for (int i = n; i < N; i += hopSize)
            s += (double) window[(size_t) i] * window[(size_t) i];
        acc += s;
    }
    const double avg = acc / (double) hopSize;
    windowCorrection = avg > 1.0e-9 ? (float) (1.0 / avg) : 1.0f;
}

float SpectralDelay::processSample (float input) noexcept
{
    const float output = outputFifo[(size_t) fifoPos];
    outputFifo[(size_t) fifoPos] = 0.0f;

    inputFifo[(size_t) fifoPos] = input;
    fifoPos = (fifoPos + 1) % activeFftSize;

    if (++hopCounter >= hopSize)
    {
        hopCounter = 0;
        processFrame();
    }

    // Pad the frame's inherent latency (activeFftSize) up to the constant
    // reported figure so dry/wet alignment holds at every FFT size.
    const int pad = maxFftSize - activeFftSize;
    alignFifo[(size_t) alignPos] = output;
    const float aligned = alignFifo[(size_t) ((alignPos - pad + maxFftSize) % maxFftSize)];
    alignPos = (alignPos + 1) % maxFftSize;
    return aligned;
}

void SpectralDelay::processFrame() noexcept
{
    const int N    = activeFftSize;
    const int bins = N / 2;                       // process bins 0 .. N/2 (inclusive)

    // Window the most recent N input samples into the FFT scratch.
    for (int i = 0; i < N; ++i)
        fftData[(size_t) i] = inputFifo[(size_t) ((fifoPos + i) % N)] * window[(size_t) i];

    std::fill (fftData.begin() + N, fftData.begin() + 2 * N, 0.0f);

    activeFft->performRealOnlyForwardTransform (fftData.data());

    // Input Modulation treats the analysed spectrum before the matrices.
    if (imAlgo != 0)
        applyInputModulation (bins);

    // Capture the magnitude spectrum (post-analysis) for the UI display.
    const float magScale = 2.0f / (float) N;
    for (int b = 0; b <= bins; ++b)
    {
        const float re = fftData[(size_t)(2*b)], im = fftData[(size_t)(2*b+1)];
        magnitudes[(size_t) b] = std::sqrt (re*re + im*im) * magScale;
    }

    const float inputGate = frozen ? 0.0f : 1.0f;
    float* writePtr = &spectralRing[(size_t) writeFrame * (size_t) frameStride];

    for (int b = 0; b <= bins; ++b)
    {
        const int idx = 2 * b;

        // Attenuation matrix gates what ENTERS the delay network (frozen = full cut).
        const float g     = bandGain[(size_t) b] * inputGate;
        const float inRe  = fftData[(size_t) idx]     * g;
        const float inIm  = fftData[(size_t) idx + 1] * g;

        // Delay matrix: each bin reads from its own tap offset. A zero tap is a
        // straight passthrough of the attenuated input — feedback is inert there
        // (nothing recirculates without a delay), so the write is the input alone.
        const int df = (int) bandDelayFr[(size_t) b];
        if (df == 0)
        {
            writePtr[(size_t) idx]     = inRe;
            writePtr[(size_t) idx + 1] = inIm;
            fftData[(size_t) idx]      = inRe;
            fftData[(size_t) idx + 1]  = inIm;
            continue;
        }

        const int readFrame = (writeFrame - df + ringFrames) % ringFrames;
        const float* readPtr = &spectralRing[(size_t) readFrame * (size_t) frameStride];

        const float delRe = readPtr[(size_t) idx];
        const float delIm = readPtr[(size_t) idx + 1];

        // Feedback matrix: 100% when frozen so captured content sustains forever.
        const float fb = frozen ? 1.0f : bandFeedback[(size_t) b];

        writePtr[(size_t) idx]     = inRe + fb * delRe;   // store into delay line
        writePtr[(size_t) idx + 1] = inIm + fb * delIm;

        fftData[(size_t) idx]      = delRe;               // wet = delay-line output
        fftData[(size_t) idx + 1]  = delIm;
    }

    writeFrame = (writeFrame + 1) % ringFrames;

    // Capture the wet-output spectrum (post matrices) for the UI display.
    for (int b = 0; b <= bins; ++b)
    {
        const float re = fftData[(size_t) (2 * b)], im = fftData[(size_t) (2 * b + 1)];
        outMagnitudes[(size_t) b] = std::sqrt (re * re + im * im) * magScale;
    }

    activeFft->performRealOnlyInverseTransform (fftData.data());

    for (int i = 0; i < N; ++i)
        outputFifo[(size_t) ((fifoPos + i) % N)] +=
            fftData[(size_t) i] * window[(size_t) i] * windowCorrection;
}

// The Input Modulation treatments. These are original frequency-domain designs;
// each one is a compact, self-contained spectral effect.
// They operate in place on the
// interleaved (re, im) bins 0..bins.
void SpectralDelay::applyInputModulation (int bins) noexcept
{
    auto rnd = [this]() noexcept                          // uniform 0..1
    {
        imRng = imRng * 1664525u + 1013904223u;
        return (float) (imRng >> 8) / (float) (1u << 24);
    };
    const float twoPi = juce::MathConstants<float>::twoPi;
    const float fps   = (float) (sr / (double) hopSize);  // frames per second

    auto* d = fftData.data();

    switch (imAlgo)
    {
        case 1:   // Deterioration — Depth, Frequency (density), Duration
        {
            const float depth = imC1, density = imC2 * 0.5f, duration = 1.0f + imC3 * 30.0f;
            for (int b = 0; b <= bins; ++b)
            {
                float& t = imHoleTimer[(size_t) b];
                if (t <= 0.0f && rnd() < density) t = duration;
                if (t > 0.0f) { const float g = 1.0f - depth; d[2*b] *= g; d[2*b+1] *= g; t -= 1.0f; }
            }
            break;
        }
        case 2:   // Jello Mold — Threshold, Reverse Gain (invert loud bands)
        {
            const float thr = imC1, rg = imC2;
            for (int b = 0; b <= bins; ++b)
            {
                const float re = d[2*b], im = d[2*b+1];
                if (std::sqrt (re*re + im*im) > thr) { d[2*b] = -re * rg; d[2*b+1] = -im * rg; }
            }
            break;
        }
        case 3:   // PitchRoll — Shift the spectrum by up to +-128 bins
        {
            const int shift = (int) std::lround ((imC1 * 2.0f - 1.0f) * 128.0f);
            std::copy (fftData.begin(), fftData.begin() + 2 * (bins + 1), imScratch.begin());
            for (int b = 0; b <= bins; ++b)
            {
                const int src = b - shift;
                if (src >= 0 && src <= bins) { d[2*b] = imScratch[(size_t)(2*src)]; d[2*b+1] = imScratch[(size_t)(2*src+1)]; }
                else                         { d[2*b] = 0.0f;                       d[2*b+1] = 0.0f; }
            }
            break;
        }
        case 4:   // Smear — Depth (magnitude blur + phase scramble)
        {
            const float depth = imC1;
            std::copy (fftData.begin(), fftData.begin() + 2 * (bins + 1), imScratch.begin());
            auto magAt = [&] (int k) { const float r = imScratch[(size_t)(2*k)], m = imScratch[(size_t)(2*k+1)]; return std::sqrt (r*r + m*m); };
            for (int b = 0; b <= bins; ++b)
            {
                const int bl = b > 0 ? b - 1 : 0, br = b < bins ? b + 1 : bins;
                const float r = imScratch[(size_t)(2*b)], m = imScratch[(size_t)(2*b+1)];
                const float mag = std::sqrt (r*r + m*m);
                const float newMag = mag + depth * ((magAt(bl) + magAt(b) + magAt(br)) / 3.0f - mag);
                const float ph = std::atan2 (m, r) + depth * (rnd() * 2.0f - 1.0f) * twoPi;
                d[2*b] = newMag * std::cos (ph); d[2*b+1] = newMag * std::sin (ph);
            }
            break;
        }
        case 5:   // TimeSponge — Azimuth (blend current spectrum with the last frame)
        {
            const float az = imC1;
            for (int b = 0; b <= bins; ++b)
            {
                const float cr = d[2*b], ci = d[2*b+1];
                d[2*b]   = cr * (1.0f - az) + imPrev[(size_t)(2*b)]   * az;
                d[2*b+1] = ci * (1.0f - az) + imPrev[(size_t)(2*b+1)] * az;
                imPrev[(size_t)(2*b)] = cr; imPrev[(size_t)(2*b+1)] = ci;
            }
            break;
        }
        case 6:   // Foam — Depth, Speed, Function (time-varying envelope)
        {
            const float depth = imC1, speedHz = imC2 * 10.0f;
            const int   fn = juce::jlimit (0, 4, (int) (imC3 * 4.999f));
            imFoamPhase += (double) speedHz / (double) fps; imFoamPhase -= std::floor (imFoamPhase);
            const float p = (float) imFoamPhase;
            float e = 0.0f;
            switch (fn)
            {
                case 0: e = 1.0f - p; break;                          // saw down
                case 1: e = p; break;                                 // saw up
                case 2: e = p < 0.5f ? 1.0f : 0.0f; break;            // square
                case 3: e = 1.0f - std::abs (2.0f * p - 1.0f); break; // triangle
                default: e = 0.5f + 0.5f * std::sin (twoPi * p); break; // sine
            }
            const float g = 1.0f - depth * (1.0f - e);
            for (int b = 0; b <= bins; ++b) { d[2*b] *= g; d[2*b+1] *= g; }
            break;
        }
        case 7:   // Lime Twist — Max Twist (random per-bin magnitude jitter)
        {
            const float amt = imC1;
            for (int b = 0; b <= bins; ++b) { const float j = 1.0f + amt * (rnd() * 2.0f - 1.0f); d[2*b] *= j; d[2*b+1] *= j; }
            break;
        }
        case 8:   // Horse Tail — Depth, Spread (modulo), Offset (comb attenuation)
        {
            const float depth = imC1; const int spread = 2 + (int) (imC2 * 30.0f), offset = (int) (imC3 * (float) spread);
            for (int b = 0; b <= bins; ++b)
                if ((((b - offset) % spread) + spread) % spread == 0) { const float g = 1.0f - depth; d[2*b] *= g; d[2*b+1] *= g; }
            break;
        }
        case 9:   // Mini-Pulses — Gain, Repeat Time, Feedback (pulsed gate)
        {
            const float gain = imC1 * 2.0f, rateHz = 0.5f + imC2 * 20.0f, fb = imC3;
            imPulsePhase += (double) rateHz / (double) fps; imPulsePhase -= std::floor (imPulsePhase);
            const float g = (imPulsePhase < 0.2 ? 1.0f : fb) * gain;
            for (int b = 0; b <= bins; ++b) { d[2*b] *= g; d[2*b+1] *= g; }
            break;
        }
        case 10:  // Imprint — Depth, Threshold, Invert (spectral sieve)
        {
            const float depth = imC1, thr = imC2; const bool inv = imC3 > 0.5f;
            for (int b = 0; b <= bins; ++b)
            {
                const float re = d[2*b], im = d[2*b+1];
                bool below = std::sqrt (re*re + im*im) < thr;
                if (inv) below = ! below;
                if (below) { const float g = 1.0f - depth; d[2*b] *= g; d[2*b+1] *= g; }
            }
            break;
        }
        case 11:  // Phase Blaster — Phase Magic (phase scramble)
        {
            const float amt = imC1;
            for (int b = 0; b <= bins; ++b)
            {
                const float re = d[2*b], im = d[2*b+1];
                const float mag = std::sqrt (re*re + im*im);
                const float ph  = std::atan2 (im, re) + amt * (rnd() * 2.0f - 1.0f) * twoPi;
                d[2*b] = mag * std::cos (ph); d[2*b+1] = mag * std::sin (ph);
            }
            break;
        }
        default: break;
    }
}
