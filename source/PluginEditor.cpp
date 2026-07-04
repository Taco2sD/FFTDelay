#include "PluginEditor.h"
#include "Theme.h"
#include <cmath>

//==============================================================================
// SpectralLookAndFeel — every colour and font comes from theme:: tokens.
//==============================================================================
SpectralLookAndFeel::SpectralLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::colour (theme::bgApp));

    setColour (juce::Slider::rotarySliderFillColourId, theme::colour (theme::accentDly));
    setColour (juce::Slider::textBoxTextColourId,      theme::colour (theme::textPrimary));
    setColour (juce::Slider::textBoxOutlineColourId,   juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId,              theme::colour (theme::textSecondary));

    setColour (juce::ComboBox::backgroundColourId, theme::colour (theme::bgInset));
    setColour (juce::ComboBox::textColourId,       theme::colour (theme::textPrimary));
    setColour (juce::ComboBox::outlineColourId,    theme::hairline());
    setColour (juce::ComboBox::arrowColourId,      theme::colour (theme::accentDly));

    setColour (juce::PopupMenu::backgroundColourId, theme::colour (theme::bgMenu));
    setColour (juce::PopupMenu::textColourId,       theme::colour (theme::textPrimary));
    setColour (juce::PopupMenu::highlightedBackgroundColourId,
               theme::colour (theme::accentDly).withAlpha (0.18f));
    setColour (juce::PopupMenu::highlightedTextColourId, theme::colour (theme::textPrimary));

    setColour (juce::TextButton::buttonColourId,   theme::colour (theme::bgPanel));
    setColour (juce::TextButton::buttonOnColourId, theme::colour (theme::accentDly));
    setColour (juce::TextButton::textColourOffId,  theme::colour (theme::textSecondary));
    setColour (juce::TextButton::textColourOnId,   theme::colour (theme::bgCanvas));
}

void SpectralLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float pos, float a0, float a1, juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const float cx = bounds.getCentreX(), cy = bounds.getCentreY();
    const float lineW = juce::jmax (theme::arcThickness, radius * 0.1f);
    const float arcR  = radius - lineW * 0.5f;
    const float ang   = a0 + pos * (a1 - a0);

    // Track arc, then the luminous value arc in this knob's accent.
    auto stroke = juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                        juce::PathStrokeType::rounded);
    juce::Path track, valArc;
    track.addCentredArc  (cx, cy, arcR, arcR, 0.0f, a0, a1, true);
    valArc.addCentredArc (cx, cy, arcR, arcR, 0.0f, a0, ang, true);
    g.setColour (juce::Colours::white.withAlpha (0.15f));
    g.strokePath (track, stroke);
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.strokePath (valArc, stroke);

    // Flat body disc + hairline pointer.
    const float innerR = arcR - lineW * 1.6f;
    g.setColour (theme::colour (theme::bgPanel).brighter (0.06f));
    g.fillEllipse (cx - innerR, cy - innerR, innerR * 2.0f, innerR * 2.0f);
    const juce::Point<float> tip (cx + innerR * std::sin (ang), cy - innerR * std::cos (ang));
    g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
    g.drawLine (cx, cy, tip.x, tip.y, juce::jmax (1.5f, lineW * 0.6f));
}

void SpectralLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                                const juce::Colour&, bool highlighted, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState();

    if (on)
    {
        auto fill = b.findColour (juce::TextButton::buttonOnColourId);
        g.setColour (down ? fill.darker (0.2f) : highlighted ? fill.brighter (0.1f) : fill);
        g.fillRoundedRectangle (r, theme::cornerRadius);
    }
    else
    {
        if (highlighted || down)
        {
            g.setColour (juce::Colours::white.withAlpha (down ? 0.08f : 0.05f));
            g.fillRoundedRectangle (r, theme::cornerRadius);
        }
        g.setColour (theme::colour (theme::stateOff));
        g.drawRoundedRectangle (r, theme::cornerRadius, 1.0f);
    }
}

void SpectralLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (theme::label());
    g.setColour (b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId
                                                  : juce::TextButton::textColourOffId));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (2), juce::Justification::centred, 1);
}

//==============================================================================
// MidiLearnPanel — live list of MIDI assignments with range/channel/invert.
//==============================================================================
MidiLearnPanel::MidiLearnPanel (FFTDelayAudioProcessor& p) : processorRef (p)
{
    addAndMakeVisible (viewport);
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    setSize (560, 360);
    rebuild();
    startTimerHz (4);
}

juce::String MidiLearnPanel::targetName (int target) const
{
    if (target == MidiLearnManager::presetNext) return "Preset Next";
    if (target == MidiLearnManager::presetPrev) return "Preset Previous";

    const auto& params = processorRef.getParameters();
    if (juce::isPositiveAndBelow (target, params.size()))
        return params[target]->getName (40);
    return "Target " + juce::String (target);
}

void MidiLearnPanel::timerCallback() { rebuild(); }

void MidiLearnPanel::rebuild()
{
    auto& ml = processorRef.midiLearn;

    // Current (cc,target) set; bail if unchanged so live edits aren't disturbed.
    std::vector<std::pair<int, int>> sig;
    for (int cc = 0; cc < MidiLearnManager::numCC; ++cc)
        if (const int t = ml.target[(size_t) cc].load(); t != MidiLearnManager::noTarget)
            sig.emplace_back (cc, t);

    if (sig == signature)
        return;
    signature = sig;

    rows.clear();
    content.removeAllChildren();

    const int rowH = 26;
    const int w    = juce::jmax (520, getWidth() - 20);

    for (int i = 0; i < (int) sig.size(); ++i)
    {
        const int cc = sig[(size_t) i].first;
        auto row = std::make_unique<Row>();
        row->cc = cc;
        row->target = sig[(size_t) i].second;

        row->name.setText (targetName (row->target) + "  (CC " + juce::String (cc) + ")",
                           juce::dontSendNotification);
        content.addAndMakeVisible (row->name);

        row->channel.addItem ("Omni", 1);
        for (int ch = 1; ch <= 16; ++ch) row->channel.addItem (juce::String (ch), ch + 1);
        row->channel.setSelectedId (ml.channel[(size_t) cc].load() + 1, juce::dontSendNotification);
        auto* chanBox = &row->channel;
        row->channel.onChange = [&ml, cc, chanBox] { ml.channel[(size_t) cc].store (chanBox->getSelectedId() - 1); };
        content.addAndMakeVisible (row->channel);

        auto setUpRange = [&] (juce::Slider& s, std::atomic<float>& dest)
        {
            s.setSliderStyle (juce::Slider::LinearBar);
            s.setRange (0.0, 1.0, 0.001);
            s.setValue (dest.load(), juce::dontSendNotification);
            s.onValueChange = [&dest, &s] { dest.store ((float) s.getValue()); };
            content.addAndMakeVisible (s);
        };
        setUpRange (row->lo, ml.rangeMin[(size_t) cc]);
        setUpRange (row->hi, ml.rangeMax[(size_t) cc]);

        row->invert.setClickingTogglesState (true);
        row->invert.setToggleState (ml.invert[(size_t) cc].load(), juce::dontSendNotification);
        auto* invBtn = &row->invert;
        row->invert.onClick = [&ml, cc, invBtn] { ml.invert[(size_t) cc].store (invBtn->getToggleState()); };
        content.addAndMakeVisible (row->invert);

        row->clear.onClick = [this, &ml, cc] { ml.clearTarget (cc); rebuild(); };
        content.addAndMakeVisible (row->clear);

        const int y = i * rowH;
        row->name.setBounds    (6,   y + 2, 170, rowH - 4);
        row->channel.setBounds (180, y + 2, 64,  rowH - 4);
        row->lo.setBounds      (250, y + 2, 96,  rowH - 4);
        row->hi.setBounds      (350, y + 2, 96,  rowH - 4);
        row->invert.setBounds  (452, y + 2, 40,  rowH - 4);
        row->clear.setBounds   (496, y + 2, 24,  rowH - 4);

        rows.push_back (std::move (row));
    }

    content.setSize (w, juce::jmax (1, (int) sig.size()) * rowH);
}

void MidiLearnPanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (24);          // header
    viewport.setBounds (r);
    content.setSize (juce::jmax (520, r.getWidth() - 20), content.getHeight());
}

void MidiLearnPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour (theme::bgPanel));
    g.setColour (theme::colour (theme::textPrimary));
    g.setFont (theme::header());

    if (signature.empty())
        g.drawText ("No MIDI assignments yet - right-click any control and choose MIDI Learn.",
                    getLocalBounds().reduced (10), juce::Justification::centred);
    else
        g.drawText ("MIDI Learn  (target  -  channel  -  min  -  max  -  invert)",
                    getLocalBounds().removeFromTop (24).reduced (8, 0), juce::Justification::centredLeft);
}

//==============================================================================
// MatrixEditor — a paintable spectral curve.
//==============================================================================
MatrixEditor::MatrixEditor (FFTDelayAudioProcessor& proc, SpectralCurves& curvesToEdit,
                            int matrixIndex, juce::String t, juce::Colour c)
    : processorRef (proc), curves (&curvesToEdit), matrix (matrixIndex),
      inverted (matrixIndex == SpectralCurves::Attenuation),
      title (std::move (t)), colour (c)
{
    startTimerHz (4);   // catch tempo-grid / BPM changes without edit events
}

void MatrixEditor::drawTempoGrid (juce::Graphics& g, juce::Rectangle<float> r) const
{
    const auto& apvts = processorRef.apvts;
    if (apvts.getRawParameterValue ("delaySync")->load() < 0.5f)
        return;

    static constexpr float rangeSeconds[] = { 0.375f, 0.75f, 1.5f, 3.0f, 6.0f, 12.0f };
    static constexpr float gridFactor[]   = { 1.0f, 0.5f, 1.0f / 3.0f, 0.25f };

    const float maxDelay = rangeSeconds[juce::jlimit (0, 5,
                               (int) apvts.getRawParameterValue ("delayRange")->load())];
    const float bpm      = apvts.getRawParameterValue ("autoQuantize")->load() > 0.5f
                               ? processorRef.displayBpm.load (std::memory_order_relaxed) : 120.0f;
    const float gridSec  = (60.0f / bpm) * gridFactor[juce::jlimit (0, 3,
                               (int) apvts.getRawParameterValue ("delayGrid")->load())];

    g.setColour (colour.withAlpha (0.2f));
    for (float sec = gridSec; sec < maxDelay; sec += gridSec)
        g.drawVerticalLine ((int) (r.getX() + r.getWidth() * sec / maxDelay),
                            r.getY(), r.getBottom());
}

void MatrixEditor::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (theme::colour (theme::bgCanvas));
    g.fillRoundedRectangle (r, 4.0f);

    // Quarter guides on both axes (frequency vertical, value horizontal).
    g.setColour (theme::gridline());
    for (int i = 1; i < 4; ++i)
    {
        g.drawHorizontalLine ((int) (r.getY() + r.getHeight() * (float) i / 4.0f), r.getX(), r.getRight());
        g.drawVerticalLine   ((int) (r.getX() + r.getWidth()  * (float) i / 4.0f), r.getY(), r.getBottom());
    }

    if (matrix == SpectralCurves::Delay)
        drawTempoGrid (g, r);

    // The band profile: frequency runs bottom -> top, each band's value extends
    // rightward from the left edge. Filled flat, then the value edge is stroked
    // twice - a wide low-alpha pass for glow and a crisp line on top.
    const int n = SpectralCurves::numEditBands;
    juce::Path edge;
    for (int b = 0; b < n; ++b)
    {
        const float x = r.getX() + r.getWidth() * toDisplay (curves->get (matrix, b));
        const float y = r.getBottom() - r.getHeight() * ((float) b + 0.5f) / (float) n;
        if (b == 0) edge.startNewSubPath (x, y);
        else        edge.lineTo (x, y);
    }

    juce::Path fill (edge);
    fill.lineTo (r.getX(), r.getY());
    fill.lineTo (r.getX(), r.getBottom());
    fill.closeSubPath();

    g.setColour (colour.withAlpha (0.28f));
    g.fillPath (fill);
    g.setColour (colour.withAlpha (0.25f));
    g.strokePath (edge, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved));
    g.setColour (colour);
    g.strokePath (edge, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));

    g.setColour (theme::colour (theme::textSecondary));
    g.setFont (theme::label());
    g.drawText (title.toUpperCase(), r.reduced (6.0f), juce::Justification::topLeft);
}

void MatrixEditor::mouseDown (const juce::MouseEvent& e)
{
    lastBand = -1;
    paintStroke (e.position);
}

void MatrixEditor::mouseDrag (const juce::MouseEvent& e)
{
    paintStroke (e.position);
}

void MatrixEditor::mouseDoubleClick (const juce::MouseEvent&)
{
    curves->fill (matrix, SpectralCurves::defaultFor (matrix));
    if (mirror != nullptr) mirror->fill (matrix, SpectralCurves::defaultFor (matrix));
    repaint();
}

void MatrixEditor::paintStroke (juce::Point<float> pos)
{
    const int   n = SpectralCurves::numEditBands;
    const float w = (float) getWidth();
    const float h = (float) getHeight();
    if (w <= 0.0f || h <= 0.0f)
        return;

    // Rotated axes: vertical position picks the band (bottom = band 0), the
    // horizontal position is the value (via the display inversion, if any).
    const int   band  = juce::jlimit (0, n - 1, (int) std::round ((1.0f - pos.y / h) * (float) (n - 1)));
    const float value = toStored (juce::jlimit (0.0f, 1.0f, pos.x / w));

    // Interpolate across any bands skipped since the last mouse position so a fast
    // drag still paints a continuous line.
    auto writeBand = [this] (int b, float v)
    {
        curves->set (matrix, b, v);
        if (mirror != nullptr) mirror->set (matrix, b, v);   // keep L/R identical when linked
    };

    if (lastBand >= 0 && std::abs (band - lastBand) > 1)
    {
        const int   step    = band > lastBand ? 1 : -1;
        const float lastVal = curves->get (matrix, lastBand);
        for (int b = lastBand; b != band; b += step)
        {
            const float t = (float) (b - lastBand) / (float) (band - lastBand);
            writeBand (b, lastVal + t * (value - lastVal));
        }
    }

    writeBand (band, value);
    lastBand = band;
    repaint();
}

//==============================================================================
// FFTDelayAudioProcessorEditor
//==============================================================================
FFTDelayAudioProcessorEditor::FFTDelayAudioProcessorEditor (FFTDelayAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    // Six matrix editors: left column = channel 0 (L), right column = channel 1 (R),
    // each tinted with its matrix's accent from the theme.
    static const char* matrixNames[] = { "Attenuation", "Delay", "Feedback" };
    for (int ch = 0; ch < 2; ++ch)
        for (int m = 0; m < SpectralCurves::numMatrices; ++m)
        {
            auto title = juce::String (matrixNames[m]) + (ch == 0 ? " L" : " R");
            auto e = std::make_unique<MatrixEditor> (p, p.curves[(size_t) ch], m,
                                                     title, theme::accentFor (m));
            addAndMakeVisible (*e);
            matrixEditors.push_back (std::move (e));
        }

    addKnob ("dryWet",     "Dry/Wet");
    addKnob ("effectGain", "Effect Gain");
    addKnob ("attOffX",    "Att Offs");
    addKnob ("delayOffX",  "Dly Offs");
    addKnob ("fbOffX",     "FB Offs");

    addKnob ("lfoRate",   "LFO Rate");
    addKnob ("lfoDepth1", "LFO Dpt 1");
    addKnob ("lfoDepth2", "LFO Dpt 2");
    addKnob ("lfoDepth3", "LFO Dpt 3");
    imKnobs[0] = &addKnob ("imCtrl1",  "IM 1");
    imKnobs[1] = &addKnob ("imCtrl2",  "IM 2");
    imKnobs[2] = &addKnob ("imCtrl3",  "IM 3");
    imKnobs[3] = &addKnob ("imCtrl1R", "IM R1");
    imKnobs[4] = &addKnob ("imCtrl2R", "IM R2");
    imKnobs[5] = &addKnob ("imCtrl3R", "IM R3");

    addChoice ("delayRange", "Delay Range");
    addChoice ("delayGrid",  "Grid");
    addChoice ("fftSize",    "Bands");
    addChoice ("overlap",    "Overlap");
    addChoice ("window",     "Window");
    addChoice ("editMask",   "Edit Mask");
    addChoice ("inputMod",   "Input Mod");
    addChoice ("inputModR",  "Input Mod R");
    addChoice ("lfoWave",    "LFO Wave");
    addChoice ("lfoSyncRate","LFO Div");
    addChoice ("lfoDest1",   "LFO Dest 1");
    addChoice ("lfoDest2",   "LFO Dest 2");
    addChoice ("lfoDest3",   "LFO Dest 3");

    addToggle ("freeze",       "Freeze");
    addToggle ("mute",         "Mute");
    addToggle ("bypass",       "Bypass");
    addToggle ("delaySync",    "Sync");
    addToggle ("autoQuantize", "Auto Quant");
    addToggle ("lfoOn",        "LFO On");
    addToggle ("lfoSync",      "LFO Sync");
    addToggle ("imLink",       "IM Link");
    addToggle ("imBypass",     "IM Byp");
    addToggle ("zeroDelayOnProgram", "0Dly Prg");
    addToggle ("ignoreMidiReset",    "No M-Rst");

    // Graph-control strip clusters: per-matrix Bypass/Link (parameters) and the
    // per-column stereo copies (actions), beside the column's CAPS caption.
    static const char* stripCaptions[] = { "FILTER", "FREQUENCY", "FEEDBACK" };
    static const char* stripParamIds[] = { "att", "delay", "fb" };
    for (int m = 0; m < SpectralCurves::numMatrices; ++m)
    {
        auto& c = clusters[(size_t) m];
        const juce::String id (stripParamIds[m]);

        c.caption.setText (stripCaptions[m], juce::dontSendNotification);
        c.caption.setColour (juce::Label::textColourId, theme::accentFor (m));
        c.caption.setFont (theme::label());
        addAndMakeVisible (c.caption);

        for (auto* b : { &c.bypass, &c.link })
            b->setClickingTogglesState (true);
        c.bypassAttachment = std::make_unique<APVTS::ButtonAttachment> (processorRef.apvts, id + "Bypass", c.bypass);
        c.linkAttachment   = std::make_unique<APVTS::ButtonAttachment> (processorRef.apvts, id + "Link",   c.link);
        registerParamLearn (c.bypass, id + "Bypass");
        registerParamLearn (c.link,   id + "Link");
        c.link.onClick = [this] { updateLinkMirrors(); };

        c.copyDown.onClick = [this, m]
        {
            processorRef.curves[1].copyMatrixFrom (processorRef.curves[0], m);
            repaintMatrices();
        };
        c.copyUp.onClick = [this, m]
        {
            processorRef.curves[0].copyMatrixFrom (processorRef.curves[1], m);
            repaintMatrices();
        };

        for (auto* b : { &c.bypass, &c.link, &c.copyDown, &c.copyUp })
            addAndMakeVisible (*b);
    }

    // Smooth smears both channels' matrices a little more per click.
    smoothButton.onClick = [this]
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int m = 0; m < SpectralCurves::numMatrices; ++m)
                processorRef.curves[(size_t) ch].smooth (m);
        repaintMatrices();
    };
    addAndMakeVisible (smoothButton);

    resetButton.onClick = [this] { processorRef.requestEngineReset(); };
    addAndMakeVisible (resetButton);

    bpmLabel.setJustificationType (juce::Justification::centredRight);
    bpmLabel.setColour (juce::Label::textColourId, theme::colour (theme::accentDly));
    bpmLabel.setFont (theme::value());
    addAndMakeVisible (bpmLabel);

    // Preset bar.
    presetBox.setTextWhenNothingSelected ("Init");
    presetBox.onChange = [this]
    {
        const int sel = presetBox.getSelectedId() - 1;
        if (sel >= 0)
        {
            processorRef.presetManager.loadIndex (sel);
            afterPresetChange();
        }
    };
    addAndMakeVisible (presetBox);

    prevPresetButton.onClick = [this] { processorRef.presetManager.previous(); afterPresetChange(); };
    nextPresetButton.onClick = [this] { processorRef.presetManager.next();     afterPresetChange(); };
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (nextPresetButton);

    // Preset Next/Prev are MIDI-learnable actions too.
    registerLearn (nextPresetButton, MidiLearnManager::presetNext);
    registerLearn (prevPresetButton, MidiLearnManager::presetPrev);

    // Refresh the UI after a MIDI-triggered preset change (runs on message thread).
    processorRef.onPresetChangedFromMidi = [this] { afterPresetChange(); };

    savePresetButton.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("Save Preset", "Name:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", processorRef.presetManager.getCurrentName());
        w->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result)
        {
            if (result == 1)
            {
                processorRef.presetManager.savePreset (w->getTextEditorContents ("name").trim());
                refreshPresetBox();
            }
            delete w;
        }));
    };
    addAndMakeVisible (savePresetButton);

    // MIDI Learn manager window.
    midiButton.onClick = [this]
    {
        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned (new MidiLearnPanel (processorRef));
        o.content->setSize (560, 360);
        o.dialogTitle             = "MIDI Learn";
        o.dialogBackgroundColour  = theme::colour (theme::bgPanel);
        o.escapeKeyTriggersCloseButton = true;
        o.useNativeTitleBar       = true;
        o.resizable               = true;
        o.launchAsync();
    };
    addAndMakeVisible (midiButton);

    refreshPresetBox();
    updateLinkMirrors();
    startTimerHz (6);     // keep the IM knob labels in sync with the selected algorithm
    setSize (1200, 960);
}

FFTDelayAudioProcessorEditor::~FFTDelayAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
    processorRef.onPresetChangedFromMidi = nullptr;
}

void FFTDelayAudioProcessorEditor::registerLearn (juce::Component& c, int target)
{
    c.addMouseListener (this, false);
    learnTargets.push_back ({ &c, target });
}

void FFTDelayAudioProcessorEditor::registerParamLearn (juce::Component& c, const juce::String& paramID)
{
    if (auto* p = processorRef.apvts.getParameter (paramID))
        registerLearn (c, p->getParameterIndex());
}

void FFTDelayAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;

    // Find which registered control (or an ancestor of the clicked component) was hit.
    for (auto* comp = e.eventComponent; comp != nullptr; comp = comp->getParentComponent())
        for (const auto& entry : learnTargets)
            if (entry.first == comp)
            {
                showMidiMenu (entry.second);
                return;
            }
}

void FFTDelayAudioProcessorEditor::showMidiMenu (int target)
{
    auto& ml = processorRef.midiLearn;
    const int  existingCC = ml.ccForTarget (target);
    const bool armed      = ml.getArmed() == target;

    juce::PopupMenu m;
    m.addItem (1, armed ? "Waiting for CC...  (cancel)" : "MIDI Learn");
    m.addItem (2, "MIDI Clear", existingCC >= 0);
    m.addItem (3, "Invert", existingCC >= 0, ml.getInvert (target));
    if (existingCC >= 0)
    {
        m.addSeparator();
        m.addSectionHeader ("Assigned: CC " + juce::String (existingCC));
    }

    m.showMenuAsync (juce::PopupMenu::Options{}, [target, &ml] (int r)
    {
        if (r == 1)       { if (ml.getArmed() == target) ml.disarm(); else ml.arm (target); }
        else if (r == 2)  ml.clearTarget (target);
        else if (r == 3)  ml.setInvert (target, ! ml.getInvert (target));
    });
}

void FFTDelayAudioProcessorEditor::repaintMatrices()
{
    for (auto& e : matrixEditors)
        e->repaint();
}

void FFTDelayAudioProcessorEditor::updateLinkMirrors()
{
    // Each matrix has its own L/R link. When linked, an edit in either column
    // mirrors to the other so L and R stay identical (and the engine drives both
    // from the L set). matrixEditors index: 0..2 = L (Att/Dly/FB), 3..5 = R;
    // editor i edits matrix (i % 3).
    static const char* linkIds[] = { "attLink", "delayLink", "fbLink" };
    for (int i = 0; i < (int) matrixEditors.size(); ++i)
    {
        const int  m      = i % 3;
        const bool linked = processorRef.apvts.getRawParameterValue (linkIds[m])->load() > 0.5f;
        const int otherChannel = i < 3 ? 1 : 0;   // L mirrors to R-set, R mirrors to L-set
        matrixEditors[(size_t) i]->setMirror (linked ? &processorRef.curves[(size_t) otherChannel]
                                                     : nullptr);

        // Copies are no-ops while that column is linked (the sets are identical).
        clusters[(size_t) m].copyDown.setEnabled (! linked);
        clusters[(size_t) m].copyUp.setEnabled (! linked);
    }
    repaintMatrices();
}

void FFTDelayAudioProcessorEditor::refreshPresetBox()
{
    processorRef.presetManager.refresh();
    presetBox.clear (juce::dontSendNotification);

    const auto& names = processorRef.presetManager.getPresets();
    for (int i = 0; i < names.size(); ++i)
        presetBox.addItem (names[i], i + 1);

    presetBox.setSelectedId (processorRef.presetManager.getCurrentIndex() + 1,
                             juce::dontSendNotification);
}

void FFTDelayAudioProcessorEditor::afterPresetChange()
{
    // Reflect the new selection, then repaint the painted matrices (parameter
    // sliders refresh themselves via their APVTS attachments).
    presetBox.setSelectedId (processorRef.presetManager.getCurrentIndex() + 1,
                             juce::dontSendNotification);
    updateLinkMirrors();   // link state may have changed; also repaints matrices
}

juce::String FFTDelayAudioProcessorEditor::imControlName (int algo, int idx)
{
    static const char* names[12][3] = {
        { "", "", "" },                              // None
        { "Depth", "Freq", "Duration" },             // Deterioration
        { "Threshold", "Rev Gain", "" },             // Jello Mold
        { "Shift", "", "" },                         // PitchRoll
        { "Depth", "", "" },                         // Smear
        { "Azimuth", "", "" },                       // TimeSponge
        { "Depth", "Speed", "Function" },            // Foam
        { "Twist", "", "" },                         // Lime Twist
        { "Depth", "Spread", "Offset" },             // Horse Tail
        { "Gain", "Rate", "Feedback" },              // Mini-Pulses
        { "Depth", "Threshold", "Invert" },          // Imprint
        { "Phase", "", "" },                         // Phase Blaster
    };
    if (juce::isPositiveAndBelow (algo, 12) && juce::isPositiveAndBelow (idx, 3))
        return names[algo][idx];
    return {};
}

void FFTDelayAudioProcessorEditor::timerCallback()
{
    bpmLabel.setText (juce::String (processorRef.displayBpm.load (std::memory_order_relaxed), 1)
                          + " BPM",
                      juce::dontSendNotification);

    for (int set = 0; set < 2; ++set)
    {
        const auto* id = set == 0 ? "inputMod" : "inputModR";
        const int algo = (int) processorRef.apvts.getRawParameterValue (id)->load();
        if (algo == lastImAlgo[(size_t) set])
            continue;
        lastImAlgo[(size_t) set] = algo;

        for (int idx = 0; idx < 3; ++idx)
        {
            if (auto* k = imKnobs[(size_t) (set * 3 + idx)])
            {
                const auto name = imControlName (algo, idx);
                const juce::String fallback = (set == 0 ? "IM " : "IM R") + juce::String (idx + 1);
                k->label.setText (name.isNotEmpty() ? name : fallback, juce::dontSendNotification);
            }
        }
    }
}

FFTDelayAudioProcessorEditor::Knob&
FFTDelayAudioProcessorEditor::addKnob (const juce::String& paramID, const juce::String& text)
{
    auto k = std::make_unique<Knob>();
    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    addAndMakeVisible (k->slider);

    k->label.setText (text, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (k->label);

    k->attachment = std::make_unique<APVTS::SliderAttachment> (processorRef.apvts, paramID, k->slider);
    registerParamLearn (k->slider, paramID);

    knobs.push_back (std::move (k));
    knobById[paramID] = knobs.back().get();
    return *knobs.back();
}

FFTDelayAudioProcessorEditor::Choice&
FFTDelayAudioProcessorEditor::addChoice (const juce::String& paramID, const juce::String& text)
{
    auto c = std::make_unique<Choice>();

    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*> (processorRef.apvts.getParameter (paramID)))
        c->box.addItemList (choiceParam->choices, 1);

    addAndMakeVisible (c->box);

    c->label.setText (text, juce::dontSendNotification);
    c->label.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (c->label);

    c->attachment = std::make_unique<APVTS::ComboBoxAttachment> (processorRef.apvts, paramID, c->box);
    registerParamLearn (c->box, paramID);

    choices.push_back (std::move (c));
    choiceById[paramID] = choices.back().get();
    return *choices.back();
}

FFTDelayAudioProcessorEditor::Toggle&
FFTDelayAudioProcessorEditor::addToggle (const juce::String& paramID, const juce::String& text)
{
    auto t = std::make_unique<Toggle>();
    t->button.setButtonText (text);
    t->button.setClickingTogglesState (true);
    addAndMakeVisible (t->button);

    t->attachment = std::make_unique<APVTS::ButtonAttachment> (processorRef.apvts, paramID, t->button);
    registerParamLearn (t->button, paramID);

    toggles.push_back (std::move (t));
    toggleById[paramID] = toggles.back().get();
    return *toggles.back();
}

void FFTDelayAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::colour (theme::bgApp));

    // Section-card chrome: panel body, header strip, hairline, CAPS title.
    static const char* cardTitles[numCards] = { "GLOBAL", "EDIT", "LFO", "ANALYSIS", "INPUT MOD" };
    for (int i = 0; i < numCards; ++i)
    {
        auto r = cardBounds[(size_t) i].toFloat();
        if (r.isEmpty())
            continue;
        g.setColour (theme::colour (theme::bgPanel));
        g.fillRoundedRectangle (r, theme::cardRadius);
        g.setColour (theme::colour (theme::bgHeader));
        g.fillRect (r.withHeight (20.0f).reduced (1.0f, 1.0f));
        g.setColour (theme::hairline());
        g.drawRoundedRectangle (r, theme::cardRadius, 1.0f);
        g.setColour (theme::colour (theme::textSecondary));
        g.setFont (theme::label());
        g.drawText (cardTitles[i], r.withHeight (20.0f).reduced (8.0f, 0.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (theme::colour (theme::textPrimary));
    g.setFont (theme::header());
    g.drawText ("FFTDELAY", getLocalBounds().reduced (12).removeFromTop (22),
                juce::Justification::centredLeft);
}

void FFTDelayAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (24);   // title strip

    // Card helpers: each card owns a header strip; controls fill the interior.
    auto interiorOf = [] (juce::Rectangle<int> card) { return card.withTrimmedTop (22).reduced (10, 6); };
    auto knobRow = [this] (juce::Rectangle<int> row, std::initializer_list<const char*> ids)
    {
        const int w = row.getWidth() / (int) ids.size();
        for (auto* id : ids)
            if (auto* k = knobById[id])
            {
                auto cell = row.removeFromLeft (w);
                k->label.setBounds  (cell.removeFromTop (13));
                k->slider.setBounds (cell);
            }
    };
    auto choiceRow = [this] (juce::Rectangle<int> row, const char* id, int labelW)
    {
        if (auto* c = choiceById[id])
        {
            c->label.setBounds (row.removeFromLeft (labelW));
            c->box.setBounds (row);
        }
    };

    // --- Zone A: the four section cards --------------------------------------
    auto zoneA = area.removeFromTop (168);
    constexpr float cardWeights[] = { 1.15f, 1.0f, 1.4f, 0.95f };
    const float cardWeightSum = cardWeights[0] + cardWeights[1] + cardWeights[2] + cardWeights[3];

    for (int i = 0; i < 4; ++i)
    {
        const int w = (int) ((float) area.getWidth() * cardWeights[i] / cardWeightSum);
        cardBounds[(size_t) i] = zoneA.removeFromLeft (w).reduced (3, 0);
    }

    {   // GLOBAL: presets, output knobs, state buttons.
        auto in  = interiorOf (cardBounds[0]);
        auto row = in.removeFromTop (22);
        prevPresetButton.setBounds (row.removeFromLeft (24));
        nextPresetButton.setBounds (row.removeFromLeft (24).translated (2, 0));
        savePresetButton.setBounds (row.removeFromRight (44));
        presetBox.setBounds (row.reduced (6, 0));
        knobRow (in.removeFromTop (72), { "dryWet", "effectGain" });
        auto btns = in.removeFromTop (20);
        const int bw = btns.getWidth() / 4;
        for (auto* b : { &toggleById["mute"]->button, &toggleById["bypass"]->button,
                         &toggleById["freeze"]->button })
            b->setBounds (btns.removeFromLeft (bw).reduced (2, 0));
        midiButton.setBounds (btns.reduced (2, 0));
    }

    {   // EDIT: whole-curve offsets + curve actions.
        auto in = interiorOf (cardBounds[1]);
        knobRow (in.removeFromTop (72), { "attOffX", "delayOffX", "fbOffX" });
        in.removeFromTop (4);
        auto row = in.removeFromTop (20);
        smoothButton.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        resetButton.setBounds (row.reduced (2, 0));
    }

    {   // LFO: on/sync/wave/div, then rate + depths, then destinations.
        auto in  = interiorOf (cardBounds[2]);
        auto row = in.removeFromTop (20);
        toggleById["lfoOn"]->button.setBounds (row.removeFromLeft (36));
        toggleById["lfoSync"]->button.setBounds (row.removeFromLeft (44).translated (3, 0));
        if (auto* c = choiceById["lfoSyncRate"]) c->box.setBounds (row.removeFromRight (56));
        if (auto* c = choiceById["lfoWave"])     c->box.setBounds (row.reduced (6, 0));
        knobRow (in.removeFromTop (66), { "lfoRate", "lfoDepth1", "lfoDepth2", "lfoDepth3" });
        auto dests = in.removeFromTop (20);
        const int dw = dests.getWidth() / 3;
        for (auto* id : { "lfoDest1", "lfoDest2", "lfoDest3" })
            if (auto* c = choiceById[id])
                c->box.setBounds (dests.removeFromLeft (dw).reduced (2, 0));
    }

    {   // ANALYSIS: the four framing selectors + housekeeping options.
        auto in = interiorOf (cardBounds[3]);
        for (auto* id : { "fftSize", "overlap", "window", "editMask" })
        {
            choiceRow (in.removeFromTop (22), id, 52);
            in.removeFromTop (3);
        }
        auto row = in.removeFromTop (20);
        toggleById["zeroDelayOnProgram"]->button.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        toggleById["ignoreMidiReset"]->button.setBounds (row.reduced (2, 0));
    }

    area.removeFromTop (6);

    // --- Zone B: tempo toolbar -------------------------------------------------
    auto toolbar = area.removeFromTop (26);
    choiceRow (toolbar.removeFromLeft (150), "delayRange", 62);
    toolbar.removeFromLeft (8);
    if (auto* c = choiceById["delayGrid"]) c->box.setBounds (toolbar.removeFromLeft (64));
    toolbar.removeFromLeft (8);
    toggleById["delaySync"]->button.setBounds (toolbar.removeFromLeft (46));
    toolbar.removeFromLeft (4);
    toggleById["autoQuantize"]->button.setBounds (toolbar.removeFromLeft (66));
    bpmLabel.setBounds (toolbar.removeFromRight (110));
    area.removeFromTop (6);

    // --- Zone C: the matrix field + the INPUT MOD card --------------------------
    // Three matrix columns (Delay widest), LEFT channel on the top row, RIGHT on
    // the bottom, the graph-control strip between. matrixEditors: 0..2 L, 3..5 R.
    auto imCard = area.removeFromBottom (128);
    area.removeFromBottom (6);
    auto matrixArea = area;

    constexpr float colWeights[] = { 1.6f, 3.6f, 1.6f };
    constexpr int   stripH = 36, colGap = 6;
    const float weightSum = colWeights[0] + colWeights[1] + colWeights[2];

    const int rowH = (matrixArea.getHeight() - stripH) / 2;
    auto topRow    = matrixArea.removeFromTop (rowH);
    auto stripArea = matrixArea.removeFromTop (stripH);
    auto bottomRow = matrixArea;

    for (int m = 0; m < SpectralCurves::numMatrices; ++m)
    {
        const int w = (int) ((float) area.getWidth() * colWeights[m] / weightSum);
        auto top    = topRow.removeFromLeft (w);
        auto strip  = stripArea.removeFromLeft (w);
        auto bottom = bottomRow.removeFromLeft (w);

        matrixEditors[(size_t) m]    ->setBounds (top.reduced (colGap / 2, 3));
        matrixEditors[(size_t) m + 3]->setBounds (bottom.reduced (colGap / 2, 3));

        auto& c = clusters[(size_t) m];
        strip.reduce (colGap / 2 + 4, (stripH - 16) / 2);
        c.caption.setBounds (strip.removeFromLeft (juce::jmax (60, strip.getWidth() - 4 * 20)));
        for (auto* btn : { &c.bypass, &c.link, &c.copyDown, &c.copyUp })
        {
            btn->setBounds (strip.removeFromLeft (16));
            strip.removeFromLeft (4);
        }
    }

    // INPUT MOD card: left set | link/bypass column | right set.
    cardBounds[4] = imCard;
    auto in   = interiorOf (imCard);
    auto mid  = in.withSizeKeepingCentre (64, in.getHeight());
    auto lHalf = in.withRight (mid.getX() - 4);
    auto rHalf = in.withLeft (mid.getRight() + 4);

    for (auto [half, algoId, left] :
         { std::tuple (lHalf, "inputMod", true), std::tuple (rHalf, "inputModR", false) })
    {
        auto h = half;
        if (auto* c = choiceById[algoId])
            c->box.setBounds (h.removeFromTop (20).reduced (2, 0));
        h.removeFromTop (2);
        if (left) knobRow (h, { "imCtrl1",  "imCtrl2",  "imCtrl3"  });
        else      knobRow (h, { "imCtrl1R", "imCtrl2R", "imCtrl3R" });
    }

    toggleById["imLink"]->button.setBounds (mid.removeFromTop (20).reduced (2, 0));
    mid.removeFromTop (4);
    toggleById["imBypass"]->button.setBounds (mid.removeFromTop (20).reduced (2, 0));
}
