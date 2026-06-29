#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

FFTDelayAudioProcessor::FFTDelayAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    createFactoryPresets();
}

void FFTDelayAudioProcessor::createFactoryPresets()
{
    if (! presetManager.getPresets().isEmpty())
        return;   // user already has presets (or factory bank exists)

    // Snapshot the default state so we can restore it between presets and at the end.
    juce::MemoryBlock snapshot;
    getStateInformation (snapshot);
    auto restore = [&] { setStateInformation (snapshot.getData(), (int) snapshot.getSize()); };

    auto setP = [this] (const char* id, float value)
    {
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };
    auto setCurve = [this] (int matrix, float lo, float hi)   // linear ramp across both channels
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int b = 0; b < SpectralCurves::numEditBands; ++b)
            {
                const float t = (float) b / (float) (SpectralCurves::numEditBands - 1);
                curves[(size_t) ch].set (matrix, b, lo + t * (hi - lo));
            }
    };

    presetManager.savePreset ("Init");

    setP ("delayTime", 0.5f); setP ("delaySync", 1.0f); setP ("delayGrid", 1.0f);
    setP ("feedback", 0.45f); setP ("dryWet", 0.5f);
    presetManager.savePreset ("Spectral Echo");
    restore();

    setP ("freeze", 1.0f); setP ("feedback", 0.9f); setP ("dryWet", 0.7f);
    presetManager.savePreset ("Freeze Pad");
    restore();

    setCurve (SpectralCurves::Feedback, 0.25f, 1.0f);   // feedback rises into the highs
    setP ("delayTime", 0.3f); setP ("delaySync", 1.0f); setP ("delayGrid", 1.0f); setP ("dryWet", 0.55f);
    presetManager.savePreset ("Hi-Band Shimmer");
    restore();

    setP ("inputMod", 4.0f); setP ("imCtrl1", 0.6f);    // Smear
    setP ("delayTime", 0.6f); setP ("delayRange", 4.0f); setP ("feedback", 0.5f); setP ("dryWet", 0.6f);
    presetManager.savePreset ("Smear Wash");
    restore();

    presetManager.refresh();
}

FFTDelayAudioProcessor::~FFTDelayAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout
FFTDelayAudioProcessor::createParameterLayout()
{
    using FloatParam  = juce::AudioParameterFloat;
    using ChoiceParam = juce::AudioParameterChoice;
    using BoolParam   = juce::AudioParameterBool;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // --- Output section -------------------------------------------------------
    layout.add (std::make_unique<FloatParam>(
        juce::ParameterID { "dryWet", 1 }, "Dry/Wet",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.5f));

    layout.add (std::make_unique<FloatParam>(
        juce::ParameterID { "effectGain", 1 }, "Effect Gain",
        juce::NormalisableRange<float> { -24.0f, 24.0f, 0.1f }, 0.0f));

    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "mute",   1 }, "Mute",   false));
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "bypass", 1 }, "Bypass", false));
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "freeze", 1 }, "Freeze", false));

    // Stereo: when linked, both channels are driven from the L matrices.
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "link", 1 }, "Link L/R", true));

    // --- Delay / Feedback / Attenuation matrices (global values for now) -------
    layout.add (std::make_unique<FloatParam>(
        juce::ParameterID { "delayTime", 1 }, "Delay",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f, 0.5f }, 0.1f));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "delayRange", 1 }, "Delay Range",
        juce::StringArray { "375 ms", "750 ms", "1.5 s", "3 s", "6 s", "12 s" }, 3));

    // Tempo grid for the delay matrix: snap per-band delays to a musical division.
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "delaySync", 1 }, "Sync", false));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "delayGrid", 1 }, "Grid",
        juce::StringArray { "1/4", "1/8", "1/8T", "1/16" }, 1));

    // When on, the grid tracks live host tempo; off uses a fixed 120 BPM reference.
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "autoQuantize", 1 }, "Auto Quant", true));

    layout.add (std::make_unique<FloatParam>(
        juce::ParameterID { "feedback", 1 }, "Feedback",
        juce::NormalisableRange<float> { 0.0f, 0.99f, 0.001f }, 0.3f));

    layout.add (std::make_unique<FloatParam>(
        juce::ParameterID { "attenuation", 1 }, "Attenuation",
        juce::NormalisableRange<float> { -60.0f, 0.0f, 0.1f }, 0.0f));

    // --- Spectral analysis (FFT) ---------------------------------------------
    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "fftSize", 1 }, "Bands",
        juce::StringArray { "64", "128", "256", "512", "1024" }, 4));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "overlap", 1 }, "Overlap",
        juce::StringArray { "None", "2x", "3x" }, 1));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "window", 1 }, "Window",
        juce::StringArray { "Hann", "Triangle", "Hamming", "Rectangle" }, 0));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "editMask", 1 }, "Edit Mask",
        juce::StringArray { "Linear", "Adapted 2x", "Adapted 4x", "Logarithmic" }, 0));

    // --- Input Modulation (frequency-domain treatments) ----------------------
    // A left set + a right set with an 'L' link; when linked, both use the left.
    const juce::StringArray imAlgorithms { "None", "Deterioration", "Jello Mold", "PitchRoll",
                                           "Smear", "TimeSponge", "Foam", "Lime Twist", "Horse Tail",
                                           "Mini-Pulses", "Imprint", "Phase Blaster" };

    auto addInputModSet = [&] (const juce::String& suffix, const juce::String& label)
    {
        layout.add (std::make_unique<ChoiceParam>(
            juce::ParameterID { "inputMod" + suffix, 1 }, "Input Mod" + label, imAlgorithms, 0));

        for (int i = 1; i <= 3; ++i)
            layout.add (std::make_unique<FloatParam>(
                juce::ParameterID { "imCtrl" + juce::String (i) + suffix, 1 },
                "IM" + label + " Ctrl " + juce::String (i),
                juce::NormalisableRange<float> { 0.0f, 1.0f, 0.001f }, 0.5f));
    };

    addInputModSet ("", "");   // left set (parameter order preserved)
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "imLink", 1 }, "IM Link", true));
    addInputModSet ("R", " R");

    // --- LFO ------------------------------------------------------------------
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "lfoOn", 1 }, "LFO On", false));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "lfoWave", 1 }, "LFO Wave",
        juce::StringArray { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random" }, 0));

    layout.add (std::make_unique<FloatParam>(
        juce::ParameterID { "lfoRate", 1 }, "LFO Rate",
        juce::NormalisableRange<float> { 0.01f, 20.0f, 0.01f, 0.4f }, 1.0f));

    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "lfoSync", 1 }, "LFO Sync", false));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "lfoSyncRate", 1 }, "LFO Div",
        juce::StringArray { "1/1", "1/2", "1/4", "1/8", "1/16" }, 2));

    // Three modulation routings: destination + bipolar depth.
    const juce::StringArray destinations { "None", "Attenuation", "Delay", "Feedback",
                                           "Dry/Wet", "Effect Gain", "Matrix X (freq)",
                                           "Matrix Y (level)" };
    for (int i = 1; i <= 3; ++i)
    {
        layout.add (std::make_unique<ChoiceParam>(
            juce::ParameterID { "lfoDest" + juce::String (i), 1 },
            "LFO Dest " + juce::String (i), destinations, 0));

        layout.add (std::make_unique<FloatParam>(
            juce::ParameterID { "lfoDepth" + juce::String (i), 1 },
            "LFO Depth " + juce::String (i),
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f));
    }

    return layout;
}

int FFTDelayAudioProcessor::binToEditBand (int bin, int activeBins, int mask) noexcept
{
    const int lastBin  = juce::jmax (1, activeBins - 1);
    const int lastBand = SpectralCurves::numEditBands - 1;

    // Logarithmic frequency axis vs the default linear one.
    float ratio = mask == 3 ? std::log (1.0f + (float) bin) / std::log (1.0f + (float) lastBin)
                            : (float) bin / (float) lastBin;

    int band = juce::jlimit (0, lastBand, (int) std::lround (ratio * (float) lastBand));

    // Adapted masks coarsen the editable resolution (2x -> 80 points, 4x -> 40).
    if (mask == 1) band = (band / 2) * 2;
    else if (mask == 2) band = (band / 4) * 4;

    return band;
}

void FFTDelayAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (samplesPerBlock);

    for (auto& sd : spectralDelays)
        sd.prepare (sampleRate);

    // Dry-path delay ring, one slot per latency sample, so dry stays aligned
    // with the wet output. Sized to the engine's worst-case (constant) latency.
    const int latency = SpectralDelay::getLatencySamples();
    for (auto& d : dryDelay)
        d.assign ((size_t) latency, 0.0f);
    dryPos.fill (0);

    // Tell the host how many samples of latency our STFT introduces.
    setLatencySamples (latency);
}

void FFTDelayAudioProcessor::releaseResources()
{
    for (auto& sd : spectralDelays)
        sd.reset();
}

bool FFTDelayAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Only support matching mono->mono or stereo->stereo layouts.
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono()
        && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return mainOut == layouts.getMainInputChannelSet();
}

void FFTDelayAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                           juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Clear any output channels that don't have a matching input.
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // --- MIDI Learn: bind armed CCs, then apply learned CCs to their targets ---
    const auto& params = getParameters();
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();
        if (! msg.isController())
            continue;

        const int   cc    = msg.getControllerNumber();
        const int   chan  = msg.getChannel();              // 1..16
        const float val01 = (float) msg.getControllerValue() / 127.0f;

        // If armed, the first incoming CC captures the assignment.
        if (const int armed = midiLearn.getArmed(); armed != MidiLearnManager::noTarget)
        {
            midiLearn.bind (cc, armed);
            midiLearn.disarm();
        }

        const int target = midiLearn.target[(size_t) cc].load();
        if (target == MidiLearnManager::noTarget)
            continue;

        const int reqChan = midiLearn.channel[(size_t) cc].load();
        if (reqChan != 0 && reqChan != chan)
            continue;

        if (target == MidiLearnManager::presetNext || target == MidiLearnManager::presetPrev)
        {
            if (val01 > 0.5f)   // momentary: trigger on press
            {
                pendingPresetAction.store (target == MidiLearnManager::presetNext ? 1 : -1);
                triggerAsyncUpdate();
            }
            continue;
        }

        if (juce::isPositiveAndBelow (target, params.size()))
        {
            const float lo  = midiLearn.rangeMin[(size_t) cc].load();
            const float hi  = midiLearn.rangeMax[(size_t) cc].load();
            const float v   = midiLearn.invert[(size_t) cc].load() ? 1.0f - val01 : val01;
            params[target]->setValueNotifyingHost (lo + v * (hi - lo));
        }
    }

    const int numSamples = buffer.getNumSamples();

    // Read parameters once per block (lock-free atomic loads).
    float dryWet       = apvts.getRawParameterValue ("dryWet")->load();
    float effectGainDb = apvts.getRawParameterValue ("effectGain")->load();
    const bool  mute   = apvts.getRawParameterValue ("mute")->load()   > 0.5f;
    const bool  bypass = apvts.getRawParameterValue ("bypass")->load() > 0.5f;
    const bool  freeze = apvts.getRawParameterValue ("freeze")->load() > 0.5f;

    float       delayFrac = apvts.getRawParameterValue ("delayTime")->load();
    const int   rangeIdx  = (int) apvts.getRawParameterValue ("delayRange")->load();
    float       feedback  = apvts.getRawParameterValue ("feedback")->load();
    float       attenDb   = apvts.getRawParameterValue ("attenuation")->load();

    const int   fftIdx     = (int) apvts.getRawParameterValue ("fftSize")->load();
    const int   overlapIdx = (int) apvts.getRawParameterValue ("overlap")->load();
    const int   windowIdx  = (int) apvts.getRawParameterValue ("window")->load();
    const int   maskIdx    = (int) apvts.getRawParameterValue ("editMask")->load();

    const bool  delaySync    = apvts.getRawParameterValue ("delaySync")->load()    > 0.5f;
    const int   gridIdx      = (int) apvts.getRawParameterValue ("delayGrid")->load();
    const bool  autoQuantize = apvts.getRawParameterValue ("autoQuantize")->load() > 0.5f;
    const bool  linked       = apvts.getRawParameterValue ("link")->load() > 0.5f;

    // Host tempo (used by both the delay grid and a synced LFO).
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm(); hostBpm && *hostBpm > 0.0)
                bpm = *hostBpm;

    // --- LFO: advance once per block, then route to up to 3 destinations -------
    const bool lfoOn = apvts.getRawParameterValue ("lfoOn")->load() > 0.5f;
    float lfoValue = 0.0f;
    float matrixXmod = 0.0f, matrixYmod = 0.0f;   // LFO -> matrix axis offsets
    if (lfoOn)
    {
        const int   wave    = (int) apvts.getRawParameterValue ("lfoWave")->load();
        const bool  lfoSync = apvts.getRawParameterValue ("lfoSync")->load() > 0.5f;
        double rateHz = apvts.getRawParameterValue ("lfoRate")->load();
        if (lfoSync)
        {
            const int syncIdx = (int) apvts.getRawParameterValue ("lfoSyncRate")->load();
            static constexpr double beatsPerCycle[] = { 4.0, 2.0, 1.0, 0.5, 0.25 }; // 1/1..1/16
            rateHz = bpm / (60.0 * beatsPerCycle[juce::jlimit (0, 4, syncIdx)]);
        }

        const double prevPhase = lfoPhase;
        lfoPhase += (double) numSamples * rateHz / getSampleRate();
        const bool wrapped = lfoPhase >= 1.0;
        lfoPhase -= std::floor (lfoPhase);
        if (wrapped || prevPhase == 0.0)
        {
            lfoRng = lfoRng * 1664525u + 1013904223u;          // LCG for S&H
            lfoRandom = (float) (lfoRng >> 8) / (float) (1u << 24) * 2.0f - 1.0f;
        }

        const double twoPi = juce::MathConstants<double>::twoPi;
        switch (wave)
        {
            case 0: lfoValue = (float) std::sin (twoPi * lfoPhase);          break; // Sine
            case 1: lfoValue = 4.0f * std::abs ((float) lfoPhase - 0.5f) - 1.0f; break; // Triangle
            case 2: lfoValue = 2.0f * (float) lfoPhase - 1.0f;              break; // Saw Up
            case 3: lfoValue = 1.0f - 2.0f * (float) lfoPhase;              break; // Saw Down
            case 4: lfoValue = lfoPhase < 0.5 ? 1.0f : -1.0f;               break; // Square
            case 5: default: lfoValue = lfoRandom;                          break; // Random / S&H
        }

        // Apply each routing slot to its destination (additive, then clamped).
        for (int i = 1; i <= 3; ++i)
        {
            const int   dest  = (int) apvts.getRawParameterValue ("lfoDest"  + juce::String (i))->load();
            const float depth = apvts.getRawParameterValue ("lfoDepth" + juce::String (i))->load();
            const float mod   = depth * lfoValue;
            switch (dest)
            {
                case 1: attenDb      += mod * 24.0f; break;  // Attenuation (dB)
                case 2: delayFrac    += mod * 0.5f;  break;  // Delay (fraction of range)
                case 3: feedback     += mod * 0.5f;  break;  // Feedback
                case 4: dryWet       += mod * 0.5f;  break;  // Dry/Wet
                case 5: effectGainDb += mod * 12.0f; break;  // Effect Gain (dB)
                case 6: matrixXmod   += mod;         break;  // Matrix X: scroll curves in freq
                case 7: matrixYmod   += mod;         break;  // Matrix Y: offset curve levels
                default: break;                              // None
            }
        }
    }
    else
    {
        lfoPhase = 0.0;
    }

    dryWet    = juce::jlimit (0.0f, 1.0f,  dryWet);
    feedback  = juce::jlimit (0.0f, 0.99f, feedback);
    delayFrac = juce::jlimit (0.0f, 1.0f,  delayFrac);
    attenDb   = juce::jlimit (-60.0f, 0.0f, attenDb);

    const float effectGain = juce::Decibels::decibelsToGain (effectGainDb);

    static constexpr float rangeSeconds[] = { 0.375f, 0.75f, 1.5f, 3.0f, 6.0f, 12.0f };
    const float masterDelaySeconds = delayFrac * rangeSeconds[juce::jlimit (0, 5, rangeIdx)];
    const float masterAttenGain    = juce::Decibels::decibelsToGain (attenDb);

    // Tempo grid: snap delays to a musical division of the (live or fixed) tempo.
    float gridSeconds = 0.0f;
    if (delaySync)
    {
        const double gridBpm = autoQuantize ? bpm : 120.0;
        static constexpr float gridFactor[] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f }; // 1/4,1/8,1/8T,1/16
        gridSeconds = (float) (60.0 / gridBpm) * gridFactor[juce::jlimit (0, 3, gridIdx)];
    }
    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) spectralDelays.size());

    const bool  imLink = apvts.getRawParameterValue ("imLink")->load() > 0.5f;
    const int   imAlgo[2] = { (int) apvts.getRawParameterValue ("inputMod")->load(),
                              (int) apvts.getRawParameterValue ("inputModR")->load() };
    const float imC1[2]   = { apvts.getRawParameterValue ("imCtrl1")->load(),  apvts.getRawParameterValue ("imCtrl1R")->load() };
    const float imC2[2]   = { apvts.getRawParameterValue ("imCtrl2")->load(),  apvts.getRawParameterValue ("imCtrl2R")->load() };
    const float imC3[2]   = { apvts.getRawParameterValue ("imCtrl3")->load(),  apvts.getRawParameterValue ("imCtrl3R")->load() };

    // Settle framing on every engine first so all channels share one bin layout.
    for (int ch = 0; ch < (int) spectralDelays.size(); ++ch)
    {
        auto& sd = spectralDelays[(size_t) ch];
        const int s = imLink ? 0 : ch;   // which input-mod set this channel uses
        sd.setFftOrder (SpectralDelay::minFftOrder + fftIdx);
        sd.setOverlap (overlapIdx);
        sd.setWindowType (windowIdx);
        sd.setFreeze (freeze);
        sd.setInputMod (imAlgo[s], imC1[s], imC2[s], imC3[s]);
        sd.applyPendingSettings();
    }

    const int activeBins = spectralDelays[0].getActiveNumBins();

    // LFO -> matrix axes: X scrolls the painted curves in frequency (in edit
    // bands), Y offsets every curve's level.
    const int   matrixXshift = (int) std::lround (matrixXmod * (float) (SpectralCurves::numEditBands / 4));
    const float matrixYoff   = matrixYmod * 0.5f;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& sd = spectralDelays[(size_t) ch];

        // Fan the painted edit-band curves onto the analysis bins, from this
        // channel's own matrices (or the L set when linked). The curve value is a
        // 0..1 multiplier on the master knob, so flat curves == global mode.
        const SpectralCurves& src = curves[(size_t) (linked ? 0 : ch)];
        for (int b = 0; b < SpectralDelay::maxBins; ++b)
        {
            int band = binToEditBand (juce::jmin (b, activeBins - 1), activeBins, maskIdx) + matrixXshift;
            band = juce::jlimit (0, SpectralCurves::numEditBands - 1, band);

            auto curveVal = [&] (int matrix)
            {
                return juce::jlimit (0.0f, 1.0f, src.get (matrix, band) + matrixYoff);
            };

            gainCurve[(size_t) b]     = masterAttenGain * curveVal (SpectralCurves::Attenuation);
            feedbackCurve[(size_t) b] = feedback        * curveVal (SpectralCurves::Feedback);

            float delaySeconds = masterDelaySeconds * curveVal (SpectralCurves::Delay);
            if (gridSeconds > 0.0f)
                delaySeconds = std::round (delaySeconds / gridSeconds) * gridSeconds;
            delayCurve[(size_t) b] = delaySeconds;
        }

        sd.setBandGain (gainCurve.data());
        sd.setBandFeedback (feedbackCurve.data());
        sd.setBandDelaySeconds (delayCurve.data());

        auto& ring = dryDelay[(size_t) ch];
        int&  pos  = dryPos[(size_t) ch];
        const int ringLen = (int) ring.size();

        auto* samples = buffer.getWritePointer (ch);
        for (int n = 0; n < numSamples; ++n)
        {
            const float in  = samples[n];
            const float wet = sd.processSample (in);

            // Latency-aligned dry sample.
            const float dry = ringLen > 0 ? ring[(size_t) pos] : in;
            if (ringLen > 0) { ring[(size_t) pos] = in; pos = (pos + 1) % ringLen; }

            if (bypass)
                samples[n] = dry;                  // time-aligned dry passthrough
            else
            {
                const float w = mute ? 0.0f : wet * effectGain;
                samples[n] = dry * (1.0f - dryWet) + w * dryWet;
            }
        }
    }

    // Publish channel 0's analysed spectrum for the editor's display.
    const auto& mags = spectralDelays[0].getMagnitudes();
    const int   nb   = spectralDelays[0].getActiveNumBins();
    for (int b = 0; b < nb; ++b)
        displaySpectrum[(size_t) b].store (mags[(size_t) b], std::memory_order_relaxed);
    displayBins.store (nb, std::memory_order_relaxed);
}

bool FFTDelayAudioProcessor::hasEditor() const                                  { return true; }
juce::AudioProcessorEditor* FFTDelayAudioProcessor::createEditor()              { return new FFTDelayAudioProcessorEditor (*this); }

const juce::String FFTDelayAudioProcessor::getName() const                      { return JucePlugin_Name; }
bool FFTDelayAudioProcessor::acceptsMidi() const                               { return true; }
bool FFTDelayAudioProcessor::producesMidi() const                             { return false; }
bool FFTDelayAudioProcessor::isMidiEffect() const                            { return false; }
double FFTDelayAudioProcessor::getTailLengthSeconds() const                   { return 0.0; }

int FFTDelayAudioProcessor::getNumPrograms()                                   { return 1; }
int FFTDelayAudioProcessor::getCurrentProgram()                               { return 0; }
void FFTDelayAudioProcessor::setCurrentProgram (int index)                    { juce::ignoreUnused (index); }
const juce::String FFTDelayAudioProcessor::getProgramName (int index)         { juce::ignoreUnused (index); return {}; }
void FFTDelayAudioProcessor::changeProgramName (int index, const juce::String& newName) { juce::ignoreUnused (index, newName); }

void FFTDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    // Stash the painted matrices (both channels) alongside the parameters.
    state.removeChild (state.getChildWithName ("CURVES"), nullptr);
    juce::ValueTree curvesTree ("CURVES");
    static const char* matrixIds[] = { "atten", "delay", "feedback" };
    for (int ch = 0; ch < (int) curves.size(); ++ch)
        for (int m = 0; m < SpectralCurves::numMatrices; ++m)
        {
            juce::String s;
            for (int b = 0; b < SpectralCurves::numEditBands; ++b)
                s << curves[(size_t) ch].get (m, b) << ' ';
            curvesTree.setProperty (juce::String (matrixIds[m]) + "_" + juce::String (ch), s.trim(), nullptr);
        }
    state.appendChild (curvesTree, nullptr);

    // Stash MIDI Learn assignments: one "cc:target:min:max:invert:channel" token
    // per assigned CC.
    state.removeChild (state.getChildWithName ("MIDILEARN"), nullptr);
    juce::ValueTree midiTree ("MIDILEARN");
    juce::String map;
    for (int cc = 0; cc < MidiLearnManager::numCC; ++cc)
    {
        const int t = midiLearn.target[(size_t) cc].load();
        if (t == MidiLearnManager::noTarget)
            continue;
        map << cc << ':' << t << ':'
            << midiLearn.rangeMin[(size_t) cc].load() << ':'
            << midiLearn.rangeMax[(size_t) cc].load() << ':'
            << (midiLearn.invert[(size_t) cc].load() ? 1 : 0) << ':'
            << midiLearn.channel[(size_t) cc].load() << ' ';
    }
    midiTree.setProperty ("map", map.trim(), nullptr);
    state.appendChild (midiTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FFTDelayAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);

            if (auto curvesTree = tree.getChildWithName ("CURVES"); curvesTree.isValid())
            {
                static const char* matrixIds[] = { "atten", "delay", "feedback" };
                for (int ch = 0; ch < (int) curves.size(); ++ch)
                    for (int m = 0; m < SpectralCurves::numMatrices; ++m)
                    {
                        const auto id = juce::String (matrixIds[m]) + "_" + juce::String (ch);

                        // Fall back to the un-suffixed key so older single-channel
                        // presets still load (into both channels).
                        auto prop = curvesTree.getProperty (id);
                        if (prop.isVoid())
                            prop = curvesTree.getProperty (matrixIds[m]);

                        auto tokens = juce::StringArray::fromTokens (prop.toString(), " ", "");
                        for (int b = 0; b < juce::jmin (tokens.size(), SpectralCurves::numEditBands); ++b)
                            curves[(size_t) ch].set (m, b, tokens[b].getFloatValue());
                    }
            }

            if (auto midiTree = tree.getChildWithName ("MIDILEARN"); midiTree.isValid())
            {
                midiLearn.clearAll();
                auto entries = juce::StringArray::fromTokens (
                    midiTree.getProperty ("map").toString(), " ", "");
                for (const auto& e : entries)
                {
                    auto f = juce::StringArray::fromTokens (e, ":", "");
                    if (f.size() < 6)
                        continue;
                    const int cc = f[0].getIntValue();
                    if (! juce::isPositiveAndBelow (cc, MidiLearnManager::numCC))
                        continue;
                    midiLearn.target  [(size_t) cc].store (f[1].getIntValue());
                    midiLearn.rangeMin[(size_t) cc].store (f[2].getFloatValue());
                    midiLearn.rangeMax[(size_t) cc].store (f[3].getFloatValue());
                    midiLearn.invert  [(size_t) cc].store (f[4].getIntValue() != 0);
                    midiLearn.channel [(size_t) cc].store (f[5].getIntValue());
                }
            }

            apvts.replaceState (tree);
        }
    }
}

void FFTDelayAudioProcessor::handleAsyncUpdate()
{
    // Runs on the message thread: perform the MIDI-requested preset change off
    // the audio thread, then let the editor refresh.
    const int action = pendingPresetAction.exchange (0);
    if (action > 0)      presetManager.next();
    else if (action < 0) presetManager.previous();

    if (action != 0 && onPresetChangedFromMidi)
        onPresetChangedFromMidi();
}

// This is what the host's plugin loader calls to instantiate the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FFTDelayAudioProcessor();
}
