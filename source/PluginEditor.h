#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include "PluginProcessor.h"
#include "SpectralCurves.h"

// A mouse-paintable spectral curve — one of the three spectral matrices.
// Frequency runs left->right, the painted value (0..1) runs bottom->top. Drag to
// draw; double-click to reset the whole curve flat.
class MatrixEditor : public juce::Component,
                     private juce::Timer
{
public:
    MatrixEditor (FFTDelayAudioProcessor& proc, SpectralCurves& curvesToEdit, int matrixIndex,
                  juce::String title, juce::Colour colour);

    // When set (channels linked), edits are also written here so L/R stay identical.
    void setMirror (SpectralCurves* m) { mirror = m; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }
    void paintStroke (juce::Point<float> pos);

    FFTDelayAudioProcessor& processorRef;
    SpectralCurves* curves;
    SpectralCurves* mirror = nullptr;
    int             matrix;
    juce::String    title;
    juce::Colour    colour;
    int             lastBand = -1;
};

// The "Deepwater" look: flat matte chrome over theme:: tokens, with luminous
// arcs and accents reserved for values and active states. Used editor-wide.
class SpectralLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SpectralLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&,
                         bool highlighted, bool down) override;
};

// The MIDI Learn manager: a live list of every current CC assignment with
// per-assignment channel, value range and invert editing, plus a clear button.
// Polls the (lock-free) manager so freshly-learned CCs appear automatically.
class MidiLearnPanel : public juce::Component,
                       private juce::Timer
{
public:
    explicit MidiLearnPanel (FFTDelayAudioProcessor&);

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void rebuild();
    juce::String targetName (int target) const;

    struct Row
    {
        int cc = 0, target = 0;
        juce::Label      name;
        juce::ComboBox   channel;
        juce::Slider     lo, hi;
        juce::TextButton invert { "Inv" }, clear { "X" };
    };

    FFTDelayAudioProcessor&                processorRef;
    juce::Viewport                         viewport;
    juce::Component                        content;
    std::vector<std::unique_ptr<Row>>      rows;
    std::vector<std::pair<int, int>>       signature;   // (cc,target) set last built

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLearnPanel)
};

// The "editor" is the GUI. It lives on the message (UI) thread and holds only a
// reference to the processor. Every parameter is surfaced as a knob / combo /
// toggle, and the three matrices as paintable curves, so ALL controls are
// reachable.
class FFTDelayAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit FFTDelayAudioProcessorEditor (FFTDelayAudioProcessor&);
    ~FFTDelayAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Right-click on any registered control opens its MIDI Learn menu.
    void mouseDown (const juce::MouseEvent&) override;

    // Poll the Input Modulation selectors and rename the IM knobs to the chosen
    // algorithm's control names.
    void timerCallback() override;

    // Per-algorithm control name for IM knob `idx` (0..2); empty -> use default.
    static juce::String imControlName (int algo, int idx);

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label    label;
        std::unique_ptr<APVTS::ComboBoxAttachment> attachment;
    };

    struct Toggle
    {
        juce::TextButton button;
        std::unique_ptr<APVTS::ButtonAttachment> attachment;
    };

    Knob&   addKnob   (const juce::String& paramID, const juce::String& text);
    Choice& addChoice (const juce::String& paramID, const juce::String& text);
    Toggle& addToggle (const juce::String& paramID, const juce::String& text);

    FFTDelayAudioProcessor& processorRef;

    // Declared first so it outlives every child component that references it.
    SpectralLookAndFeel lookAndFeel;

    // Six matrix editors: a left column (channel 0) and right column (channel 1),
    // each Attenuation / Delay / Feedback. Both channels visible at once.
    std::vector<std::unique_ptr<MatrixEditor>> matrixEditors;

    // Actions (not stored parameters): Smooth smears the matrices a little more
    // per click; Reset clears all delay memory at the next audio block.
    juce::TextButton smoothButton { "Smooth" };
    juce::TextButton resetButton  { "Reset Dly" };

    // Stereo copy controls (both channels are always visible now).
    juce::TextButton copyLtoR { "Copy L->R" };
    juce::TextButton copyRtoL { "Copy R->L" };

    void updateLinkMirrors();   // set/clear L<->R edit mirroring from the Link state
    void repaintMatrices();

    // Preset bar.
    juce::ComboBox   presetBox;
    juce::TextButton prevPresetButton { "<" };
    juce::TextButton nextPresetButton { ">" };
    juce::TextButton savePresetButton { "Save" };
    juce::TextButton midiButton       { "MIDI" };   // opens the MIDI Learn manager

    void refreshPresetBox();    // rebuild the list + select the current preset
    void afterPresetChange();   // refresh box selection and repaint everything

    // MIDI Learn: map each registered control to its target, and show its menu.
    void registerLearn (juce::Component& c, int target);
    void registerParamLearn (juce::Component& c, const juce::String& paramID);
    void showMidiMenu (int target);
    std::vector<std::pair<juce::Component*, int>> learnTargets;

    std::vector<std::unique_ptr<Knob>>   knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<Toggle>> toggles;

    // IM knob labels to relabel dynamically: [0..2] = L set, [3..5] = R set.
    std::array<Knob*, 6> imKnobs { {} };
    std::array<int, 2>   lastImAlgo { -1, -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTDelayAudioProcessorEditor)
};
