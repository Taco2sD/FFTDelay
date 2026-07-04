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

        SpectralDelay sd;
        sd.prepare (44100.0);

        std::array<float, SpectralDelay::maxBins> gain {}, fb {}, delay {};
        gain.fill (1.0f);
        fb.fill (0.3f);
        delay.fill (0.05f);
        sd.setBandGain (gain.data());
        sd.setBandFeedback (fb.data());
        sd.setBandDelaySeconds (delay.data());
        sd.applyPendingSettings();

        beginTest ("silence in -> silence out");
        bool allZero = true;
        for (int n = 0; n < 8192; ++n)
            if (sd.processSample (0.0f) != 0.0f)
                allZero = false;
        expect (allZero);

        beginTest ("impulse response stays finite and bounded");
        sd.reset();
        float maxAbs = 0.0f;
        bool finite = true;
        for (int n = 0; n < 44100; ++n)        // ~1 s, covers the delay + feedback tail
        {
            const float in  = (n == 0) ? 1.0f : 0.0f;
            const float out = sd.processSample (in);
            if (! std::isfinite (out)) finite = false;
            maxAbs = juce::jmax (maxAbs, std::abs (out));
        }
        expect (finite);
        expect (maxAbs < 100.0f);              // feedback < 1 must not blow up

        beginTest ("switching FFT size stays finite (RT-safe reinit)");
        sd.setFftOrder (SpectralDelay::minFftOrder);
        sd.applyPendingSettings();
        bool stillFinite = true;
        for (int n = 0; n < 8192; ++n)
            if (! std::isfinite (sd.processSample (0.5f)))
                stillFinite = false;
        expect (stillFinite);
    }
};

//==============================================================================
static SpectralCurvesTests spectralCurvesTests;
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
