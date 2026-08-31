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

        // The standalone window is a DocumentWindow -> ResizableWindow; hosts
        // that wrap the editor in their own ResizableWindow are recoloured too
        // (no-op elsewhere, since dynamic_cast simply fails).
        //
        // NOTE: do NOT call setUsingNativeTitleBar() here. This fires mid-way
        // through StandaloneFilterWindow::updateContent(), before the wrapper
        // has fitted the window to the editor, so recreating the peer at that
        // moment leaves the standalone stuck at the DocumentWindow's initial
        // 128x128 minimum -- and it also hides the built-in Options button.
        if (auto* sfw = dynamic_cast<juce::ResizableWindow*> (getTopLevelComponent()))
        {
            topLevelIsWindow = true;
            sfw->setColour (juce::ResizableWindow::backgroundColourId, GUI::Color::Background);

            // The window size tracks the editor (applyZoom's top-level setSize),
            // so free user drag-resize is disabled.
            sfw->setResizable (false, false);

            // Windows only: force 1:1 physical scaling so the plugin shell never
            // gets scaled by an OS display scale (CRT_UI_RESIZE.md §8).
           #if JUCE_WINDOWS
            if (auto* peer = getPeer())
                peer->setCustomPlatformScaleFactor (1.0f);
           #endif
        }
    }

    // Zoom engine: the only way the window size changes. Read from the
    // persisted "ui_scale" APVTS choice parameter -> applyZoom.
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchAudioProcessorEditor)
};
