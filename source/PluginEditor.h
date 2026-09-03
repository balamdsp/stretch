#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "Helpers/StretchDefines.h"
#include "Helpers/StretchLookAndFeel.h"
#include "Panels/StretchTopPanel.h"
#include "Panels/StretchMainPanel.h"
#include "Components/CRTScreen.h"

class StretchAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     public juce::DragAndDropContainer,
                                     private juce::AudioProcessorValueTreeState::Listener
{
public:
    StretchAudioProcessorEditor (StretchAudioProcessor&);
    ~StretchAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void parentHierarchyChanged() override
    {
        if (topLevelIsWindow)
            return;

        // Recolour host ResizableWindow wrappers too (dynamic_cast no-ops).
        //
        // NOTE: no setUsingNativeTitleBar() here: fires mid-updateContent(),
        // recreating the peer then sticks standalone at 128x128 minimum.
        if (auto* sfw = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        {
            topLevelIsWindow = true;
            sfw->setColour (juce::ResizableWindow::backgroundColourId, GUI::Color::Background);

            // X11 size lock: peer keeps windowIsResizable or WM hides hints;
            // min==max constrainer pins do the locking. Hosts use legacy path.
            juce::Component::SafePointer<juce::ResizableWindow> safeSfw { sfw };
            juce::Component::SafePointer<StretchAudioProcessorEditor> safeThis { this };
            juce::MessageManager::callAsync ([safeSfw, safeThis]
            {
                if (safeSfw == nullptr || safeThis == nullptr)
                    return;
                const int w = juce::roundToInt (Zoom::BaseW * safeThis->uiScale);
                const int h = juce::roundToInt (Zoom::BaseH * safeThis->uiScale);
                if (safeThis->processor.isRunningAsStandalone())
                {
                    if (! safeSfw->isResizable())
                        safeSfw->setResizable (true, false);
                    safeSfw->setUsingNativeTitleBar (true); // no-op if already set
                    // Outer accounting (see applyZoom): live frame in.
                    const auto frame = getNativeFrameSize (safeSfw.getComponent());
                    const int outerW = juce::jmax (1, w + frame.x);
                    const int outerH = juce::jmax (1, h + frame.y);
                    if (auto* c = safeSfw->getConstrainer())
                        c->setSizeLimits (outerW, outerH, outerW, outerH);
                    safeThis->setSize (w, h);
                    safeSfw->setSize (outerW, outerH);
                }
                else
                {
                    safeSfw->setUsingNativeTitleBar (true); // no-op if already set
                    safeSfw->setResizable (false, false);
                    safeSfw->setSize (w, h);
                }
            });
            // OS scale stays; Zoom::uiScale multiplies on top.
        }
    }

    // Zoom engine: persisted "ui_scale" param -> applyZoom.
    void applyZoom (float scale);
    void parameterChanged (const juce::String& parameterID, float newValue) override;

private:
    void updateZoomLimits();
    void applyScaledFonts();
    void refreshAllLookAndFells();

    StretchAudioProcessor& processor;

    struct Screen : public juce::Component
    {
        explicit Screen (StretchAudioProcessor& p)
            : topPanel (p), mainPanel (p)
        {
            addAndMakeVisible (topPanel);
            addAndMakeVisible (mainPanel);
        }

        void paint (juce::Graphics&) override {}

        void resized() override
        {
            juce::FlexBox fb;
            fb.flexDirection = juce::FlexBox::Direction::column;

            float topPanelHeight = getHeight() / 8.0f;

            juce::FlexItem top ((float) getWidth(), topPanelHeight, topPanel);
            juce::FlexItem main ((float) getWidth(), (float) (getHeight() - (int) topPanelHeight), mainPanel);

            fb.items.addArray ({top, main});
            fb.performLayout (getLocalBounds().toFloat());
        }

        StretchTopPanel topPanel;
        StretchMainPanel mainPanel;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Screen)
    };

    Screen screen;
    CRTScreen crtOverlay { &screen, &processor.getCrtEnabledFlag() };

    float uiScale = Zoom::uiScale;

    bool topLevelIsWindow = false;

    void onFileDropped (const juce::File& file);
    void onSetExportFolder();

    // Native-frame extents (L+R, T+B); (0,0) when n/a or unknown.
    static juce::Point<int> getNativeFrameSize (juce::Component* topLevelWindow);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchAudioProcessorEditor)
};
