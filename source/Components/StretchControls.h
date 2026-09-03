#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Helpers/StretchDefines.h"
#include "../Helpers/StretchLookAndFeel.h"

// PITCH / RATE / FORMANT rows: dim label over a track + right value box.
class StretchControls : public juce::Component
{
public:
    StretchControls (juce::AudioProcessorValueTreeState& apvts)
    {
        auto makeSlider = [this, &apvts] (const juce::String& paramID,
                                          const juce::String& label,
                                          std::unique_ptr<juce::Slider>& slider,
                                          std::unique_ptr<juce::SliderParameterAttachment>& attachment,
                                          std::unique_ptr<juce::Label>& nameLabel)
        {
            slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal,
                                                       juce::Slider::TextBoxRight);
            // Explicit L&F: pre-editor sliders would keep the stock textbox.
            slider->setLookAndFeel (&sliderLookAndFeel());
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 124, 24);
            slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            addAndMakeVisible (*slider);

            attachment = std::make_unique<juce::SliderParameterAttachment> (
                *apvts.getParameter (paramID), *slider, nullptr);

            nameLabel = std::make_unique<juce::Label> ("", label);
            nameLabel->setJustificationType (juce::Justification::centredLeft);
            nameLabel->setFont (StretchLookAndFeel::makeFont (kLabelFontPx * Zoom::uiScale));
            nameLabel->setColour (juce::Label::textColourId,
                                  GUI::Color::Logo.withAlpha (0.5f));
            addAndMakeVisible (*nameLabel);
        };

        makeSlider ("PitchSemitones", "PITCH", pitchSlider, pitchAttachment, pitchLabel);
        makeSlider ("TimeRatio", "RATE", timeSlider, timeAttachment, timeLabel);
        makeSlider ("FormantSemitones", "FORMANT", formantSlider, formantSliderAttachment, formantLabel);
    }

    ~StretchControls() override
    {
        // setLookAndFeel takes no ownership.
        pitchSlider->setLookAndFeel (nullptr);
        timeSlider->setLookAndFeel (nullptr);
        formantSlider->setLookAndFeel (nullptr);
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
        g.drawText (">> STRETCH PARAMETERS", title, juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        const float s = Zoom::uiScale;
        const Metrics m (s);

        auto area = getLocalBounds()
                        .reduced (juce::roundToInt (GUI::Layout::CardInset() + GUI::Layout::ContentInset()));

        area.removeFromTop (m.sc (kTitleInsetPx) + m.sc (kTitleHeightPx));

        const float labelHeight = 18.0f * s;
        // Taller than the track: the L&F trims the box bottom to centre text.
        // 42px gives a 37px readout at 100%.
        const float sliderHeight = 42.0f * s;
        const float rowGap = 14.0f * s;
        const float rowHeight = labelHeight + 2.0f * s + sliderHeight;

        // Centre the three rows in the remaining card space.
        const float rowsTotal = 3.0f * rowHeight + 2.0f * rowGap;
        area.removeFromTop (juce::jmax (0.0f, (area.getHeight() - rowsTotal) * 0.5f));

        for (int i = 0; i < 3; ++i)
        {
            auto row = area.removeFromTop (rowHeight);

            juce::Slider* slider = (i == 0) ? pitchSlider.get()
                                 : (i == 1) ? timeSlider.get()
                                            : formantSlider.get();
            juce::Label* label = (i == 0) ? pitchLabel.get()
                              : (i == 1) ? timeLabel.get()
                                         : formantLabel.get();

            label->setBounds (row.removeFromTop (labelHeight).toNearestInt());
            row.removeFromTop (2.0f * s);

            auto trackArea = row.removeFromTop (sliderHeight);

            slider->setBounds (trackArea.toNearestInt());

            if (i < 2)
                area.removeFromTop (rowGap);
        }
    }

    // Zoom hook: labels/boxes have explicit fonts/sizes; re-set them here.
    void applyFontScale (float scale)
    {
        for (auto* l : { pitchLabel.get(), timeLabel.get(), formantLabel.get() })
            if (l != nullptr)
                l->setFont (StretchLookAndFeel::makeFont (kLabelFontPx * scale));

        const int boxW = juce::roundToInt (124.0f * scale);
        const int boxH = juce::roundToInt (37.0f * scale);
        for (auto* slider : { pitchSlider.get(), timeSlider.get(), formantSlider.get() })
            if (slider != nullptr)
                slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, boxW, boxH);
    }

private:
    static constexpr int kTitleInsetPx = 5;
    static constexpr int kTitleHeightPx = 16;
    static constexpr float kLabelFontPx = 17.0f;

    // One shared instance for the three faders; outlives them (static).
    static StretchLookAndFeel& sliderLookAndFeel()
    {
        static StretchLookAndFeel lnf;
        return lnf;
    }

    std::unique_ptr<juce::Slider> pitchSlider;
    std::unique_ptr<juce::Slider> timeSlider;
    std::unique_ptr<juce::Slider> formantSlider;

    std::unique_ptr<juce::SliderParameterAttachment> pitchAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> timeAttachment;
    std::unique_ptr<juce::SliderParameterAttachment> formantSliderAttachment;

    std::unique_ptr<juce::Label> pitchLabel;
    std::unique_ptr<juce::Label> timeLabel;
    std::unique_ptr<juce::Label> formantLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchControls)
};
