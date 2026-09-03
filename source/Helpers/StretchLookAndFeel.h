#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

namespace StretchColors
{
    // Green phosphor palette (Pip-Boy / Fairlight style).
    static const juce::Colour background{0xff061410};
    static const juce::Colour body{0xff081711};
    static const juce::Colour card{0xff10241a};
    static const juce::Colour cardDark{0xff0a1a12};
    static const juce::Colour headerBg{0xff04100b};

    static const juce::Colour accent{0xff8affbe};
    static const juce::Colour highlight{0xffeaffe4};
    static const juce::Colour accentDim{0xff1e4d33};
    static const juce::Colour accentSoft{0xff3d8a63};

    static const juce::Colour textPrimary{0xff8affbe};
    static const juce::Colour textMid{0xff63b98f};
    static const juce::Colour textBrand{0xffeaffe4};

    static const juce::Colour buttonOff{0xff142e20};
    static const juce::Colour buttonOn{0xff24513a};
    static const juce::Colour buttonBorder{0xff2f6b49};

    static const juce::Colour menuBg{0xff071309};
    static const juce::Colour menuText{0xff8affbe};
    static const juce::Colour menuTextBright{0xffeaffe4};
    static const juce::Colour menuTextDim{0xff3d8a63};
    static const juce::Colour menuHover{0xff1d4630};
    static const juce::Colour menuBorder{0xff27593d};
    static const juce::Colour menuInnerBorder{0xff123023};
}

// Flat terminal glyph button for JUCE-drawn title bars (V4 defaults paint white).
class StretchTitleBarButton : public juce::Button
{
public:
    StretchTitleBarButton (const juce::String& name,
                           const juce::Path& normalShapeIn,
                           const juce::Path& toggledShapeIn)
        : juce::Button (name),
          normalShape (normalShapeIn),
          toggledShape (toggledShapeIn)
    {
    }

    void paintButton (juce::Graphics& g, bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override
    {
        using namespace StretchColors;

        g.fillAll ((shouldDrawButtonAsHighlighted || shouldDrawButtonAsDown)
                       ? buttonOn.withAlpha (0.6f)
                       : menuBg);

        g.setColour ((! isEnabled() || shouldDrawButtonAsDown)
                         ? textMid.withAlpha (0.6f)
                         : (shouldDrawButtonAsHighlighted ? textPrimary : textMid));

        auto& p = getToggleState() ? toggledShape : normalShape;

        auto reducedRect = juce::Justification (juce::Justification::centred)
                               .appliedToRectangle (juce::Rectangle<int> (getHeight(), getHeight()),
                                                    getLocalBounds())
                               .toFloat()
                               .reduced ((float) getHeight() * 0.3f);

        g.fillPath (p, p.getTransformToScaleToFit (reducedRect, true));
    }

private:
    juce::Path normalShape, toggledShape;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchTitleBarButton)
};

// Slider value box: fixed font, wheel-proof; edit text centred.
class StretchSliderTextBoxLabel : public juce::Label
{
public:
    StretchSliderTextBoxLabel() : juce::Label ({}, {}) {}

    void lookAndFeelChanged() override
    {
        juce::Label::lookAndFeelChanged();
        setFont (getFont());
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override {}

protected:
    // Centre the inline edit text (display state is already centred).
    juce::TextEditor* createEditorComponent() override
    {
        auto* ed = juce::Label::createEditorComponent();
        ed->applyFontToAllText (getFont());
        ed->setJustification (juce::Justification::centred);
        return ed;
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchSliderTextBoxLabel)
};

class StretchLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static const juce::Typeface::Ptr& getTypeface()
    {
        static const juce::Typeface::Ptr typeface = juce::Typeface::createSystemTypefaceFor(
            BinaryData::VT323Regular_ttf,
            BinaryData::VT323Regular_ttfSize);
        return typeface;
    }

    // DirectWrite renders smaller than FreeType; match Linux size.
   #if JUCE_WINDOWS
    static constexpr float kFontSizeScale = 1.15f;
   #else
    static constexpr float kFontSizeScale = 1.0f;
   #endif

    static juce::Font makeFont (float height)
    {
        // Pin legacy metrics: JUCE 9 default metrics render larger at the
        // same height, breaking the designed layout.
        return juce::Font (juce::FontOptions (getTypeface())
                               .withMetricsKind (juce::TypefaceMetricsKind::legacy))
            .withHeight (height * kFontSizeScale);
    }

    StretchLookAndFeel()
    {
        using namespace StretchColors;

        // Midnight first: widgets reading V4's LIGHT scheme fall back dark.
        setColourScheme (getMidnightColourScheme());

        setColour(juce::ResizableWindow::backgroundColourId, background);

        setColour(juce::Slider::thumbColourId, accent);
        setColour(juce::Slider::trackColourId, accentDim);
        setColour(juce::Slider::rotarySliderFillColourId, highlight);
        setColour(juce::Slider::rotarySliderOutlineColourId, accentDim);
        setColour(juce::Slider::textBoxTextColourId, textPrimary);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxHighlightColourId, accentDim);

        setColour(juce::ScrollBar::thumbColourId, accentDim);

        setColour(juce::ComboBox::backgroundColourId, buttonOff);
        setColour(juce::ComboBox::outlineColourId, buttonBorder);
        setColour(juce::ComboBox::textColourId, textPrimary);
        setColour(juce::ComboBox::arrowColourId, textMid);

        setColour(juce::TextButton::buttonColourId, buttonOff);
        setColour(juce::TextButton::buttonOnColourId, buttonOn);
        setColour(juce::TextButton::textColourOffId, textMid);
        setColour(juce::TextButton::textColourOnId, accent);

        setColour(juce::Label::textColourId, textMid);

        setColour(juce::PopupMenu::backgroundColourId, menuBg);
        setColour(juce::PopupMenu::textColourId, menuText);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, menuHover);
        setColour(juce::PopupMenu::highlightedTextColourId, menuTextBright);

        setColour(juce::TextEditor::backgroundColourId, menuBg);
        setColour(juce::TextEditor::textColourId, menuTextBright);
        setColour(juce::TextEditor::highlightColourId, menuHover);
        setColour(juce::TextEditor::highlightedTextColourId, menuTextBright);
        setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TextEditor::focusedOutlineColourId, menuBorder);
        setColour(juce::Label::textWhenEditingColourId, menuTextBright);
        setColour(juce::Label::backgroundWhenEditingColourId, menuBg);
        setColour(juce::Label::outlineWhenEditingColourId, menuBorder);
        setColour(juce::CaretComponent::caretColourId, menuTextBright);

        setColour(juce::ToggleButton::textColourId, textPrimary);
        setColour(juce::ToggleButton::tickColourId, textPrimary);
        setColour(juce::ToggleButton::tickDisabledColourId, accentDim);

        setColour(juce::ListBox::backgroundColourId, menuBg);
        setColour(juce::ListBox::outlineColourId, menuBorder);
        setColour(juce::ListBox::textColourId, menuText);

        setColour(juce::ProgressBar::foregroundColourId, accent);
        setColour(juce::ProgressBar::backgroundColourId, accentDim);
    }

    juce::Font getFont(float height, bool = true) const
    {
        if (getTypeface() != nullptr)
            return makeFont (height * Zoom::uiScale);
        return juce::Font (juce::FontOptions (
            juce::Font::getDefaultSansSerifFontName(), height * Zoom::uiScale * kFontSizeScale, juce::Font::plain)
                               .withMetricsKind (juce::TypefaceMetricsKind::legacy));
    }

    juce::Font getCustomFont(float height, bool = true) const { return getFont(height); }

    juce::Font getLabelFont(juce::Label &label) override
    {
        // Same size for display + inline editor (smaller shrank edits).
        if (label.findParentComponentOfClass<juce::Slider>() != nullptr)
            return getFont(26.0f);
        return getFont(26.0f);
    }

    // Same font on box + editor so edits never crop glyphs.
    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto *l = new StretchSliderTextBoxLabel();
        l->setJustificationType (juce::Justification::centred);
        l->setKeyboardType (juce::TextInputTarget::decimalKeyboard);

        l->setColour (juce::Label::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
        l->setColour (juce::Label::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
        l->setColour (juce::Label::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
        l->setColour (juce::TextEditor::textColourId, slider.findColour (juce::Slider::textBoxTextColourId));
        l->setColour (juce::TextEditor::backgroundColourId, slider.findColour (juce::Slider::textBoxBackgroundColourId));
        l->setColour (juce::TextEditor::outlineColourId, slider.findColour (juce::Slider::textBoxOutlineColourId));
        l->setColour (juce::TextEditor::highlightColourId, slider.findColour (juce::Slider::textBoxHighlightColourId));

        using namespace StretchColors;
        l->setFont (getFont (26.0f));
        return l;
    }
    juce::Font getComboBoxFont(juce::ComboBox &) override { return getFont(24.0f); }
    juce::Font getTextButtonFont(juce::TextButton &, int height) override
    {
        return getFont(juce::jmin(24.0f, (float) height * 0.85f));
    }

    juce::Font getAlertWindowTitleFont() override { return getFont(36.0f); }
    juce::Font getAlertWindowMessageFont() override { return getFont(26.0f, false); }
    int getAlertWindowButtonHeight() override { return juce::roundToInt (34.0f * Zoom::uiScale); }

    // Dark CRT alert box + mono glow frame; text padded, top-anchored.
    void drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert,
                       const juce::Rectangle<int>& textArea,
                       juce::TextLayout& textLayout) override
    {
        using namespace StretchColors;

        const auto bounds = alert.getLocalBounds();

        // Dark body (scanlines show); glow frame; text padded, top-anchored.
        g.setColour (card);
        g.fillRect (bounds);

        g.setColour (juce::Colours::black.withAlpha (0.28f));
        for (int y = 0; y < bounds.getHeight(); y += 2)
            g.fillRect (0, y, bounds.getWidth(), 1);

        // Mono glow frame (double border).
        g.setColour (accent.withAlpha (0.22f));
        g.drawRect (0, 0, bounds.getWidth(), bounds.getHeight(), 1);
        g.setColour (accentDim);
        g.drawRect (1, 1, bounds.getWidth() - 2, bounds.getHeight() - 2, 1);

        // Padded, top-anchored (clear of the buttons).
        const auto padded = textArea.reduced (juce::roundToInt (24.0f * Zoom::uiScale),
                                              juce::roundToInt (6.0f * Zoom::uiScale));
        if (padded.isEmpty())
            return;

        const float layoutHeight = textLayout.getHeight();
        const int drawY = padded.getY();

        textLayout.draw (g, juce::Rectangle<int> (padded.getX(), drawY,
                                                  padded.getWidth(),
                                                  juce::jmin (padded.getHeight(), (int) layoutHeight)).toFloat());
    }

    // Dark title bar + white VT323 title (V4 defaults render white/black).
    void drawDocumentWindowTitleBar (juce::DocumentWindow& window,
                                     juce::Graphics& g,
                                     int w, int h,
                                     int titleSpaceX, int titleSpaceW,
                                     const juce::Image* icon, bool drawTitleTextOnLeft) override
    {
        if (w * h == 0)
            return;

        g.fillAll (StretchColors::menuBg);

        juce::Font font = makeFont ((float) h * 0.65f);
        g.setFont (font);

        // Centre the title in the button-free zone.
        auto textW = juce::GlyphArrangement::getStringWidthInt (font, window.getName());
        int iconW = 0;
        int iconH = 0;

        if (icon != nullptr)
        {
            iconH = static_cast<int> (font.getHeight());
            iconW = icon->getWidth() * iconH / icon->getHeight() + 4;
        }

        textW = juce::jmin (titleSpaceW, textW + iconW);
        auto textX = drawTitleTextOnLeft ? titleSpaceX
                                         : juce::jmax (titleSpaceX, (w - textW) / 2);

        if (textX + textW > titleSpaceX + titleSpaceW)
            textX = titleSpaceX + titleSpaceW - textW;

        if (icon != nullptr)
        {
            g.setOpacity (window.isActiveWindow() ? 1.0f : 0.6f);
            g.drawImageWithin (*icon, textX, (h - iconH) / 2, iconW, iconH,
                               juce::RectanglePlacement::centred, false);
            textX += iconW;
            textW -= iconW;
        }

        g.setColour (StretchColors::menuTextBright);
        g.drawText (window.getName(), textX, 0, textW, h,
                    juce::Justification::centredLeft, true);
    }

    juce::Button* createDocumentWindowButton (int buttonType) override
    {
        const float crossThickness = 0.15f;
        juce::Path shape;

        if (buttonType == juce::DocumentWindow::closeButton)
        {
            shape.addLineSegment ({ 0.0f, 0.0f, 1.0f, 1.0f }, crossThickness);
            shape.addLineSegment ({ 1.0f, 0.0f, 0.0f, 1.0f }, crossThickness);
            return new StretchTitleBarButton ("close", shape, shape);
        }

        if (buttonType == juce::DocumentWindow::minimiseButton)
        {
            shape.addLineSegment ({ 0.0f, 0.5f, 1.0f, 0.5f }, crossThickness);
            return new StretchTitleBarButton ("minimise", shape, shape);
        }

        if (buttonType == juce::DocumentWindow::maximiseButton)
        {
            shape.addLineSegment ({ 0.5f, 0.0f, 0.5f, 1.0f }, crossThickness);
            shape.addLineSegment ({ 0.0f, 0.5f, 1.0f, 0.5f }, crossThickness);

            juce::Path fullscreenShape;
            fullscreenShape.startNewSubPath (45.0f, 100.0f);
            fullscreenShape.lineTo (0.0f, 100.0f);
            fullscreenShape.lineTo (0.0f, 0.0f);
            fullscreenShape.lineTo (100.0f, 0.0f);
            fullscreenShape.lineTo (100.0f, 45.0f);
            fullscreenShape.addRectangle (45.0f, 45.0f, 100.0f, 100.0f);
            juce::PathStrokeType (30.0f).createStrokedPath (fullscreenShape, fullscreenShape);

            return new StretchTitleBarButton ("maximise", shape, fullscreenShape);
        }

        jassertfalse;
        return nullptr;
    }

    juce::Font getPopupMenuFont() override { return getCustomFont(24.0f); }
    int getPopupMenuBorderSize() override { return juce::roundToInt (3.0f * Zoom::uiScale); }

    void drawPopupMenuBackground(juce::Graphics &g, int width, int height) override
    {
        using namespace StretchColors;
        g.fillAll(menuBg);
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        for (int y = 0; y < height; y += 2)
            g.fillRect(0, y, width, 1);
        g.setColour(menuBorder.withAlpha(0.7f));
        g.drawRect(0, 0, width, height, 1);
        g.setColour(menuInnerBorder);
        g.drawRect(1, 1, width - 2, height - 2, 1);
    }

    void drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool, bool, const juce::String &text,
                           const juce::String &, const juce::Drawable *, const juce::Colour *) override
    {
        using namespace StretchColors;
        if (isSeparator)
        {
            g.setColour(menuBorder.withAlpha(0.6f));
            g.fillRect(area.getX(), area.getCentreY(), area.getWidth(), 1);
            return;
        }
        if (isHighlighted)
        {
            g.setColour(menuHover);
            g.fillRect(area.getX(), area.getY() + 1, area.getWidth(), area.getHeight() - 1);
            g.setColour(menuTextBright.withAlpha(0.8f));
            g.fillRect(area.getX(), area.getY() + 1, 2, area.getHeight() - 1);
        }
        g.setColour(juce::Colours::black.withAlpha(0.22f));
        for (int y = 1; y < area.getHeight(); y += 2)
            g.fillRect(area.getX(), area.getY() + y, area.getWidth(), 1);
        g.setColour(! isActive ? menuTextDim : isHighlighted ? menuTextBright : menuText);
        g.setFont(getPopupMenuFont());
        g.drawText(text, area.reduced(10, 0), juce::Justification::centredLeft, true);
    }

    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                   int standardMenuItemHeight,
                                   int& idealWidth, int& idealHeight) override
    {
        LookAndFeel_V4::getIdealPopupMenuItemSize(text, isSeparator,
                                                   standardMenuItemHeight,
                                                   idealWidth, idealHeight);
        idealWidth = juce::jmax(idealWidth, 220);
    }

    juce::Slider::SliderLayout getSliderLayout(juce::Slider &slider) override
    {
        juce::Slider::SliderLayout layout;
        auto bounds = slider.getLocalBounds();
        if (slider.isRotary())
        {
            auto textBoxStrip = bounds.removeFromBottom(24);
            layout.textBoxBounds = textBoxStrip.withSizeKeepingCentre(
                juce::jmin(96, textBoxStrip.getWidth()), 22);
            layout.sliderBounds = bounds.expanded(2, 2);
        }
        else if (slider.isHorizontal())
        {
            // Trimmed at the bottom to centre on the track; never above
            // component bounds (clips glyph tops).
            layout.textBoxBounds = bounds.removeFromRight (juce::roundToInt (124.0f * Zoom::uiScale))
                                          .withTrimmedBottom (juce::roundToInt (5.0f * Zoom::uiScale));
            layout.sliderBounds = bounds.reduced(4, 0);
        }
        else
        {
            layout.textBoxBounds = bounds.removeFromBottom(60);
            layout.sliderBounds = bounds.reduced(0, 8).translated(0, 8);
        }
        return layout;
    }

    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider &) override
    {
        using namespace StretchColors;
        const float cx = (float)x + (float)width * 0.5f;
        const float cy = (float)y + (float)height * 0.5f;
        const float r = juce::jmin((float)width, (float)height) * 0.42f;
        const float toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        const float arcR = r + 6.0f;

        g.setColour(cardDark);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
        g.setColour(accentDim.withAlpha(0.35f));
        g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.0f);

        juce::Path bgArc;
        bgArc.addCentredArc(cx, cy, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(accentDim);
        g.strokePath(bgArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::square));

        juce::Path valArc;
        valArc.addCentredArc(cx, cy, arcR, arcR, 0.0f, rotaryStartAngle, toAngle, true);
        g.setColour(highlight);
        g.strokePath(valArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::square));

        const float outerR = r * 0.85f;
        g.setColour(accent);
        const float pLen = 6.0f;
        const float pWid = 3.0f;
        juce::Path pointer;
        pointer.addRectangle(cx - pWid * 0.5f, cy - outerR - pLen, pWid, pLen);
        pointer.applyTransform(juce::AffineTransform::rotation(toAngle, cx, cy));
        g.fillPath(pointer);
    }

    void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height,
                          float sliderPos, float, float,
                          const juce::Slider::SliderStyle style, juce::Slider &) override
    {
        using namespace StretchColors;
        if (style == juce::Slider::LinearHorizontal)
        {
            const float th = 12.0f * Zoom::uiScale;
            juce::Rectangle<float> track{
                (float)x, (float)y + (float)height * 0.5f - th * 0.5f,
                (float)width, th};
            g.setColour(accentDim);
            g.fillRect(track);

            const float fillW = sliderPos - track.getX();
            if (fillW > 0.5f)
            {
                g.setColour(highlight);
                g.fillRect(juce::Rectangle<float>(track.getX(), track.getY(),
                                                  juce::jmin(fillW, track.getWidth()), th));
            }
            return;
        }
        const float tw = 12.0f * Zoom::uiScale;
        juce::Rectangle<float> track{
            (float)x + (float)width * 0.5f - tw * 0.5f,
            (float)y, tw, (float)height};
        g.setColour(accentDim);
        g.fillRect(track);

        const float fillH = juce::jlimit(0.0f, track.getHeight(), track.getBottom() - sliderPos);
        if (fillH > 0.5f)
        {
            float fillY = track.getBottom() - fillH;
            g.setColour(highlight);
            g.fillRect(juce::Rectangle<float>(track.getX(), fillY, tw, fillH));
        }
    }

    void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                              const juce::Colour &, bool shouldDrawButtonAsHighlighted, bool) override
    {
        using namespace StretchColors;
        const bool isOn = button.getToggleState();
        const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        if (isOn)
        {
            g.setColour(buttonOn);
            g.fillRoundedRectangle(bounds, 2.0f);
        }
        else if (shouldDrawButtonAsHighlighted)
        {
            g.setColour(buttonOn.withAlpha(0.6f));
            g.fillRoundedRectangle(bounds, 2.0f);
        }
        else
        {
            g.setColour(buttonOff);
            g.fillRoundedRectangle(bounds, 2.0f);
        }
        g.setColour(buttonBorder);
        g.drawRoundedRectangle(bounds, 2.0f, 1.0f);
    }

    void drawButtonText(juce::Graphics &g, juce::TextButton &button,
                        bool shouldDrawButtonAsHighlighted, bool) override
    {
        using namespace StretchColors;
        const bool isOn = button.getToggleState();
        const auto area = button.getLocalBounds().toFloat();
        g.setFont(getCustomFont(24.0f));
        g.setColour(! button.isEnabled() ? textMid.withAlpha (0.35f)
                    : isOn ? textPrimary : (shouldDrawButtonAsHighlighted ? textPrimary : textMid));
        const juce::String text = isOn ? ("> " + button.getButtonText() + " <")
                                       : ("[ " + button.getButtonText() + " ]");
        // Plain centred: caps-only ink centres itself; the old -2px sat high.
        g.drawText(text, area, juce::Justification::centred, true);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool isButtonDown) override
    {
        if (!button.getButtonText().isEmpty())
        {
            drawButtonBackground(g, button, findColour(juce::TextButton::buttonColourId),
                                 shouldDrawButtonAsHighlighted, isButtonDown);
            using namespace StretchColors;
            const bool isOn = button.getToggleState();
            const auto area = button.getLocalBounds();
            g.setFont(getCustomFont(24.0f));
            g.setColour(isOn ? textPrimary : (shouldDrawButtonAsHighlighted ? textPrimary : textMid));
            const juce::String text = isOn ? ("> " + button.getButtonText() + " <")
                                           : ("[ " + button.getButtonText() + " ]");
g.drawText(text, area, juce::Justification::centred, true);
        }
        else
        {
            LookAndFeel_V4::drawToggleButton(g, button, shouldDrawButtonAsHighlighted, isButtonDown);
        }
    }

    void drawComboBox(juce::Graphics &g, int width, int height,
                      bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace StretchColors;
        g.setColour(buttonOff);
        g.fillRoundedRectangle(0, 0, (float)width, (float)height, 2.0f);
        g.setColour(buttonBorder);
        g.drawRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f, (float)height - 1.0f, 2.0f, 1.0f);
        if (box.getTextWhenNothingSelected() == "\u2630")
            return;
        juce::Path p;
        const float arrowW = 14.0f * Zoom::uiScale;
        const float arrowH = 2.0f * Zoom::uiScale;
        const float arrowTip = 3.0f * Zoom::uiScale;
        p.addTriangle((float)width - arrowW, (float)height * 0.5f - arrowH,
                      (float)width - arrowW + 8.0f * Zoom::uiScale, (float)height * 0.5f - arrowH,
                      (float)width - arrowW + 4.0f * Zoom::uiScale, (float)height * 0.5f + arrowTip);
        g.setColour(textMid);
        g.fillPath(p);
    }

    void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override
    {
        label.setBounds(0, 0, box.getWidth() - 20, box.getHeight() - 7);
        label.setFont(getCustomFont(21.0f));
        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, StretchColors::textPrimary);
    }

    void drawComboBoxTextWhenNothingSelected(juce::Graphics &g, juce::ComboBox &box,
                                             juce::Label &label) override
    {
        if (box.getTextWhenNothingSelected() == "\u2630")
        {
            const auto r = box.getLocalBounds().toFloat();
            const float cx = r.getCentreX();
            const float cy = r.getCentreY();
            const float barW = 14.0f * Zoom::uiScale;
            const float barH = 2.0f * Zoom::uiScale;
            const float gap  = 5.0f * Zoom::uiScale;
            g.setColour(StretchColors::textMid);
            g.fillRect(juce::Rectangle<float>(cx - barW * 0.5f, cy - gap - barH * 0.5f, barW, barH));
            g.fillRect(juce::Rectangle<float>(cx - barW * 0.5f, cy - barH * 0.5f,          barW, barH));
            g.fillRect(juce::Rectangle<float>(cx - barW * 0.5f, cy + gap - barH * 0.5f,    barW, barH));
            return;
        }
        juce::LookAndFeel_V4::drawComboBoxTextWhenNothingSelected(g, box, label);
    }
};
