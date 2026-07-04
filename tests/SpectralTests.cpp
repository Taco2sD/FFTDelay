// Unit tests for FFTDelay's DSP/data components, using JUCE's built-in
// UnitTest framework. Built as the FFTDelayTests console app and run via CTest.
//
// These cover the pure, deterministic pieces — the curve store, the MIDI Learn
// assignment map, and the spectral engine's numeric sanity (no NaNs, silence in
// -> silence out). The full plugin/GUI is exercised separately by `auval`.

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

#include "../source/SpectralCurves.h"
#include "../source/MidiLearnManager.h"
#include "../source/SpectralDelay.h"

//==============================================================================
struct SpectralCurvesTests : juce::UnitTest
{
    SpectralCurvesTests() : juce::UnitTest ("SpectralCurves") {}

    void runTest() override
    {
        SpectralCurves c;

        beginTest ("defaults: attenuation unity, delay and feedback zero");
        for (int m = 0; m < SpectralCurves::numMatrices; ++m)
            for (int b = 0; b < SpectralCurves::numEditBands; ++b)
                expectEquals (c.get (m, b), SpectralCurves::defaultFor (m));

        beginTest ("set / get round-trips");
        c.set (SpectralCurves::Delay, 10, 0.25f);
        expectEquals (c.get (SpectralCurves::Delay, 10), 0.25f);

        beginTest ("fill sets a whole matrix");
        c.fill (SpectralCurves::Feedback, 0.5f);
        for (int b = 0; b < SpectralCurves::numEditBands; ++b)
            expectEquals (c.get (SpectralCurves::Feedback, b), 0.5f);

        beginTest ("copyFrom duplicates every point");
        SpectralCurves d;
        d.set (SpectralCurves::Attenuation, 5, 0.3f);
        c.copyFrom (d);
        expectEquals (c.get (SpectralCurves::Attenuation, 5), 0.3f);

        beginTest ("selection: off by default, per-band set/clear, value-independent");
        SpectralCurves sel;
        for (int b = 0; b < SpectralCurves::numEditBands; ++b)
            expect (! sel.isSelected (SpectralCurves::Delay, b));
        sel.setSelected (SpectralCurves::Delay, 42, true);
        expect (sel.isSelected (SpectralCurves::Delay, 42));
        expect (! sel.isSelected (SpectralCurves::Feedback, 42));      // per matrix
        expectEquals (sel.get (SpectralCurves::Delay, 42),
                      SpectralCurves::defaultFor (SpectralCurves::Delay));   // values untouched
        sel.clearSelection (SpectralCurves::Delay);
        expect (! sel.isSelected (SpectralCurves::Delay, 42));

        beginTest ("transformers: quantize, average, flip values, reverse bands");
        SpectralCurves t;
        t.set (SpectralCurves::Delay, 0, 0.30f);
        t.quantize (SpectralCurves::Delay, 0.25f);
        expectWithinAbsoluteError (t.get (SpectralCurves::Delay, 0), 0.25f, 1.0e-6f);

        t.set (SpectralCurves::Delay, 0, 1.0f);      // mean of one 1.0 + 159 zeros
        for (int b = 1; b < SpectralCurves::numEditBands; ++b)
            t.set (SpectralCurves::Delay, b, 0.0f);
        t.average (SpectralCurves::Delay);
        expectWithinAbsoluteError (t.get (SpectralCurves::Delay, 80),
                                   1.0f / (float) SpectralCurves::numEditBands, 1.0e-6f);

        t.set (SpectralCurves::Feedback, 3, 0.2f);
        t.flipValues (SpectralCurves::Feedback);
        expectWithinAbsoluteError (t.get (SpectralCurves::Feedback, 3), 0.8f, 1.0e-6f);

        t.fill (SpectralCurves::Attenuation, 0.0f);
        t.set (SpectralCurves::Attenuation, 0, 1.0f);
        t.reverseBands (SpectralCurves::Attenuation);
        expectWithinAbsoluteError (t.get (SpectralCurves::Attenuation,
                                          SpectralCurves::numEditBands - 1), 1.0f, 1.0e-6f);
        expectWithinAbsoluteError (t.get (SpectralCurves::Attenuation, 0), 0.0f, 1.0e-6f);

        beginTest ("generator: saw-up spans 0..amp across one cycle");
        SpectralCurves gen;
        gen.generate (SpectralCurves::Delay, 4, 0.5f, 1.0f, 1u);
        expectWithinAbsoluteError (gen.get (SpectralCurves::Delay, 0), 0.0f, 1.0e-6f);
        expectWithinAbsoluteError (gen.get (SpectralCurves::Delay,
                                            SpectralCurves::numEditBands - 1), 0.5f, 1.0e-2f);

        beginTest ("smooth applies a 3-tap kernel to a spike");
        SpectralCurves s;
        s.fill (SpectralCurves::Delay, 0.0f);
        s.set (SpectralCurves::Delay, 80, 1.0f);
        s.smooth (SpectralCurves::Delay);
        expectWithinAbsoluteError (s.get (SpectralCurves::Delay, 80), 0.5f, 1.0e-6f);
        expectWithinAbsoluteError (s.get (SpectralCurves::Delay, 79), 0.25f, 1.0e-6f);
        expectWithinAbsoluteError (s.get (SpectralCurves::Delay, 81), 0.25f, 1.0e-6f);
    }
};

//==============================================================================
struct EditMaskTests : juce::UnitTest
{
    EditMaskTests() : juce::UnitTest ("EditMask mapping") {}

    void runTest() override
    {
        constexpr int last = SpectralCurves::numEditBands - 1;

        for (int mask = 0; mask <= 3; ++mask)
            for (int bins : { 64, 512, 1024 })
            {
                beginTest ("mask " + juce::String (mask) + " / " + juce::String (bins)
                           + " bins: monotonic and in range");
                int prev = 0;
                for (int b = 0; b < bins; ++b)
                {
                    const int band = SpectralCurves::binToEditBand (b, bins, mask);
                    expect (band >= prev && band >= 0 && band <= last);
                    prev = band;
                }
            }

        beginTest ("linear spans the full grid");
        expectEquals (SpectralCurves::binToEditBand (0,    1024, SpectralCurves::Linear), 0);
        expectEquals (SpectralCurves::binToEditBand (1023, 1024, SpectralCurves::Linear), last);

        beginTest ("adapted 2x: lowest 30 bins get one 2-column group each");
        for (int b = 0; b < 30; ++b)
            expectEquals (SpectralCurves::binToEditBand (b, 1024, SpectralCurves::Adapted2x), b * 2);
        expect (SpectralCurves::binToEditBand (1023, 1024, SpectralCurves::Adapted2x) <= 158);

        beginTest ("adapted 4x: lowest 15 bins get one 4-column group each");
        for (int b = 0; b < 15; ++b)
            expectEquals (SpectralCurves::binToEditBand (b, 1024, SpectralCurves::Adapted4x), b * 4);
        expect (SpectralCurves::binToEditBand (1023, 1024, SpectralCurves::Adapted4x) <= 156);

        beginTest ("logarithmic gives low bins more grid than linear");
        expect (SpectralCurves::binToEditBand (32, 1024, SpectralCurves::Logarithmic)
                > SpectralCurves::binToEditBand (32, 1024, SpectralCurves::Linear));
    }
};

//==============================================================================
struct MidiLearnTests : juce::UnitTest
{
    MidiLearnTests() : juce::UnitTest ("MidiLearnManager") {}

    void runTest() override
    {
        MidiLearnManager ml;

        beginTest ("clears to no assignments");
        expectEquals (ml.getArmed(), (int) MidiLearnManager::noTarget);
        expectEquals (ml.ccForTarget (5), -1);

        beginTest ("bind maps CC -> target both ways");
        ml.bind (64, 5);
        expectEquals (ml.target[64].load(), 5);
        expectEquals (ml.ccForTarget (5), 64);

        beginTest ("clearTarget removes the assignment");
        ml.clearTarget (5);
        expectEquals (ml.ccForTarget (5), -1);
        expectEquals (ml.target[64].load(), (int) MidiLearnManager::noTarget);

        beginTest ("invert toggles per target");
        ml.bind (10, 7);
        expect (! ml.getInvert (7));
        ml.setInvert (7, true);
        expect (ml.getInvert (7));

        beginTest ("arm / disarm");
        ml.arm (3);
        expectEquals (ml.getArmed(), 3);
        ml.disarm();
        expectEquals (ml.getArmed(), (int) MidiLearnManager::noTarget);

        beginTest ("clearAll wipes everything");
        ml.bind (1, 1);
        ml.bind (2, 2);
        ml.clearAll();
        expectEquals (ml.ccForTarget (1), -1);
        expectEquals (ml.ccForTarget (2), -1);
    }
};

//==============================================================================
struct SpectralDelayTests : juce::UnitTest
{
    SpectralDelayTests() : juce::UnitTest ("SpectralDelay") {}

    void runTest() override
    {
        beginTest ("reported latency is the max frame size");
        expectEquals (SpectralDelay::getLatencySamples(), SpectralDelay::maxFftSize);

        constexpr double sr      = 44100.0;
        constexpr int    latency = SpectralDelay::maxFftSize;

        auto engine = std::make_unique<SpectralDelay>();
        auto& sd = *engine;
        sd.prepare (sr);

        std::array<float, SpectralDelay::maxBins> gain {}, fb {}, delay {};
        auto applyCurves = [&] (float g, float f, float d)
        {
            gain.fill (g); fb.fill (f); delay.fill (d);
            sd.setBandGain (gain.data());
            sd.setBandFeedback (fb.data());
            sd.setBandDelaySeconds (delay.data());
        };

        // Feed an impulse and return the index + value of the output peak.
        auto impulsePeak = [&] (int numSamples)
        {
            sd.reset();
            int peakIdx = 0; float peak = 0.0f;
            for (int n = 0; n < numSamples; ++n)
            {
                const float out = std::abs (sd.processSample (n == 0 ? 1.0f : 0.0f));
                if (out > peak) { peak = out; peakIdx = n; }
            }
            return std::pair<int, float> (peakIdx, peak);
        };

        beginTest ("silence in -> silence out");
        applyCurves (1.0f, 0.3f, 0.05f);
        bool allZero = true;
        for (int n = 0; n < 8192; ++n)
            if (sd.processSample (0.0f) != 0.0f)
                allZero = false;
        expect (allZero);

        beginTest ("zero-delay taps pass input straight through at constant latency");
        applyCurves (1.0f, 0.0f, 0.0f);
        auto [p0, v0] = impulsePeak (3 * latency);
        expectEquals (p0, latency);
        expect (v0 > 0.25f);

        beginTest ("constant latency holds at the smallest FFT size (alignment pad)");
        sd.setFftOrder (SpectralDelay::minFftOrder);
        sd.applyPendingSettings();
        applyCurves (1.0f, 0.0f, 0.0f);
        auto [p1, v1] = impulsePeak (3 * latency);
        expectEquals (p1, latency);
        expect (v1 > 0.25f);

        beginTest ("feedback is inert at zero delay (no recirculation)");
        sd.setFftOrder (SpectralDelay::maxFftOrder);
        sd.applyPendingSettings();
        applyCurves (1.0f, 1.0f, 0.0f);
        sd.reset();
        float tailEnergy = 0.0f;
        for (int n = 0; n < 6 * latency; ++n)
        {
            const float out = sd.processSample (n == 0 ? 1.0f : 0.0f);
            if (n > 2 * latency)                       // well past the direct impulse
                tailEnergy += std::abs (out);
        }
        expect (tailEnergy < 1.0e-3f);

        beginTest ("a full-cut attenuation curve silences the wet path");
        applyCurves (0.0f, 0.5f, 0.1f);
        sd.reset();
        float maxOut = 0.0f;
        for (int n = 0; n < 22050; ++n)
            maxOut = juce::jmax (maxOut, std::abs (sd.processSample (n == 0 ? 1.0f : 0.5f)));
        expect (maxOut < 1.0e-9f);

        beginTest ("a 12 s painted delay lands where painted (full ring capacity)");
        sd.setFftOrder (SpectralDelay::minFftOrder);   // hop = frame at 0x: fast + worst-ish case
        sd.setOverlap (0);
        sd.setWindowType (3);   // Rectangle: the only window that reconstructs at 0x
        sd.applyPendingSettings();
        applyCurves (1.0f, 0.0f, SpectralDelay::maxDelaySeconds);
        const int hop      = (1 << SpectralDelay::minFftOrder);
        const int expected = latency
                           + (int) std::lround (SpectralDelay::maxDelaySeconds * sr / hop) * hop;
        auto [p2, v2] = impulsePeak (expected + 4 * hop);
        expect (std::abs (p2 - expected) <= hop);
        expect (v2 > 0.1f);

        beginTest ("impulse response stays finite and bounded under feedback");
        sd.setFftOrder (SpectralDelay::maxFftOrder);
        sd.setOverlap (2);
        sd.setWindowType (0);
        sd.applyPendingSettings();
        applyCurves (1.0f, 0.9f, 0.05f);
        sd.reset();
        bool finite = true; float maxAbs = 0.0f;
        for (int n = 0; n < 44100; ++n)
        {
            const float out = sd.processSample (n == 0 ? 1.0f : 0.0f);
            if (! std::isfinite (out)) finite = false;
            maxAbs = juce::jmax (maxAbs, std::abs (out));
        }
        expect (finite);
        expect (maxAbs < 100.0f);
    }
};

//==============================================================================
static SpectralCurvesTests spectralCurvesTests;
static EditMaskTests       editMaskTests;
static MidiLearnTests      midiLearnTests;
static SpectralDelayTests  spectralDelayTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;

    if (failures > 0)
        std::cerr << failures << " test failure(s)\n";
    else
        std::cout << "All tests passed.\n";

    return failures > 0 ? 1 : 0;
}
