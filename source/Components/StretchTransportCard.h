#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Helpers/StretchDefines.h"
#include "../Helpers/StretchLookAndFeel.h"
#include "../PluginProcessor.h"

// ---------------------------------------------------------------------------
// StretchIconButton - boxed transport button with a hand-drawn vector glyph:
// play triangle (morphs into pause bars), stop square, loop arrow.
// ---------------------------------------------------------------------------
class StretchIconButton : public juce::TextButton
{
public:
    enum class Glyph { Play, Pause, Stop, Loop, Rewind, X, Plus, Minus, Fit };

    StretchIconButton (Glyph g, const juce::String& name = {})
        : juce::TextButton (name.isEmpty() ? "icon" : name), glyph (g)
    {
        setClickingTogglesState (g == Glyph::Loop); // only LOOP is a toggle
    }

    void setGlyph (Glyph g)
    {
        if (glyph != g)
        {
            glyph = g;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId),
                                               isMouseOver(), isDown());

        const bool bright = getToggleState() || isMouseOver() || isDown();
        g.setColour (bright ? StretchColors::textPrimary : StretchColors::textMid);

        // Glyphs are drawn about the button's centre; every constant scales
        // with the current UI zoom so icons stay proportional at any size.
        const float s = Zoom::uiScale;

        auto b = getLocalBounds().toFloat().reduced (10.0f * s);
        const float cx = b.getCentreX();
        const float cy = b.getCentreY();

        switch (glyph)
        {
            case Glyph::Play:
            {
                juce::Path tri;
                tri.addTriangle (cx - 4.5f * s, cy - 7.0f * s,
                                 cx - 4.5f * s, cy + 7.0f * s,
                                 cx + 7.5f * s, cy);
                g.fillPath (tri);
                break;
            }
            case Glyph::Pause:
            {
                g.fillRect (cx - 6.0f * s, cy - 7.0f * s, 3.5f * s, 14.0f * s);
                g.fillRect (cx + 2.5f * s, cy - 7.0f * s, 3.5f * s, 14.0f * s);
                break;
            }
            case Glyph::Stop:
            {
                g.fillRect (cx - 6.0f * s, cy - 6.0f * s, 12.0f * s, 12.0f * s);
                break;
            }
            case Glyph::Rewind:
            {
                // Double left-pointing triangles (rewind / scrub back).
                juce::Path head;
                head.addTriangle (cx + 6.0f * s, cy - 7.0f * s,
                                  cx + 6.0f * s, cy + 7.0f * s,
                                  cx - 2.0f * s, cy);
                g.fillPath (head);

                juce::Path tail;
                tail.addTriangle (cx - 1.0f * s, cy - 7.0f * s,
                                  cx - 1.0f * s, cy + 7.0f * s,
                                  cx - 9.0f * s, cy);
                g.fillPath (tail);
                break;
            }
            case Glyph::X:
            {
                g.drawLine (cx - 5.0f * s, cy - 5.0f * s, cx + 5.0f * s, cy + 5.0f * s, 2.2f * s);
                g.drawLine (cx - 5.0f * s, cy + 5.0f * s, cx + 5.0f * s, cy - 5.0f * s, 2.2f * s);
                break;
            }
            case Glyph::Loop:
            {
                // Circular arrow: open ring + head. Note: the ring radius and
                // head angles are scale-independent; only offsets/strokes move
                // with the zoom.
                const float r = 7.5f * s;
                juce::Path ring;
                ring.addCentredArc (cx, cy, r, r, 0.0f,
                                    -juce::MathConstants<float>::halfPi * 0.9f,
                                    juce::MathConstants<float>::twoPi * 0.85f,
                                    true);
                g.strokePath (ring, juce::PathStrokeType ((getToggleState() ? 2.6f : 2.0f) * s));

                juce::Path head;
                const float hx = cx + r * std::cos (-juce::MathConstants<float>::halfPi * 0.9f);
                const float hy = cy + r * std::sin (-juce::MathConstants<float>::halfPi * 0.9f);
                head.addTriangle (hx - 2.0f * s, hy + 1.0f * s,
                                  hx + 4.5f * s, hy - 1.5f * s,
                                  hx - 1.0f * s, hy + 5.0f * s);
                g.fillPath (head);

                if (getToggleState())
                    g.fillRect (cx - 2.5f * s, cy - 2.5f * s, 5.0f * s, 5.0f * s);
                break;
            }
            case Glyph::Plus:
            {
                // Smaller strokes for the compact waveform-zoom cluster.
                g.drawLine (cx - 5.0f * s, cy, cx + 5.0f * s, cy, 2.0f * s);
                g.drawLine (cx, cy - 5.0f * s, cx, cy + 5.0f * s, 2.0f * s);
                break;
            }
            case Glyph::Minus:
            {
                g.drawLine (cx - 5.0f * s, cy, cx + 5.0f * s, cy, 2.0f * s);
                break;
            }
            case Glyph::Fit:
            {
                // Bracket pair with a centre bar ("zoom to fit").
                g.drawLine (cx - 7.0f * s, cy - 5.0f * s, cx - 7.0f * s, cy + 5.0f * s, 1.8f * s);
                g.drawLine (cx - 7.0f * s, cy - 5.0f * s, cx - 3.5f * s, cy - 5.0f * s, 1.8f * s);
                g.drawLine (cx - 7.0f * s, cy + 5.0f * s, cx - 3.5f * s, cy + 5.0f * s, 1.8f * s);

                g.drawLine (cx + 7.0f * s, cy - 5.0f * s, cx + 7.0f * s, cy + 5.0f * s, 1.8f * s);
                g.drawLine (cx + 3.5f * s, cy - 5.0f * s, cx + 7.0f * s, cy - 5.0f * s, 1.8f * s);
                g.drawLine (cx + 3.5f * s, cy + 5.0f * s, cx + 7.0f * s, cy + 5.0f * s, 1.8f * s);

                g.drawLine (cx - 3.0f * s, cy, cx + 3.0f * s, cy, 1.8f * s);
                break;
            }
        }
    }

private:
    Glyph glyph;
};

// ---------------------------------------------------------------------------
// StretchTransportCard - PLAY / STOP / LOOP as three equal FlexBox rows.
// ---------------------------------------------------------------------------
class StretchTransportCard : public juce::Component,
                             private juce::Timer
{
public:
    explicit StretchTransportCard (StretchAudioProcessor& proc)
        : processor (proc)
    {
        playButton.setGlyph (StretchIconButton::Glyph::Play);
        playButton.onClick = [this] { processor.transportPlay(); };
        addAndMakeVisible (playButton);

        stopButton.setGlyph (StretchIconButton::Glyph::Stop);
        stopButton.onClick = [this] { processor.transportStop(); };
        addAndMakeVisible (stopButton);

        loopButton.setGlyph (StretchIconButton::Glyph::Loop);
        loopButton.setToggleState (processor.isLooping(), juce::dontSendNotification);
        loopButton.onClick = [this] { processor.setLooping (loopButton.getToggleState()); };
        addAndMakeVisible (loopButton);

        startTimerHz (30);
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
        g.drawText (">> TRANSPORT", title, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const float s = Zoom::uiScale;
        const Metrics m (s);

        auto area = getLocalBounds()
                        .reduced (juce::roundToInt (GUI::Layout::CardInset() + GUI::Layout::ContentInset()));

        // Title strip: 5 px down + 16 px text, matching the paint() inset.
        area.removeFromTop (m.sc (kTitleInsetPx) + m.sc (kTitleHeightPx));

        // Three equal rows, explicitly sized FlexItems (cannot collapse to zero).
        const float rowGap = 7.0f * s;
        const float rowH = (area.getHeight() - 2.0f * rowGap) / 3.0f;

        juce::FlexBox fb;
        fb.flexDirection = juce::FlexBox::Direction::column;
        fb.justifyContent = juce::FlexBox::JustifyContent::center;

        fb.items.add (juce::FlexItem ((float) area.getWidth(), rowH, playButton)
                          .withMargin ({ 0.0f, 0.0f, rowGap, 0.0f }));
        fb.items.add (juce::FlexItem ((float) area.getWidth(), rowH, stopButton)
                          .withMargin ({ 0.0f, 0.0f, rowGap, 0.0f }));
        fb.items.add (juce::FlexItem ((float) area.getWidth(), rowH, loopButton));

        fb.performLayout (area.toFloat());
    }

private:
    void timerCallback() override
    {
        // Keep glyphs honest even when the transport changes elsewhere.
        playButton.setGlyph (processor.isPlaying()
            ? StretchIconButton::Glyph::Pause
            : StretchIconButton::Glyph::Play);

        loopButton.setToggleState (processor.isLooping(), juce::dontSendNotification);
    }

    static constexpr int kTitleInsetPx = 5;
    static constexpr int kTitleHeightPx = 16;

    StretchAudioProcessor& processor;

    StretchIconButton playButton { StretchIconButton::Glyph::Play };
    StretchIconButton stopButton { StretchIconButton::Glyph::Stop };
    StretchIconButton loopButton { StretchIconButton::Glyph::Loop };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchTransportCard)
};
