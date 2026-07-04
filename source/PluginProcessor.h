#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include "SpectralDelay.h"
#include "SpectralCurves.h"
#include "PresetManager.h"
#include "MidiLearnManager.h"
#include <functional>

// The "processor" is the audio engine: it has no knowledge of the GUI.
// The host (your DAW) calls processBlock() on the audio thread for every
// buffer of samples. Keep that method real-time safe: no allocations, locks,
// or file I/O in there.
class FFTDelayAudioProcessor : public juce::AudioProcessor,
                               public juce::AsyncUpdater
{
public:
    FFTDelayAudioProcessor();
    ~FFTDelayAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Single source of truth for parameters. The GUI attaches to this, and
    // it handles save/restore of plugin state for you.
    juce::AudioProcessorValueTreeState apvts;

    // Latest analysed spectrum (channel 0), published lock-free for the editor's
    // spectral-analyzer display. Written on the audio thread, read on the UI thread.
    std::array<std::atomic<float>, SpectralDelay::maxBins> displaySpectrum {};
    std::atomic<int> displayBins { 0 };

    // The three painted matrices, one independent set per channel (L = 0, R = 1).
    // The editor draws into these; processBlock reads them. Each matrix has its
    // own L/R link parameter; when linked, both channels use the L set.
    std::array<SpectralCurves, 2> curves;

    // On-disk presets (save/recall + next/previous). Constructed after apvts so
    // saving can serialise the parameter state.
    PresetManager presetManager { *this };

    // MIDI Learn: every parameter + preset Next/Prev are CC-assignable.
    MidiLearnManager midiLearn;

    // MIDI-triggered preset navigation is deferred to the message thread (preset
    // I/O is not realtime safe) via AsyncUpdater. The editor sets the callback so
    // it can refresh its UI after a MIDI-driven preset change.
    void handleAsyncUpdate() override;
    std::atomic<int>      pendingPresetAction { 0 };  // +1 next, -1 prev
    std::function<void()> onPresetChangedFromMidi;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Write a small bank of factory presets to disk on first run (empty folder).
    void createFactoryPresets();

    // One spectral delay engine per channel (stereo).
    std::array<SpectralDelay, 2> spectralDelays;

    // Latency-compensated dry path (one ring per channel), so the parallel dry
    // signal stays phase-aligned with the STFT-delayed wet signal.
    std::array<std::vector<float>, 2> dryDelay;
    std::array<int, 2>                dryPos { 0, 0 };

    // Scratch buffers used to push the per-band matrices into the engine each
    // block. Reused so processBlock never allocates.
    std::array<float, SpectralDelay::maxBins> gainCurve {};
    std::array<float, SpectralDelay::maxBins> delayCurve {};
    std::array<float, SpectralDelay::maxBins> feedbackCurve {};

    // LFO state (advanced once per block on the audio thread).
    double   lfoPhase  = 0.0;   // [0, 1)
    float    lfoRandom = 0.0f;  // held value for the Random / S&H waveform
    uint32_t lfoRng    = 0x12345678u;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTDelayAudioProcessor)
};
