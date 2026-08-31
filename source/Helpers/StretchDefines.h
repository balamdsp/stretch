#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#define STRETCH_PANEL_WIDTH   1000
#define STRETCH_PANEL_HEIGHT  700

#define TOP_PANEL_HEIGHT    50

static const juce::String PLUGIN_NAME = "Stretch";

// Taken from CMake's project(VERSION ...) via JUCE's generated plugin defines.
static const juce::String PLUGIN_VERSION = JucePlugin_VersionString;

// ---------------------------------------------------------------------------
// UI zoom. The editor is always sized to STRETCH_PANEL * Zoom::uiScale;
// everything (layout, fonts, dialogs) scales off one factor. Range is
// 100%..300% (no shrink below the design size). Zoom::uiScale is read/written
// on the message thread only.
// ---------------------------------------------------------------------------
namespace Zoom
{
    inline constexpr float Min = 1.0f;
    inline constexpr float Max = 3.0f;
    inline constexpr float BaseW = (float) STRETCH_PANEL_WIDTH;
    inline constexpr float BaseH = (float) STRETCH_PANEL_HEIGHT;

    inline float uiScale = 1.0f;
}

// Scale-aware layout metric. Instantiate with the current zoom; sc() rounds,
// scf() keeps fractions.
struct Metrics
{
    explicit Metrics (float s = Zoom::uiScale) : scale (s) {}

    float scale;
    int   sc (float v) const { return juce::roundToInt (v * scale); }
    int   sc (int v) const { return juce::roundToInt ((float) v * scale); }
    float scf (float v) const { return v * scale; }
};

// Each panel derives the zoom from its own width: clamped so any transient
// host-initiated size never pushes layout outside the permitted range.
inline float scaleFor (const juce::Component& c)
{
    return juce::jlimit (Zoom::Min, Zoom::Max,
                         (float) c.getWidth() / Zoom::BaseW);
}

namespace GUI
{
    namespace Color
    {
        // Green phosphor ramp — keep in visual sync with StretchColors.
        static const juce::Colour Transparent = juce::Colour (0x00000000);

        static const juce::Colour Accent = juce::Colour (0xFF8AFFBE);
        static const juce::Colour AccentDim = juce::Colour (0xFF1E4D33);
        static const juce::Colour Body = juce::Colour (0xFF081711);
        static const juce::Colour Card = juce::Colour (0xFF10241A);
        static const juce::Colour CardDark = juce::Colour (0xFF0A1A12);
        static const juce::Colour Background = juce::Colour (0xFF061410);
        static const juce::Colour BackgroundGradientStart = juce::Colour (0xFF142E20).withAlpha (0.9f);
        static const juce::Colour BackgroundGradientEnd = juce::Colour (0xFF142E20).withAlpha (0.0f);
        static const juce::Colour BackgroundDark = juce::Colour (0xFF0A1A12);
        static const juce::Colour Logo = juce::Colour (0xFF9DFFCC);
        static const juce::Colour BrowserBackground = juce::Colour (0xFF0A1A12);
        static const juce::Colour KeyDown = juce::Colour (0xFFEAFFE4);
    }

    namespace Layout
    {
        // MainMargin doubles as the edge-safety band for CRT curvature when
        // the shader is off (with CRT on, its 5% frame adds clearance).
        // All values scale with the current UI zoom.
        inline float MainMargin()      { return 15.0f * Zoom::uiScale; }
        inline float MainGap()         { return 14.0f * Zoom::uiScale; }
        inline float CardGap()         { return 12.0f * Zoom::uiScale; }
        inline float CardInset()       { return 12.0f * Zoom::uiScale; }
        inline float ContentInset()    { return 13.0f * Zoom::uiScale; }
        inline float CardCorner()      { return 4.0f  * Zoom::uiScale; }
        inline float InnerCardCorner() { return 4.0f  * Zoom::uiScale; }
    }

    namespace Paint
    {
        inline void drawCardOutline (juce::Graphics &g, juce::Rectangle<float> bounds,
                                     float corner, float alpha = 0.40f)
        {
            g.setColour (GUI::Color::Accent.withAlpha (alpha));
            g.drawRoundedRectangle (bounds, corner, 1.0f);
        }
    }
}
