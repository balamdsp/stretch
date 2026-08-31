#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_graphics/juce_graphics.h>
#include "../Helpers/StretchDefines.h"
#include "../Helpers/StretchLookAndFeel.h"
#include "../Helpers/StretchPresets.h"
#include "../Components/StretchExportDialogs.h"
#include "../PluginProcessor.h"

#if JUCE_STANDALONE_APPLICATION
 #include <juce_audio_devices/juce_audio_devices.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include "../Standalone/CustomStandaloneFilterWindow.h"
 #include "../Components/AudioSettingsPanel.h"
#endif

class StretchAudioProcessor;

// ---------------------------------------------------------------------------
// HamburgerButton - boxed background plus three hand-drawn bars
// (VT323 has no U+2630 glyph), brightening on hover.
// ---------------------------------------------------------------------------
class StretchHamburgerButton : public juce::TextButton
{
public:
    using TextButton::TextButton;

    void paint (juce::Graphics& g) override
    {
        getLookAndFeel().drawButtonBackground (g, *this, findColour (buttonColourId),
                                               isMouseOver(), isDown());

        const auto bounds = getLocalBounds().toFloat();
        const float s = scaleFor (*this);
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float barW = 18.0f * s;
        const float barH = 2.0f * s;
        const float gap = 5.0f * s;

        g.setColour (isMouseOver() ? StretchColors::textPrimary : StretchColors::textMid);
        for (int i = -1; i <= 1; ++i)
            g.fillRect (cx - barW * 0.5f, cy + i * gap - barH * 0.5f, barW, barH);
    }
};

// ---------------------------------------------------------------------------
// StretchExportButton - drag out the rendered WAV while the mouse is held,
// or click for a "saved to..." popup. RATE below 10 % warns first (a long
// render cannot pause mid-drag, so a low-rate drag falls back to the click
// confirm flow).
// ---------------------------------------------------------------------------
class StretchExportButton : public juce::TextButton
{
public:
    explicit StretchExportButton (StretchAudioProcessor& proc)
        : processor (proc)
    {
        setButtonText ("EXPORT");

        // An export may already be running when the editor is (re)created
        // mid-session; stay consistent from the first frame.
        setEnabled (! processor.isExportRunning());

        // Exports complete on a worker thread; this handler runs on the
        // message thread and swaps the progress card for the result.
        processor.onExportFinished = [safeThis = juce::Component::SafePointer<StretchExportButton> (this)]
            (bool success, juce::File exported, bool cancelled)
        {
            if (safeThis != nullptr)
                safeThis->exportCompleted (success, std::move (exported), cancelled);
        };
    }

    ~StretchExportButton() override
    {
        // The processor joins its export worker before dying, but the
        // progress card must not outlive the panel that owns the getter.
        closeProgressCard();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragArmed = true;
        dragStarted = false;
        TextButton::mouseDown (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragArmed && ! dragStarted
            && e.getDistanceFromDragStart() >= 6
            && processor.hasLoadedFile())
        {
            dragStarted = true;
            deferBeginExport (/*allowOsDrag=*/ true);
        }
        TextButton::mouseDrag (e);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragArmed && ! dragStarted && e.mouseWasClicked() && processor.hasLoadedFile())
            deferBeginExport (false);

        dragArmed = false;
        dragStarted = false;
        TextButton::mouseUp (e);
    }

private:
    // Dialogs must never be spawned while THIS button still owns the mouse
    // capture: a modal created inside a click/drag sequence can fail to
    // surface or take input on Windows. Hop to the next message-loop turn.
    void deferBeginExport (bool allowOsDrag)
    {
        juce::Component::SafePointer<StretchExportButton> safeThis { this };
        juce::MessageManager::callAsync ([safeThis, allowOsDrag]
        {
            if (safeThis != nullptr)
                safeThis->beginExport (allowOsDrag);
        });
    }

    void beginExport (bool allowOsDrag)
    {
        // One render at a time: the button is disabled while an export
        // runs, so reaching this branch is only possible via a stale async
        // hop — do nothing rather than stacking a second progress card.
        if (processor.isExportRunning())
            return;

        // Warn before rendering anything risky: freeze / very low rates
        // produce endless files (capped to two minutes), other settings may
        // simply produce a huge one.
        const bool lowRate = processor.isFrozen()
                             || std::abs (processor.getRateValue()) < 0.10f;
        const bool hugeFile = processor.estimateExportBytes()
                              > StretchAudioProcessor::kMaxExportBytes;

        if (lowRate || hugeFile)
        {
            allowOsDrag = false; // can't hold an OS drag across a dialog

            const juce::String title = lowRate ? ">> RATE BELOW 10 %" : ">> FILE SIZE";
            const juce::String message = lowRate
                ? "The current RATE may produce an extremely long file.\n"
                  "The export is capped to the first two minutes."
                : "The current settings may produce a very large file (> 100 MB).";

            juce::Component::SafePointer<StretchExportButton> safeThis { this };
            StretchExportDialogs::showWarning (title, message, this, [safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->doExport (false);
            });
            return;
        }

        doExport (allowOsDrag);
    }

    void doExport (bool allowOsDrag)
    {
        dragOnFinish = allowOsDrag;

        if (! processor.startBackgroundExport())
        {
            dragOnFinish = false;
            showError ("Failed to start the export.\nCheck disk space / permissions.");
            return;
        }

        setEnabled (false); // one render at a time; re-enabled on completion
        openProgressCard();
    }

private:
    void openProgressCard()
    {
        closeProgressCard();

        progressWindow = StretchExportDialogs::showProgress (
            ">> EXPORTING",
            [&processor = processor] { return processor.getExportProgress(); },
            [&processor = processor] { processor.cancelExport(); },
            this);
    }

    void closeProgressCard()
    {
        if (progressWindow != nullptr)
            progressWindow->closeButtonPressed(); // self-deletes the window
        progressWindow = nullptr;
    }

    void exportCompleted (bool success, const juce::File& exported, bool cancelled)
    {
        setEnabled (true);
        closeProgressCard();

        if (cancelled)
            return;

        if (! success)
        {
            showError ("Failed to render the export file.\nCheck disk space / permissions.");
            return;
        }

        if (dragOnFinish)
        {
            dragOnFinish = false;
            juce::DragAndDropContainer::performExternalDragDropOfFiles (
                { exported.getFullPathName() }, false, this);
            return;
        }

        StretchExportDialogs::showSavedPath (exported);
    }

    void showError (const juce::String& message)
    {
        StretchExportDialogs::openWindow (
            new StretchExportDialog (">> EXPORT", message, "CLOSE", {}),
            this, 460, 200);
    }

    StretchAudioProcessor& processor;
    bool dragArmed = false;
    bool dragStarted = false;
    bool dragOnFinish = false;

    // SafePointer: the window self-deletes when closed (button or close box),
    // and must never be touched again afterwards.
    juce::Component::SafePointer<juce::DocumentWindow> progressWindow;
};

// ---------------------------------------------------------------------------
// StretchVolumeSlider - top-right master gain readout: "-inf dB" at the
// range floor, "X dB" / "X.X dB" otherwise.
// ---------------------------------------------------------------------------
class StretchVolumeSlider : public juce::Slider
{
public:
    StretchVolumeSlider()
        : juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight)
    {
        setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 30);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    }

    juce::String getTextFromValue (double v) override
    {
        if (v <= getRange().getStart())
            return "-inf dB";
        if (std::abs (v - std::round (v)) < 0.001)
            return juce::String (juce::roundToInt (v)) + " dB";
        return juce::String (v, 1) + " dB";
    }

    double getValueFromText (const juce::String& text) override
    {
        return text.retainCharacters ("-+.0123456789").getDoubleValue();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchVolumeSlider)
};

class StretchTopPanel : public juce::Component,
                        private juce::Timer
{
public:
    StretchTopPanel (StretchAudioProcessor& p)
        : processor (p),
          exportButton (p)
    {
        menuButton.onClick = [this] { showMenu(); };
        addAndMakeVisible (menuButton);

        addAndMakeVisible (exportButton);

        // Master gain readout with the larger VT323 value box.
        addAndMakeVisible (volumeSlider);
        volumeSlider.setLookAndFeel (&volumeLookAndFeel);
        volumeAttachment = std::make_unique<juce::SliderParameterAttachment> (
            *p.parameters.getParameter ("OutputGain"), volumeSlider, nullptr);

        // Blinking terminal cursor for the STRETCH... banner.
        startTimerHz (2);
    }

    ~StretchTopPanel() override
    {
        stopTimer();
        // Component::setLookAndFeel does NOT take ownership (ERRATA.md).
        volumeSlider.setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        const float s = scaleFor (*this);

        const int margin = (int) (18.0f * s);
        const int text_width = bounds.getWidth() - margin * 2;
        const int line1_height = (int) (30.0f * s);
        const int line2_height = (int) (16.0f * s);
        const int block_height = line1_height + (int) (4.0f * s) + line2_height;

        // One shared centre line for every header element (see resized()).
        const int cy = bounds.getCentreY();
        const int block_y = cy - block_height / 2;

        const juce::String banner = "STRETCH...";

        g.setColour (GUI::Color::Logo);
        g.setFont (StretchLookAndFeel::makeFont (40.0f * s));
        g.drawText (banner, margin, block_y, text_width, line1_height,
                    juce::Justification::centredLeft, false);

        // Blinking block cursor after the banner text.
        if (cursorVisible)
        {
            const auto bannerFont = StretchLookAndFeel::makeFont (40.0f * s);
            const float bannerWidth = (float) juce::GlyphArrangement::getStringWidthInt (bannerFont, banner);
            g.setColour (GUI::Color::Logo.withAlpha (0.85f));
            g.fillRect ((float) margin + bannerWidth + 8.0f * s, (float) block_y,
                        14.0f * s, (float) line1_height);
        }

        g.setColour (GUI::Color::Logo.withAlpha (0.55f));
        g.setFont (StretchLookAndFeel::makeFont (17.0f * s));
        g.drawText (juce::String ("PITCH & TIME STRETCH // v") + PLUGIN_VERSION,
                    margin, block_y + line1_height + 4, text_width, line2_height,
                    juce::Justification::centredLeft, false);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        const float s = scaleFor (*this);
        const int cy = bounds.getCentreY(); // shared centre line with paint()

        const int exportW = (int) (130.0f * s);
        const int menuW   = (int) (56.0f * s);
        const int ctrlH   = (int) (36.0f * s);
        const int gap     = (int) (10.0f * s);

        const int clusterW = exportW + gap + menuW;
        const int clusterX = (bounds.getWidth() - clusterW) / 2;

        exportButton.setBounds (clusterX,                 cy - ctrlH / 2, exportW, ctrlH);
        menuButton.setBounds   (clusterX + exportW + gap, cy - ctrlH / 2, menuW,   ctrlH);

        // Master gain readout: right zone, on the same centre line. Taller
        // than the visible track so the L&F can trim the value box's bottom
        // and optically centre its text against the track.
        const int volW = juce::jmin ((int) (300.0f * s), bounds.getWidth() / 3);
        const int volH = (int) (42.0f * s);
        volumeSlider.setBounds (bounds.getWidth() - volW - (int) (18.0f * s),
                                cy - volH / 2,
                                volW, volH);
    }

    // Zoom hook (editor's applyScaledFonts): the master gain textbox carries
    // an explicit size/font, so it is rebuilt at the new zoom here.
    void applyFontScale (float scale)
    {
        volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false,
                                      juce::roundToInt (84.0f * scale),
                                      juce::roundToInt (30.0f * scale));
    }

    std::function<void()> onSetExportFolder;
    std::function<void()> onViewStateChanged;

private:
    void timerCallback() override
    {
        cursorVisible = ! cursorVisible;
        repaint();
    }

    bool cursorVisible = true;

    void displayAboutPopup()
    {
        // About layout: version, one-line descriptor, then a "Based on:" block
        // with aligned "-- licence" columns. Lines stay short — dialog body
        // Labels do not word-wrap (ROADMAP §2 constraint 2).
        const juce::String aboutMessage =
            "Version " + PLUGIN_VERSION
            + "\n\nA pitch & time stretch plugin"
            + "\nBy BalamDSP"
            + "\n\nBased on:"
            + "\n  JUCE framework       -- JUCE Ltd (AGPL)"
            + "\n  Signalsmith Stretch  -- Geraint Luff (MIT)"
            + "\n  cool-retro-term      -- Swordfish90 (GPL)"
            + "\n  VT323 typeface       -- Peter Hull (OFL)"
            + "\n\n>> SHORTCUTS"
            + "\nSPACE play/pause   L loop   F freeze   R reverse"
            + "\nWave: wheel zooms, wheel-X scrolls,"
            + "\nSHIFT+drag selects the loop region,"
            + "\nALT+click clears it, double-click resets.";

        StretchExportDialogs::openWindow (
            new StretchExportDialog (">> ABOUT STRETCH", aboutMessage, "CLOSE", {}),
            nullptr, 500, 600);
    }

    void showMenu()
    {
        juce::PopupMenu menu;

        menu.addItem (1, "Set Export Folder...");
        menu.addItem (2, "Reset Export Folder");
        menu.addItem (11, "Export Options...");
        menu.addSeparator();
        menu.addItem (3, juce::String ("CRT Enabled - ") + (processor.isCrtEnabled() ? "[X]" : "[ ]"));
        menu.addSeparator();

        // UI zoom: sets the persisted "ui_scale" choice param; the editor
        // re-sizes itself to STRETCH_PANEL * uiScale on the change.
        {
            juce::PopupMenu zoomSub;
            const int count = (int) StretchZoom::ZOOM_PERCENTS.size();

            int currentIdx = 0;
            if (auto* scaleParam = processor.parameters.getParameter (StretchZoom::UI_SCALE_ID))
                currentIdx = juce::roundToInt (scaleParam->getValue() * (count - 1));
            currentIdx = juce::jlimit (0, count - 1, currentIdx);

            for (int i = 0; i < count; ++i)
                zoomSub.addItem (kZoomBaseId + i,
                                 juce::String (StretchZoom::ZOOM_PERCENTS[i]) + "%",
                                 true, i == currentIdx);

            menu.addSubMenu (">> UI Scale", zoomSub);
        }
        menu.addSeparator();

        // Recent files: global list, shared by every format/instance.
        // Paths are shown inline, truncated from the left so the filename
        // stays visible (JUCE 9 popup items have no hover-help support).
        recentPaths = processor.getRecentFiles();
        if (! recentPaths.isEmpty())
        {
            juce::PopupMenu recentSub;

            for (int i = 0; i < recentPaths.size(); ++i)
            {
                const juce::String& path = recentPaths[i];
                const juce::String label = (path.length() <= 52)
                    ? path
                    : ("..." + path.substring (path.length() - 49));

                recentSub.addItem (kRecentBaseId + i, label,
                                   true,
                                   path == processor.getSourceFile().getFullPathName());
            }

            recentSub.addSeparator();
            recentSub.addItem (kRecentClearId, "Clear List");
            menu.addSubMenu (">> Recent Files...", recentSub);
            menu.addSeparator();
        }

        // Presets: global XML folder + save-as entry.
        presetPaths = StretchPresets::listPresets();
        {
            juce::PopupMenu presetSub;

            for (int i = 0; i < presetPaths.size(); ++i)
                presetSub.addItem (kPresetBaseId + i,
                                   presetPaths[i].getFileNameWithoutExtension());

            if (! presetPaths.isEmpty())
                presetSub.addSeparator();

            presetSub.addItem (kPresetSaveAsId, "Save Preset As...");
            menu.addSubMenu (">> Presets", presetSub);
            menu.addSeparator();
        }

        // Runtime gate: plugin sources compile once for all formats, so a
        // preprocessor guard cannot tell them apart -- wrapperType can.
        if (processor.isRunningAsStandalone())
        {
            juce::PopupMenu standaloneSub;
            standaloneSub.addItem (4, "Audio/MIDI Settings...");
            standaloneSub.addItem (5, "Save State...");
            standaloneSub.addItem (6, "Load State...");
            standaloneSub.addItem (7, "Reset to Default");
            menu.addSubMenu (">> Standalone", standaloneSub);
            menu.addSeparator();
        }

        menu.addItem (10, "About");

        menu.showMenuAsync (
            juce::PopupMenu::Options()
                .withTargetComponent (&menuButton),
            [this] (int result)
            {
                handleMenuResult (result);
            });
    }

    void handleMenuResult (int selectedId)
    {
        // Standalone-only items can never be triggered in a host (the
        // submenu is hidden), but guard anyway.
        if (selectedId >= 4 && selectedId <= 7 && ! processor.isRunningAsStandalone())
            return;

        if (selectedId == kRecentClearId)
        {
            processor.clearRecentFiles();
            return;
        }

        // UI scale choice: write the normalized param value; the editor's
        // parameterChanged listener applies the zoom.
        const int zoomIndex = selectedId - kZoomBaseId;
        if (zoomIndex >= 0 && zoomIndex < (int) StretchZoom::ZOOM_PERCENTS.size())
        {
            if (auto* scaleParam = processor.parameters.getParameter (StretchZoom::UI_SCALE_ID))
                scaleParam->setValueNotifyingHost (
                    (float) zoomIndex / (float) (StretchZoom::ZOOM_PERCENTS.size() - 1));
            return;
        }

        const int recentIndex = selectedId - kRecentBaseId;
        if (recentIndex >= 0 && recentIndex < recentPaths.size())
        {
            const juce::File f (recentPaths[recentIndex]);
            if (f.existsAsFile())
                processor.loadAudioFile (f);
            return;
        }

        if (selectedId == kPresetSaveAsId)
        {
            savePresetAsDialog();
            return;
        }

        const int presetIndex = selectedId - kPresetBaseId;
        if (presetIndex >= 0 && presetIndex < presetPaths.size())
        {
            StretchPresets::ViewState vs;
            StretchPresets::load (presetPaths[presetIndex], processor.parameters, vs);
            processor.setWaveViewStart (vs.viewStart);
            processor.setWaveViewLen (vs.viewLen);
            processor.setLoopRegion (vs.loopStart, vs.loopEnd);
            if (onViewStateChanged)
                onViewStateChanged();
            return;
        }

        switch (selectedId)
        {
            case 1:
                if (onSetExportFolder)
                    onSetExportFolder();
                break;
            case 2:
                processor.setExportFolder (juce::File());
                break;
            case 3:
                processor.setCrtEnabled (! processor.isCrtEnabled());
                break;
            case 11:
                showExportOptionsDialog();
                break;
           #if JUCE_STANDALONE_APPLICATION
            // The custom standalone window is only visible to standalone
            // builds (see includes at top of this file); the runtime
            // wrapper check makes these unreachable in hosts.
            case 4:
                if (auto* tl = getTopLevelComponent())
                    if (auto* sfw = dynamic_cast<juce::StretchFilterWindow*> (tl))
                        new SettingsWindow (sfw->getPluginHolder()->deviceManager);
                break;
            case 5:
                if (auto* tl = getTopLevelComponent())
                    if (auto* sfw = dynamic_cast<juce::StretchFilterWindow*> (tl))
                        sfw->getPluginHolder()->askUserToSaveState();
                break;
            case 6:
                if (auto* tl = getTopLevelComponent())
                    if (auto* sfw = dynamic_cast<juce::StretchFilterWindow*> (tl))
                        sfw->getPluginHolder()->askUserToLoadState();
                break;
            case 7:
            {
                // Deferred: resetToDefaultState() destroys/recreates the editor
                // synchronously; never do that from inside this menu callback
                // (which is owned by the component tree being replaced).
                juce::Component::SafePointer<StretchTopPanel> safeThis { this };
                juce::MessageManager::callAsync ([safeThis]
                {
                    if (safeThis == nullptr)
                        return;
                    if (auto* tl = safeThis->getTopLevelComponent())
                        if (auto* sfw = dynamic_cast<juce::StretchFilterWindow*> (tl))
                            sfw->resetToDefaultState();
                });
                break;
            }
           #endif
            case 10:
                displayAboutPopup();
                break;
            default:
                break;
        }
    }

    // "SAVE PRESET AS..." -> name-entry card -> XML in the global presets
    // folder. Errors surface as a themed card, never an AlertWindow.
    void savePresetAsDialog()
    {
        juce::Component::SafePointer<StretchTopPanel> safeThis { this };
        StretchExportDialogs::askForName (">> SAVE PRESET", {}, this,
            [safeThis] (const juce::String& name)
            {
                if (safeThis == nullptr || name.trim().isEmpty())
                    return;

                StretchPresets::ViewState views;
                views.viewStart = safeThis->processor.getWaveViewStart();
                views.viewLen   = safeThis->processor.getWaveViewLen();
                views.loopStart = safeThis->processor.getLoopStart();
                views.loopEnd   = safeThis->processor.getLoopEnd();

                if (! StretchPresets::save (safeThis->processor.parameters, name, views))
                    StretchExportDialogs::openWindow (
                        new StretchExportDialog (">> PRESETS",
                            "Failed to save the preset.\nCheck disk space / permissions.",
                            "CLOSE", {}),
                        nullptr, 460, 200);
            });
    }

    // Global export options card; choices persist system-wide and are read
    // by the next background render.
    void showExportOptionsDialog()
    {
        const auto opts = StretchAudioProcessor::getExportOptions();

        StretchExportDialogs::showExportOptions (
            opts.sampleRate, opts.bitDepth, (int) opts.format, this,
            [] (int rate, int depth, int format)
            {
                StretchAudioProcessor::ExportOptions updated;
                updated.sampleRate = rate;
                updated.bitDepth = depth;
                updated.format = (StretchAudioProcessor::ExportFormat) format;
                StretchAudioProcessor::setExportOptions (updated);
            });
    }

    // Menu item ids: fixed actions stay low; dynamic lists live in blocks.
    static constexpr int kRecentBaseId = 100;
    static constexpr int kPresetBaseId = 200;
    static constexpr int kPresetSaveAsId = 300;
    static constexpr int kRecentClearId = 400;
    static constexpr int kZoomBaseId = 500;

    juce::StringArray recentPaths;      // snapshot taken when the menu opened
    juce::Array<juce::File> presetPaths;

    StretchAudioProcessor& processor;
    StretchHamburgerButton menuButton;
    StretchExportButton exportButton;

    // Bigger VT323 readout for the master gain box only. getFont() reads the
    // current Zoom::uiScale, so the value box font follows the zoom even when
    // the textbox is rebuilt (applyFontScale).
    struct VolumeLookAndFeel : public StretchLookAndFeel
    {
        juce::Font getLabelFont (juce::Label&) override { return getFont (26.0f); }

        juce::Label* createSliderTextBox (juce::Slider& slider) override
        {
            auto* l = StretchLookAndFeel::createSliderTextBox (slider);
            l->setFont (getFont (26.0f));
            return l;
        }
    };

    StretchVolumeSlider volumeSlider;
    VolumeLookAndFeel volumeLookAndFeel;

    std::unique_ptr<juce::SliderParameterAttachment> volumeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchTopPanel)
};
