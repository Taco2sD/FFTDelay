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

    // Optionally start each recalled preset with clean delay memory.
    presetManager.onPresetLoaded = [this]
    {
        if (apvts.getRawParameterValue ("zeroDelayOnProgram")->load() > 0.5f)
            requestEngineReset();
    };

    // Fresh instances (no assignments yet) get the factory CC map; a loaded
    // session/preset restores its own map over this in setStateInformation.
    bool anyAssigned = false;
    for (int cc = 0; cc < MidiLearnManager::numCC && ! anyAssigned; ++cc)
        anyAssigned = midiLearn.target[(size_t) cc].load() != MidiLearnManager::noTarget;
    if (! anyAssigned)
        applyFactoryMidiMap();
}

void FFTDelayAudioProcessor::applyFactoryMidiMap()
{
    midiLearn.clearAll();

    auto bind = [this] (int cc, const char* paramId)
    {
        if (auto* p = apvts.getParameter (paramId))
            midiLearn.bind (cc, p->getParameterIndex());
    };

    bind (1, "lfoRate");
    bind (2, "lfoDepth1");  bind (3, "lfoDepth2");  bind (4, "lfoDepth3");
    bind (5, "lfoOn");
    bind (8, "lfoDest1");   bind (9, "lfoDest2");   bind (10, "lfoDest3");
    bind (11, "lfoWave");
    bind (64, "freeze");
    bind (66, "imCtrl1");   bind (67, "imCtrl2");   bind (68, "imCtrl3");
    bind (69, "imCtrl1R");  bind (70, "imCtrl2R");  bind (71, "imCtrl3R");
    bind (72, "attOffX");   bind (73, "delayOffX"); bind (74, "fbOffX");
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

    setCurve (SpectralCurves::Delay, 0.35f, 0.35f); setCurve (SpectralCurves::Feedback, 0.45f, 0.45f);
    setP ("delaySync", 1.0f); setP ("delayGrid", 1.0f); setP ("dryWet", 0.5f);
    presetManager.savePreset ("Spectral Echo");
    restore();

    setCurve (SpectralCurves::Feedback, 0.9f, 0.9f);
    setP ("freeze", 1.0f); setP ("dryWet", 0.7f);
    presetManager.savePreset ("Freeze Pad");
    restore();

    setCurve (SpectralCurves::Feedback, 0.25f, 1.0f);   // feedback rises into the highs
    setCurve (SpectralCurves::Delay, 0.3f, 0.3f);
    setP ("delaySync", 1.0f); setP ("delayGrid", 1.0f); setP ("dryWet", 0.55f);
    presetManager.savePreset ("Hi-Band Shimmer");
    restore();

    setCurve (SpectralCurves::Delay, 0.6f, 0.6f); setCurve (SpectralCurves::Feedback, 0.5f, 0.5f);
    setP ("inputMod", 4.0f); setP ("imCtrl1", 0.6f);    // Smear
    setP ("delayRange", 4.0f); setP ("dryWet", 0.6f);
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

    // --- The three matrices ----------------------------------------------------
    // The painted curves ARE the values (no global scalers behind them). Each
    // matrix gets: an automatable value-axis offset (added to every point of both
    // channels at fan-out time), an L/R link, and a stage bypass.
    for (const auto* m : { "att", "delay", "fb" })
    {
        const juce::String id (m), Name = juce::String (m) == "att"   ? "Attenuation"
                                        : juce::String (m) == "delay" ? "Delay" : "Feedback";
        layout.add (std::make_unique<FloatParam>(
            juce::ParameterID { id + "OffX", 1 }, Name + " Offset",
            juce::NormalisableRange<float> { -1.0f, 1.0f, 0.001f }, 0.0f));
        layout.add (std::make_unique<BoolParam>(juce::ParameterID { id + "Link",   1 }, Name + " Link",   true));
        layout.add (std::make_unique<BoolParam>(juce::ParameterID { id + "Bypass", 1 }, Name + " Bypass", false));
    }

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "delayRange", 1 }, "Max Delay",
        juce::StringArray { "375 ms", "750 ms", "1.5 s", "3 s", "6 s", "12 s" }, 3));

    // Tempo grid for the delay matrix: snap per-band delays to a musical division.
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "delaySync", 1 }, "Sync", false));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "delayGrid", 1 }, "Grid",
        juce::StringArray { "1/4", "1/8", "1/8T", "1/16" }, 1));

    // When on, the grid tracks live host tempo; off uses a fixed 120 BPM reference.
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "autoQuantize", 1 }, "Auto Quant", true));

    // --- Housekeeping options --------------------------------------------------
    // Clear delay memory when a preset is recalled / when MIDI system-reset
    // arrives, so stale echoes never bleed into new material.
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "zeroDelayOnProgram", 1 }, "Zero Dly on Prg", true));
    layout.add (std::make_unique<BoolParam>(juce::ParameterID { "ignoreMidiReset",    1 }, "Ignore MIDI Rst", false));

    // --- Spectral analysis (FFT) ---------------------------------------------
    // Choices are BAND counts (frame = 2 x bands): 64 bands = FFT 128 ... 1024
    // bands = FFT 2048. Index maps to SpectralDelay::minFftOrder + idx.
    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "fftSize", 1 }, "Bands",
        juce::StringArray { "64", "128", "256", "512", "1024" }, 3));

    // Hop = frame / (overlap + 1); 0x deliberately breaks perfect reconstruction.
    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "overlap", 1 }, "Overlap",
        juce::StringArray { "0x", "1x", "2x" }, 1));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "window", 1 }, "Window",
        juce::StringArray { "Hann", "Triangle", "Hamming", "Rectangle" }, 0));

    layout.add (std::make_unique<ChoiceParam>(
        juce::ParameterID { "editMask", 1 }, "Edit Mask",
        juce::StringArray { "Linear", "Adapted 2x", "Adapted 4x", "Logarithmic" }, 0));

    // --- Input Modulation (frequency-domain treatments) ----------------------
    // A left set + a right set with an 'L' link; when linked, both use the left.
    const juce::StringArray imAlgorithms { "None", "Erode", "Invert", "Bin Shift",
                                           "Smear", "Drift", "Ripple", "Scatter", "Comb",
                                           "Pulse Gate", "Spectral Gate", "Phase Scramble" };

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

    // Three modulation routings: destination + bipolar depth. Destinations 1..18
    // are {matrix} x {channel} x {value-offset X / frequency-shift Y / selected
    // bands}; 19..24 modulate the Input Mod controls. Decoded in processBlock.
    juce::StringArray destinations { "No Assignment" };
    for (const auto* m : { "Att", "Delay", "FB" })
        for (const auto* ch : { "L", "R" })
            for (const auto* axis : { "X", "Y", "Sel" })
                destinations.add (juce::String (m) + " " + ch + " " + axis);
    for (const auto* ch : { "L", "R" })
        for (int i = 1; i <= 3; ++i)
            destinations.add ("Input Mod " + juce::String (ch) + " " + juce::String (i));

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

    // A requested reset (button, preset recall, MIDI reset) clears all delay
    // memory here, on the audio thread, where the engines are owned.
    if (engineResetPending.exchange (false))
        for (auto& sd : spectralDelays)
            sd.reset();

    // Clear any output channels that don't have a matching input.
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // --- MIDI Learn: bind armed CCs, then apply learned CCs to their targets ---
    const auto& params = getParameters();
    for (const auto metadata : midiMessages)
    {
        const auto msg = metadata.getMessage();

        // MIDI system reset (0xFF) clears delay memory unless the option says not to.
        if (msg.getRawDataSize() == 1 && msg.getRawData()[0] == 0xff
            && apvts.getRawParameterValue ("ignoreMidiReset")->load() < 0.5f)
        {
            engineResetPending.store (true);
            continue;
        }

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

    // Per-matrix controls, indexed [Attenuation, Delay, Feedback] to match
    // SpectralCurves::Matrix. offX is the automatable whole-curve value offset.
    static const char* matrixIds[] = { "att", "delay", "fb" };
    float offX     [SpectralCurves::numMatrices];
    bool  mtxLink  [SpectralCurves::numMatrices];
    bool  mtxBypass[SpectralCurves::numMatrices];
    for (int m = 0; m < SpectralCurves::numMatrices; ++m)
    {
        const juce::String id (matrixIds[m]);
        offX[m]      = apvts.getRawParameterValue (id + "OffX")->load();
        mtxLink[m]   = apvts.getRawParameterValue (id + "Link")->load()   > 0.5f;
        mtxBypass[m] = apvts.getRawParameterValue (id + "Bypass")->load() > 0.5f;
    }

    const int   rangeIdx   = (int) apvts.getRawParameterValue ("delayRange")->load();
    const int   fftIdx     = (int) apvts.getRawParameterValue ("fftSize")->load();
    const int   overlapIdx = (int) apvts.getRawParameterValue ("overlap")->load();
    const int   windowIdx  = (int) apvts.getRawParameterValue ("window")->load();
    const int   maskIdx    = (int) apvts.getRawParameterValue ("editMask")->load();

    const bool  delaySync    = apvts.getRawParameterValue ("delaySync")->load()    > 0.5f;
    const int   gridIdx      = (int) apvts.getRawParameterValue ("delayGrid")->load();
    const bool  autoQuantize = apvts.getRawParameterValue ("autoQuantize")->load() > 0.5f;

    // Host tempo (used by the delay grid, a synced LFO, and the UI readouts).
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto hostBpm = pos->getBpm(); hostBpm && *hostBpm > 0.0)
                bpm = *hostBpm;
    displayBpm.store ((float) bpm, std::memory_order_relaxed);

    // --- LFO: advance once per block, then route to up to 3 destinations -------
    // Modulation targets are whole-curve offsets applied at fan-out time (the
    // stored curves are never redrawn): X = value offset, Y = frequency shift,
    // Sel = selected-bands offset (inert until band selection lands). Plus the
    // six Input Mod controls.
    const bool lfoOn = apvts.getRawParameterValue ("lfoOn")->load() > 0.5f;
    float lfoValue = 0.0f;
    float lfoX[SpectralCurves::numMatrices][2] {};   // [matrix][channel] value offset
    float lfoY[SpectralCurves::numMatrices][2] {};   // [matrix][channel] freq shift
    float imMod[2][3] {};                            // [channel][control]
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

        // Decode each routing slot: 0 none; 1..18 = (matrix, channel, X/Y/Sel)
        // in matrix-major order; 19..24 = Input Mod controls (L 1-3, R 1-3).
        for (int i = 1; i <= 3; ++i)
        {
            const int   dest  = (int) apvts.getRawParameterValue ("lfoDest"  + juce::String (i))->load();
            const float mod   = apvts.getRawParameterValue ("lfoDepth" + juce::String (i))->load() * lfoValue;
            if (dest >= 1 && dest <= 18)
            {
                const int m = (dest - 1) / 6, ch = ((dest - 1) % 6) / 3, axis = (dest - 1) % 3;
                if      (axis == 0) lfoX[m][ch] += mod;
                else if (axis == 1) lfoY[m][ch] += mod;
                // axis == 2 (Sel): applied once band selection exists.
            }
            else if (dest >= 19 && dest <= 24)
            {
                imMod[(dest - 19) / 3][(dest - 19) % 3] += mod;
            }
        }
    }
    else
    {
        lfoPhase = 0.0;
    }

    dryWet = juce::jlimit (0.0f, 1.0f, dryWet);
    const float effectGain = juce::Decibels::decibelsToGain (effectGainDb);

    static constexpr float rangeSeconds[] = { 0.375f, 0.75f, 1.5f, 3.0f, 6.0f, 12.0f };
    const float maxDelaySeconds = rangeSeconds[juce::jlimit (0, 5, rangeIdx)];

    // Tempo grid: snap delays to a musical division of the (live or fixed) tempo.
    float gridSeconds = 0.0f;
    if (delaySync)
    {
        const double gridBpm = autoQuantize ? bpm : 120.0;
        static constexpr float gridFactor[] = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f }; // 1/4,1/8,1/8T,1/16
        gridSeconds = (float) (60.0 / gridBpm) * gridFactor[juce::jlimit (0, 3, gridIdx)];
    }
    const int numChannels = juce::jmin (buffer.getNumChannels(), (int) spectralDelays.size());

    const bool  imLink   = apvts.getRawParameterValue ("imLink")->load()   > 0.5f;
    const bool  imBypass = apvts.getRawParameterValue ("imBypass")->load() > 0.5f;
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
        auto imc = [&] (float base, int ctl) { return juce::jlimit (0.0f, 1.0f, base + imMod[s][ctl]); };
        sd.setInputMod (imBypass ? 0 : imAlgo[s],
                        imc (imC1[s], 0), imc (imC2[s], 1), imc (imC3[s], 2));
        sd.applyPendingSettings();
    }

    const int activeBins = spectralDelays[0].getActiveNumBins();

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& sd = spectralDelays[(size_t) ch];

        // Fan the painted edit-band curves onto the analysis bins. Each matrix
        // resolves its own source channel (its Link makes both use the L set)
        // and applies its offsets: X (param + LFO) shifts every point's value,
        // Y (LFO) shifts the curve along the frequency axis in edit bands.
        auto curveVal = [&] (int m, int band)
        {
            if (mtxBypass[m])
                return m == SpectralCurves::Attenuation ? 1.0f : 0.0f;   // stage passthrough

            const int mch   = mtxLink[m] ? 0 : ch;
            const int shift = (int) std::lround (lfoY[m][mch] * (float) (SpectralCurves::numEditBands / 4));
            band = juce::jlimit (0, SpectralCurves::numEditBands - 1, band + shift);
            return juce::jlimit (0.0f, 1.0f,
                                 curves[(size_t) mch].get (m, band) + offX[m] + lfoX[m][mch]);
        };

        for (int b = 0; b < SpectralDelay::maxBins; ++b)
        {
            const int band = SpectralCurves::binToEditBand (b, activeBins, maskIdx);

            // Curve bottom is a true zero multiplier (full cut), top is unity.
            gainCurve[(size_t) b]     = curveVal (SpectralCurves::Attenuation, band);
            feedbackCurve[(size_t) b] = curveVal (SpectralCurves::Feedback,    band);

            float delaySeconds = maxDelaySeconds * curveVal (SpectralCurves::Delay, band);
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

// Bump when the parameter surface changes incompatibly; setStateInformation
// migrates older states. v2: curves became the sole value source (the global
// delay/feedback/attenuation scalers were removed and baked into the curves).
static constexpr int kStateVersion = 2;

void FFTDelayAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", kStateVersion, nullptr);

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

            // One-shot migration for pre-v2 states: the curves were multipliers
            // on global delay/feedback/attenuation scalers. Bake those scalers
            // into the curves so old sessions keep their sound.
            if ((int) tree.getProperty ("stateVersion", 1) < 2)
            {
                auto oldParam = [&tree] (const juce::String& id, float fallback)
                {
                    auto p = tree.getChildWithProperty ("id", id);
                    return p.isValid() ? (float) p.getProperty ("value", fallback) : fallback;
                };
                const float dly = oldParam ("delayTime", 0.1f);
                const float fb  = oldParam ("feedback", 0.3f);
                const float att = juce::Decibels::decibelsToGain (oldParam ("attenuation", 0.0f));
                for (auto& c : curves)
                    for (int b = 0; b < SpectralCurves::numEditBands; ++b)
                    {
                        c.set (SpectralCurves::Delay,       b, c.get (SpectralCurves::Delay, b) * dly);
                        c.set (SpectralCurves::Feedback,    b, c.get (SpectralCurves::Feedback, b) * fb);
                        c.set (SpectralCurves::Attenuation, b, c.get (SpectralCurves::Attenuation, b) * att);
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
