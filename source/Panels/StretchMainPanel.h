#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Helpers/StretchDefines.h"
#include "../PluginProcessor.h"
#include "../Components/WaveformDisplay.h"
#include "../Components/StretchControls.h"
#include "../Components/StretchFxCard.h"
#include "../Components/StretchTransportCard.h"

class StretchMainPanel : public juce::Component
{
public:
    explicit StretchMainPanel (StretchAudioProcessor& p)
        : waveform (p),
          controls (p.parameters),
          fx (p),
          transport (p)
    {
        addAndMakeVisible (waveform);
        addAndMakeVisible (controls);
        addAndMakeVisible (fx);
        addAndMakeVisible (transport);
    }

    void resized() override
    {
        const float s = scaleFor (*this);
        const Metrics m (s);

        auto area = getLocalBounds().reduced (juce::roundToInt (GUI::Layout::MainMargin()));

        // 300 fits the three fader rows (3 x (18 label + 2 + 42 strip) + gaps
        // = 214px) inside the card after insets + title strip; anything less
        // clips the FORMANT row at the card edge. Scales with the UI zoom.
        const int controlsHeight = m.sc (300.0f);
        const int gap = juce::roundToInt (GUI::Layout::MainGap());

        waveform.setBounds (area.removeFromTop (area.getHeight() - controlsHeight - gap));
        area.removeFromTop (gap);

        // Bottom band: sliders card (left) + FX card + transport card (right).
        const int sideCardWidth = m.sc (190.0f);
        transport.setBounds (area.removeFromRight (sideCardWidth));
        area.removeFromRight (gap);
        fx.setBounds (area.removeFromRight (sideCardWidth));
        area.removeFromRight (gap);
        controls.setBounds (area);
    }

    WaveformDisplay waveform;
    StretchControls controls;
    StretchFxCard fx;
    StretchTransportCard transport;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchMainPanel)
};
