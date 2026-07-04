#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>
#include "PluginProcessor.h"
#include "SpectralCurves.h"

// A mouse-paintable spectral matrix. Frequency runs bottom->top, the painted
// value runs left->right as a striped silhouette with a luminous edge. The
// Attenuation matrix displays inverted (paint rightward to CUT, so a fresh
// unity curve reads as empty). Delay editors overlay tempo gridlines when
// synced. Drag to draw; double-click resets the curve. SHIFT-drag paints the
// red band selection (the "Sel" modulation target; starting on a selected
// band erases instead); shift-double-click clears the selection.
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
    void timerCallback() override { repaint(); }   // slow tick: grid/BPM changes
    void paintStroke (juce::Point<float> pos);
    void selectStroke (juce::Point<float> pos);
    int  bandAt (float y) const noexcept;
    void drawTempoGrid (juce::Graphics&, juce::Rectangle<float>) const;

    // Stored curve value <-> displayed position (Attenuation shows 1 - value).
    float toDisplay (float v) const noexcept { return inverted ? 1.0f - v : v; }
    float toStored  (float d) const noexcept { return inverted ? 1.0f - d : d; }

    FFTDelayAudioProcessor& processorRef;
    SpectralCurves* curves;
    SpectralCurves* mirror = nullptr;
    int             matrix;
    bool            inverted;
    juce::String    title;
    juce::Colour    colour;
    int             lastBand = -1;

    // Selection-stroke state: set on shift-mouse-down, held for the drag.
    bool selecting = false, selectMode = true;
    int  lastSelBand = -1;
};

// A scrolling spectral history fed from the processor's lock-free spectrum
// taps: the newest frame enters at the left edge and drifts right, at 30 fps.
// The top half shows the LEFT channel, the bottom half the RIGHT; low
// frequencies sit at the bottom of each half.
class Sonogram : public juce::Component,
                 private juce::Timer
{
public:
    Sonogram (FFTDelayAudioProcessor&, bool showOutput, juce::String caption);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    FFTDelayAudioProcessor& processorRef;
    bool         useOutput;
    juce::String caption;
    juce::Image  image;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Sonogram)
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

    // Controls are owned by the vectors below; these let resized() place each
    // one by parameter id inside its section card.
    std::map<juce::String, Knob*>   knobById;
    std::map<juce::String, Choice*> choiceById;
    std::map<juce::String, Toggle*> toggleById;

    // Section cards (GLOBAL / EDIT / LFO / ANALYSIS across the top, INPUT MOD
    // along the bottom): bounds captured in resized(), chrome drawn in paint().
    static constexpr int numCards = 5;
    std::array<juce::Rectangle<int>, numCards> cardBounds;

    juce::Label bpmLabel;   // live host tempo readout (toolbar)

    FFTDelayAudioProcessor& processorRef;

    // Declared first so it outlives every child component that references it.
    SpectralLookAndFeel lookAndFeel;

    // Free resizing: every control lives on this fixed-size logical canvas,
    // which resized() scales as a whole - crisp vectors at any window size.
    // The shadowing helper below reroutes the constructor's addAndMakeVisible
    // calls onto the canvas, so child components never deal with scaling.
    static constexpr int logicalW = 1200, logicalH = 960;
    juce::Component content;
    void addAndMakeVisible (juce::Component& c) { content.addAndMakeVisible (c); }
    void layoutContent();   // lays out the logical canvas (called from resized)

    // Six matrix editors: a left column (channel 0) and right column (channel 1),
    // each Attenuation / Delay / Feedback. Both channels visible at once.
    std::vector<std::unique_ptr<MatrixEditor>> matrixEditors;

    // Scrolling spectral histories flanking the matrix field.
    Sonogram sonoIn  { processorRef, false, "IN" };
    Sonogram sonoOut { processorRef, true,  "OUT" };

    // The EDIT card's operation surface (all actions / UI state, not params).
    // Ops apply to every enabled target (channel x matrix toggle); GO writes the
    // selected generator shape scaled by AMP with PER cycles across the grid;
    // UNDO restores the single-level snapshot taken before the last op.
    std::array<juce::TextButton, 6> opTargets;   // AL DL FL / AR DR FR
    std::array<juce::TextButton, 6> genShapes;   // SIN TRI SQR SW- SW+ RND (radio)
    juce::Slider     ampBox, perBox;
    juce::TextButton qntButton  { "QNT" },  avgButton  { "AVG" },
                     flpXButton { "FLPX" }, flpYButton { "FLPY" },
                     smoothButton { "SMTH" }, goButton { "GO" },
                     undoButton { "UNDO" },   resetButton { "RST DLY" };

    struct UndoState
    {
        std::array<std::array<std::array<float, SpectralCurves::numEditBands>,
                              SpectralCurves::numMatrices>, 2> v {};
        bool valid = false;
    } undoState;

    void snapshotForUndo();
    void restoreUndo();
    void forEachOpTarget (const std::function<void (int channel, int matrix)>&);

    // The graph-control strip sits between the L (top) and R (bottom) matrix
    // rows: one cluster per matrix column with its stage Bypass [B], stereo
    // Link [L], copy L->R [v] / R->L [^], and the column's CAPS caption.
    struct MatrixCluster
    {
        juce::Label      caption;
        juce::TextButton bypass { "B" }, link { "L" }, copyDown { "v" }, copyUp { "^" };
        std::unique_ptr<APVTS::ButtonAttachment> bypassAttachment, linkAttachment;
    };
    std::array<MatrixCluster, SpectralCurves::numMatrices> clusters;

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
