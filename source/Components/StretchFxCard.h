#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Helpers/StretchDefines.h"
#include "../Helpers/StretchLookAndFeel.h"
#include "StretchTransportCard.h"
#include "../PluginProcessor.h"

// ---------------------------------------------------------------------------
// StretchFxTextButton - bracketed terminal-style button rendered at a size
// that fits long labels like FORMANT PRESERVE inside a 190px card row.
// ---------------------------------------------------------------------------
class StretchFxTextButton : public juce::TextButton
{
public:
    using TextButton::TextButton;

    void paint (juce::Graphics& g) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId),
                                               isMouseOver(), isDown());

        using namespace StretchColors;
        const bool on = getToggleState();
        const bool hot = isMouseOver() || isDown();
        const float s = Zoom::uiScale;
        g.setFont (StretchLookAndFeel::makeFont (20.0f * s));
        g.setColour ((on || hot) ? textPrimary : textMid);

        const juce::String text = on ? ("> " + getButtonText() + " <")
                                     : ("[ " + getButtonText() + " ]");
        g.drawText (text, getLocalBounds().toFloat().translated (0, -juce::roundToInt (2.0f * s)),
                    juce::Justification::centred, true);
    }
};

// ---------------------------------------------------------------------------
// StretchFxCard - PRESERVE / FREEZE / REWIND laid out like the transport
// card: outer dark card, inner card, title strip, three equal FlexBox rows.
// All three toggles ride APVTS parameters via ButtonParameterAttachment, so
// host automation, keyboard shortcuts and the buttons stay in sync.
// ---------------------------------------------------------------------------
class StretchFxCard : public juce::Component
{
public:
    explicit StretchFxCard (StretchAudioProcessor& p)
        : processor (p)
    {
        preserveButton.setButtonText ("FORMANT PRESERVE");
        preserveButton.setClickingTogglesState (true);
        addAndMakeVisible (preserveButton);
        preserveAttachment = std::make_unique<juce::ButtonParameterAttachment> (
            *p.parameters.getParameter ("FormantPreserve"), preserveButton);

        freezeButton.setButtonText ("FREEZE");
        freezeButton.setClickingTogglesState (true);
        addAndMakeVisible (freezeButton);
        freezeAttachment = std::make_unique<juce::ButtonParameterAttachment> (
            *p.parameters.getParameter ("Freeze"), freezeButton);

        // REWIND is a latching reverse-playback toggle: while engaged the
        // RATE sign flips, so the material plays backwards at the same rate.
        // No seeking is involved -- under FREEZE it simply changes nothing
        // audibly (stillness) and never disturbs playback.
        rewindButton.setClickingTogglesState (true);
        addAndMakeVisible (rewindButton);
        rewindAttachment = std::make_unique<juce::ButtonParameterAttachment> (
            *p.parameters.getParameter ("Rewind"), rewindButton);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const float s = Zoom::uiScale;
        const Metrics m (s);

        g.setColour (GUI::Color::CardDark);
        g.fillRoundedRectangle (bounds, GUI::Layout::CardCorner());
        GUI::Paint::drawCardOutline (g, bounds, GUI::Layout::CardCorner());

        auto innerBounds = bounds.reduced (GUI::Layout::CardInset());
        g.setColour (GUI::Color::Card);
        g.fillRoundedRectangle (innerBounds, GUI::Layout::InnerCardCorner());
        GUI::Paint::drawCardOutline (g, innerBounds, GUI::Layout::InnerCardCorner(), 0.25f);

        auto title = innerBounds.withTrimmedLeft ((float) m.sc (kTitleInsetPx))
                        .withTrimmedTop ((float) m.sc (kTitleInsetPx))
                        .removeFromTop ((float) m.sc (kTitleHeightPx));
        g.setColour (GUI::Color::Logo.withAlpha (0.5f));
        g.setFont (StretchLookAndFeel::makeFont (19.0f * s));
        g.drawText (">> FX", title, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const float s = Zoom::uiScale;
        const Metrics m (s);

        auto area = getLocalBounds()
                        .reduced (juce::roundToInt (GUI::Layout::CardInset() + GUI::Layout::ContentInset()));

        // Title strip: 5 px down + 16 px text, matching the paint() inset.
        area.removeFromTop (m.sc (kTitleInsetPx) + m.sc (kTitleHeightPx));

        const float rowGap = 7.0f * s;
        const float rowH = (area.getHeight() - 2.0f * rowGap) / 3.0f;

        juce::FlexBox fb;
        fb.flexDirection = juce::FlexBox::Direction::column;
        fb.justifyContent = juce::FlexBox::JustifyContent::center;

        fb.items.add (juce::FlexItem ((float) area.getWidth(), rowH, preserveButton)
                          .withMargin ({ 0.0f, 0.0f, rowGap, 0.0f }));
        fb.items.add (juce::FlexItem ((float) area.getWidth(), rowH, freezeButton)
                          .withMargin ({ 0.0f, 0.0f, rowGap, 0.0f }));
        fb.items.add (juce::FlexItem ((float) area.getWidth(), rowH, rewindButton));

        fb.performLayout (area.toFloat());
    }

private:
    static constexpr int kTitleInsetPx = 5;
    static constexpr int kTitleHeightPx = 16;

    StretchAudioProcessor& processor;

    StretchFxTextButton preserveButton;
    StretchFxTextButton freezeButton;
    StretchIconButton rewindButton { StretchIconButton::Glyph::Rewind };

    std::unique_ptr<juce::ButtonParameterAttachment> preserveAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> freezeAttachment;
    std::unique_ptr<juce::ButtonParameterAttachment> rewindAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchFxCard)
};
