#pragma once

#if JucePlugin_Build_Standalone

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "../Helpers/StretchDefines.h"
#include "../Helpers/StretchLookAndFeel.h"

// Styled AudioDeviceSelector in a non-modal DocumentWindow.
class AudioSettingsPanel : public juce::Component,
                           private juce::Button::Listener
{
public:
    AudioSettingsPanel (juce::AudioDeviceManager& dm, float scale = Zoom::uiScale)
        : deviceManager (dm),
          layerScale (scale),
          selectorComp (dm, 0, 2, 0, 2, true, true, false, true)
    {
        selectorComp.setLookAndFeel (&settingsLF);
        addAndMakeVisible (selectorComp);

        closeBtn.setButtonText ("CLOSE");
        closeBtn.setClickingTogglesState (false);
        closeBtn.setRepaintsOnMouseActivity (true);
        closeBtn.addListener (this);
        addAndMakeVisible (closeBtn);

        setSize (juce::roundToInt (760.0f * layerScale),
                 juce::roundToInt (540.0f * layerScale));
    }

    ~AudioSettingsPanel() override
    {
        selectorComp.setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (GUI::Color::Background);

        const int titleH = juce::roundToInt (layerScale * 34.0f);
        auto titleArea = getLocalBounds().removeFromTop (titleH);
        g.setColour (GUI::Color::Logo.withAlpha (0.70f));
        g.setFont (StretchLookAndFeel::makeFont (18.0f * layerScale));
        g.drawText (">> AUDIO / MIDI SETTINGS",
                    titleArea.reduced ((int) GUI::Layout::ContentInset(),
                                       juce::roundToInt (6.0f * layerScale)),
                    juce::Justification::centredLeft, false);

        g.setColour (GUI::Color::Accent.withAlpha (0.15f));
        g.drawLine (0.0f, titleH - 1.0f, (float) getWidth(), titleH - 1.0f, 1.0f);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop (juce::roundToInt (34.0f * layerScale));

        auto bottomBar = bounds.removeFromBottom (juce::roundToInt (46.0f * layerScale));
        closeBtn.setBounds (bottomBar.withSizeKeepingCentre (
            juce::roundToInt (140.0f * layerScale), juce::roundToInt (26.0f * layerScale)));

        selectorComp.setBounds (bounds.reduced (juce::roundToInt (8.0f * layerScale),
                                                juce::roundToInt (4.0f * layerScale)));
    }

private:
    void buttonClicked (juce::Button*) override
    {
        if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
            dw->closeButtonPressed();
    }

    // Smaller fonts than the main L&F: the device selector is dense.
    struct SettingsLookAndFeel : public StretchLookAndFeel
    {
        juce::Font getComboBoxFont (juce::ComboBox&) override
        {
            return getFont (19.0f);
        }

        juce::Font getTextButtonFont (juce::TextButton&, int height) override
        {
            return getFont (juce::jmin (18.0f, (float) height * 0.85f));
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                             bool shouldDrawButtonAsHighlighted, bool isButtonDown) override
        {
            using namespace StretchColors;

            const bool isOn = button.getToggleState();
            const auto area = button.getLocalBounds().toFloat();

            if (button.getButtonText() == "X")
            {
                g.setFont (getCustomFont (18.0f));
                g.setColour (shouldDrawButtonAsHighlighted ? textBrand : textMid);
                g.drawText ("X", area.translated (0, -2), juce::Justification::centred, false);
                return;
            }

            g.setFont (getCustomFont (18.0f));
            g.setColour (isOn ? textPrimary : (shouldDrawButtonAsHighlighted ? textPrimary : textMid));

            const juce::String text = isOn ? ("> " + button.getButtonText() + " <")
                                           : ("[ " + button.getButtonText() + " ]");

            g.drawText (text, area.translated (0, -2), juce::Justification::centred, true);
        }

        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                               bool shouldDrawButtonAsHighlighted, bool isButtonDown) override
        {
            if (! button.getButtonText().isEmpty())
            {
                drawButtonBackground (g, button, findColour (juce::TextButton::buttonColourId),
                                      shouldDrawButtonAsHighlighted, isButtonDown);

                using namespace StretchColors;
                const bool isOn = button.getToggleState();
                const auto area = button.getLocalBounds();
                g.setFont (getCustomFont (18.0f));
                g.setColour (isOn ? textPrimary : (shouldDrawButtonAsHighlighted ? textPrimary : textMid));

                const juce::String text = isOn ? ("> " + button.getButtonText() + " <")
                                               : ("[ " + button.getButtonText() + " ]");

                g.drawText (text, area.translated (0, -2), juce::Justification::centred, true);
            }
            else
            {
                LookAndFeel_V4::drawToggleButton (g, button, shouldDrawButtonAsHighlighted, isButtonDown);
            }
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (0, 0, box.getWidth() - 20, box.getHeight() - 7);
            label.setFont (getCustomFont (17.0f));
            label.setJustificationType (juce::Justification::centred);
            label.setColour (juce::Label::textColourId, StretchColors::textPrimary);
        }
    };

    juce::AudioDeviceManager& deviceManager;
    const float layerScale;
    SettingsLookAndFeel settingsLF;
    juce::AudioDeviceSelectorComponent selectorComp;
    juce::TextButton closeBtn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioSettingsPanel)
};

// Non-modal wrapper window for the audio settings panel.
// Avoids the modal DialogWindow mouse-tracking bug where hover
// feedback never appears because JUCE's mouse input source
// doesn't update its cached peer/position for a stationary cursor
// over a freshly-opened modal dialog.
class SettingsWindow : public juce::DocumentWindow
{
public:
    SettingsWindow (juce::AudioDeviceManager& dm)
        : juce::DocumentWindow ("Audio/MIDI Settings",
                                juce::Colour (0xff0c0c0c),
                                allButtons),
          scale (Zoom::uiScale)
    {
        setContentOwned (new AudioSettingsPanel (dm, scale), true);
        setResizable (false, false);
        centreWithSize (juce::roundToInt (760.0f * scale),
                        juce::roundToInt (540.0f * scale));
        setVisible (true);
    }

    void closeButtonPressed() override { delete this; }

private:
    const float scale;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsWindow)
};

#endif
