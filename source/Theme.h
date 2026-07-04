#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// "Deepwater" design tokens: the single source for every colour, font and
// metric in the UI. Components read from here and never hard-code values, so
// the whole look can be retuned (or a light theme added) in one place.
//
// The formula: flat matte chrome on a near-black, cool teal-tinted field, with
// luminous colour reserved for data displays, value arcs and active states.
// Backgrounds form a three-level ladder (canvas < app < panel); accents stay in
// one cyan-teal family so the surface reads as a unit; red appears only for
// selection, store and reset states.
namespace theme
{
// --- palette -----------------------------------------------------------------
inline constexpr juce::uint32 bgApp      = 0xff12181a;   // window field
inline constexpr juce::uint32 bgPanel    = 0xff1c2427;   // section cards
inline constexpr juce::uint32 bgHeader   = 0xff222b2f;   // card header strips
inline constexpr juce::uint32 bgCanvas   = 0xff06141a;   // matrix / sonogram wells
inline constexpr juce::uint32 bgInset    = 0xff0b1e26;   // value boxes, dropdowns
inline constexpr juce::uint32 bgMenu     = 0xff161d20;   // popup menus

inline constexpr juce::uint32 textPrimary   = 0xffe6edef;
inline constexpr juce::uint32 textSecondary = 0xff8fa6ac;

inline constexpr juce::uint32 accentAtt = 0xff2ec4b6;    // attenuation (green-teal)
inline constexpr juce::uint32 accentDly = 0xff00c9fa;    // delay — hero accent
inline constexpr juce::uint32 accentFb  = 0xff3e8ef7;    // feedback (blue)
inline constexpr juce::uint32 accentMod = 0xffb18cff;    // LFO / modulation
inline constexpr juce::uint32 accentRed = 0xffff4b4e;    // selection / store / reset
inline constexpr juce::uint32 stateOff  = 0xff3a4a4f;    // inactive outlines

inline juce::Colour colour (juce::uint32 argb)  { return juce::Colour (argb); }

inline juce::Colour hairline()   { return juce::Colours::white.withAlpha (0.08f); }
inline juce::Colour gridline()   { return juce::Colours::white.withAlpha (0.06f); }
inline juce::Colour watermark()  { return juce::Colours::white.withAlpha (0.07f); }

// Accent for a SpectralCurves::Matrix index (Attenuation, Delay, Feedback).
inline juce::Colour accentFor (int matrix)
{
    return colour (matrix == 0 ? accentAtt : matrix == 1 ? accentDly : accentFb);
}

// --- typography ----------------------------------------------------------------
// System sans for now; sized so a bundled face can slot in behind these helpers
// without touching call sites.
inline juce::Font microLabel()  { return juce::Font (juce::FontOptions (9.0f)); }
inline juce::Font label()       { return juce::Font (juce::FontOptions (10.0f)); }
inline juce::Font value()       { return juce::Font (juce::FontOptions (12.0f, juce::Font::bold)); }
inline juce::Font header()      { return juce::Font (juce::FontOptions (14.0f, juce::Font::bold)); }
inline juce::Font heroReadout() { return juce::Font (juce::FontOptions (18.0f, juce::Font::bold)); }

// --- metrics -------------------------------------------------------------------
inline constexpr float cornerRadius = 3.0f;   // buttons, value boxes
inline constexpr float cardRadius   = 6.0f;   // section cards
inline constexpr float arcThickness = 2.5f;   // knob track / value arcs
}
