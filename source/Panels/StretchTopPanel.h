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

// Boxed button with three hand-drawn bars (no U+2630 glyph in VT323).
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

// Drag out the rendered file, or click for a "saved to..." popup. Low RATE
// can't pause mid-drag, so it falls back to the click confirm flow.
// Bracket-button paint shared with the preset display (same font sizing).
class StretchExportButton : public StretchFxTextButton
{
public:
    explicit StretchExportButton (StretchAudioProcessor& proc)
        : processor (proc)
    {
        setButtonText ("EXPORT");

        // May already be running when the editor is recreated mid-session.
        setEnabled (! processor.isExportRunning());

        // Completion arrives on the message thread.
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
    // Never spawn dialogs while this button holds mouse capture (breaks on
    // Windows); hop to the next message-loop turn.
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
        // One render at a time; a stale async hop reaching here does nothing.
        if (processor.isExportRunning())
            return;

        // Warn before risky renders: freeze/low rates cap at two minutes,
        // other settings may just be huge.
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
            processor.hasActiveSelection() ? ">> EXPORTING SELECTION" : ">> EXPORTING",
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

    // SafePointer: the window self-deletes on close.
    juce::Component::SafePointer<juce::DocumentWindow> progressWindow;
};

// Master gain readout: "-inf dB" at the floor, else "X [X] dB".
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

        presetDisplay.setButtonText (processor.presetSession.name);
        presetDisplay.onClick = [this] { showPresetsMenu(); };
        addAndMakeVisible (presetDisplay);

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

        const int presetW = (int) (150.0f * s);
        const int exportW = (int) (100.0f * s);
        const int menuW   = (int) (56.0f * s);
        const int ctrlH   = (int) (36.0f * s);
        const int gap     = (int) (10.0f * s);

        const int clusterW = presetW + gap + exportW + gap + menuW;
        const int clusterX = (bounds.getWidth() - clusterW) / 2;

        presetDisplay.setBounds (clusterX,                              cy - ctrlH / 2, presetW, ctrlH);
        exportButton.setBounds  (clusterX + presetW + gap,              cy - ctrlH / 2, exportW, ctrlH);
        menuButton.setBounds    (clusterX + presetW + gap + exportW + gap, cy - ctrlH / 2, menuW, ctrlH);

        // Right zone, same centre line. Taller than the track so the L&F can
        // trim the value box bottom and centre its text on the track.
        // 260px wide: clears the narrowed center cluster with room to spare.
        const int volW = juce::jmin ((int) (260.0f * s), bounds.getWidth() / 3);
        const int volH = (int) (42.0f * s);
        volumeSlider.setBounds (bounds.getWidth() - volW - (int) (18.0f * s),
                                cy - volH / 2,
                                volW, volH);
    }

    // Zoom hook: the gain textbox has an explicit size/font; rebuild it here.
    void applyFontScale (float scale)
    {
        volumeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false,
                                      juce::roundToInt (84.0f * scale),
                                      juce::roundToInt (30.0f * scale));
    }

    std::function<void()> onSetExportFolder;
    std::function<void()> onViewStateChanged;
    std::function<void()> onSampleUnloaded;
    std::function<void (int)> onCrtStrengthChanged;

private:
    void timerCallback() override
    {
        cursorVisible = ! cursorVisible;

        // Preset display follows the session (menu actions update it).
        const auto& presetName = processor.presetSession.name;
        if (presetDisplay.getButtonText() != presetName)
            presetDisplay.setButtonText (presetName);

        repaint();
    }

    bool cursorVisible = true;

    void displayAboutPopup()
    {
        // Lines stay short: dialog body labels do not word-wrap.
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

    // Preset-only popup for the name display (not the full hamburger menu).
    void showPresetsMenu()
    {
        presetPaths = StretchPresets::listPresets();

        juce::PopupMenu presetSub;
        buildPresetSubmenu (presetSub);
        presetSub.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (&presetDisplay),
            [this] (int result)
            {
                handleMenuResult (result);
            });
    }

    // Action cluster + recursive list; presetPaths must be fresh (see callers).
    void buildPresetSubmenu (juce::PopupMenu& presetSub)
    {
        presetSub.addItem (kPresetInitId, "Init");
        presetSub.addItem (kPresetSaveId, "Save");
            presetSub.addItem (kPresetSaveAsId, "Save As...");
        presetSub.addItem (kPresetLoadFileId, "Load From File...");
        presetSub.addSeparator();

        addPresetLevel (presetSub, StretchPresets::presetsFolder());
    }

    void showMenu (juce::Component* anchor = nullptr)
    {
        juce::PopupMenu menu;

        menu.addItem (1, "Set Export Folder...");
        menu.addItem (2, "Reset Export Folder");
        menu.addItem (11, "Export Options...");
        menu.addSeparator();

        menu.addItem (12, "Set Preset Folder...");
        menu.addItem (13, "Reset Preset Folder");
        menu.addSeparator();

        // CRT Layout: enabled toggle + >> Strength submenu; both persist live.
        {
            juce::PopupMenu crtSub;
            crtSub.addItem (3, juce::String ("CRT Enabled - ")
                            + (processor.isCrtEnabled() ? "[X]" : "[ ]"));

            juce::PopupMenu strengthSub;
            const int strength = processor.getCrtStrength();
            strengthSub.addItem (kCrtStrengthBaseId + 0, "Low",  true, strength == 0);
            strengthSub.addItem (kCrtStrengthBaseId + 1, "Medium", true, strength == 1);
            strengthSub.addItem (kCrtStrengthBaseId + 2, "High", true, strength == 2);
            crtSub.addSubMenu (">> Strength", strengthSub);

            menu.addSubMenu (">> CRT Layout", crtSub);
        }

        // Zoom: writes the persisted "ui_scale" param; the editor resizes.
        {
            juce::PopupMenu zoomSub;
            const int count = (int) StretchZoom::ZOOM_PERCENTS.size();

            int currentIdx = 0;
            if (auto* scaleParam = processor.parameters.getParameter (StretchZoom::UI_SCALE_ID))
                currentIdx = juce::roundToInt (scaleParam->getValue() * (count - 1));
            currentIdx = juce::jlimit (0, count - 1, currentIdx);

            for (int i = 0; i < count; ++i)
                zoomSub.addItem (kZoomBaseId + i,
                                 juce::String (StretchZoom::ZOOM_PERCENTS
                                     [static_cast<size_t> (i)]) + "%",
                                 true, i == currentIdx);

            menu.addSubMenu (">> Zoom", zoomSub);
        }
        menu.addSeparator();

        // Recent files: global list; truncated from the left so names show.
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
        }

        presetPaths = StretchPresets::listPresets();
        {
            juce::PopupMenu presetSub;
            buildPresetSubmenu (presetSub);
            menu.addSubMenu (">> Presets", presetSub);
            menu.addSeparator();
        }

        // Preprocessor can't tell formats apart (one binary); wrapperType can.
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
                .withTargetComponent (anchor != nullptr ? anchor : &menuButton),
            [this] (int result)
            {
                handleMenuResult (result);
            });
    }

    void handleMenuResult (int selectedId)
    {
        // Hidden in hosts, but guard anyway.
        if (selectedId >= 4 && selectedId <= 7 && ! processor.isRunningAsStandalone())
            return;

        if (selectedId == kRecentClearId)
        {
            processor.clearRecentFiles();
            return;
        }

        // Zoom writes the normalized value; the editor listener applies it.
        const int zoomIndex = selectedId - kZoomBaseId;
        if (zoomIndex >= 0 && zoomIndex < (int) StretchZoom::ZOOM_PERCENTS.size())
        {
            if (auto* scaleParam = processor.parameters.getParameter (StretchZoom::UI_SCALE_ID))
                scaleParam->setValueNotifyingHost (
                    (float) zoomIndex / (float) (StretchZoom::ZOOM_PERCENTS.size() - 1));
            return;
        }

        // Persists to settings.xml; overlay applies it via the callback.
        const int crtStrengthIdx = selectedId - kCrtStrengthBaseId;
        if (crtStrengthIdx >= 0 && crtStrengthIdx < 3)
        {
            processor.setCrtStrength (crtStrengthIdx);
            if (onCrtStrengthChanged)
                onCrtStrengthChanged (crtStrengthIdx);
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

        if (selectedId == kPresetInitId)
        {
            initPresetDialog();
            return;
        }

        if (selectedId == kPresetSaveId)
        {
            savePreset();
            return;
        }

        if (selectedId == kPresetLoadFileId)
        {
            loadPresetFileDialog();
            return;
        }

        const int presetIndex = selectedId - kPresetBaseId;
        if (presetIndex >= 0 && presetIndex < presetPaths.size())
        {
            loadPresetFile (presetPaths[presetIndex]);
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
            case 12:
                setPresetFolderDialog();
                break;
            case 13:
                StretchSettings::resetPresetDirectory();
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
                // Deferred: resetting destroys this editor synchronously,
                // never from inside this menu callback.
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

    void setPresetFolderDialog()
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Select Preset Folder",
            StretchPresets::presetsFolder(),
            "");

        juce::Component::SafePointer<StretchTopPanel> safeThis { this };
        chooser->launchAsync (
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectDirectories,
            [safeThis, chooser] (const juce::FileChooser& fc)
            {
                if (safeThis == nullptr)
                    return;
                const juce::File result = fc.getResult();
                if (result.isDirectory())
                    StretchSettings::setPresetDirectory (result);
            });
    }

    StretchPresets::ViewState currentViewState() const
    {
        return { processor.getWaveViewStart(), processor.getWaveViewLen(),
                 processor.getLoopStart(), processor.getLoopEnd() };
    }

    void applyViewState (const StretchPresets::ViewState& vs)
    {
        processor.setWaveViewStart (vs.viewStart);
        processor.setWaveViewLen (vs.viewLen);
        processor.setLoopRegion (vs.loopStart, vs.loopEnd);
        if (onViewStateChanged)
            onViewStateChanged();
    }

    // SAVE PRESET AS card; errors show as a themed card.
    void savePresetAsDialog()
    {
        juce::Component::SafePointer<StretchTopPanel> safeThis { this };
        StretchExportDialogs::askForName (">> SAVE PRESET", "Enter a name for your preset:",
                                          {}, this,
            [safeThis] (const juce::String& name)
            {
                if (safeThis == nullptr || name.trim().isEmpty())
                    return;

                const auto& views = safeThis->currentViewState();
                juce::String sessionName;
                juce::File sessionFile;
                if (! StretchPresets::saveAs (safeThis->processor.parameters, name, views,
                                              sessionName, sessionFile,
                                              safeThis->processor.getSourceFile().getFullPathName()))
                {
                    StretchExportDialogs::openWindow (
                        new StretchExportDialog (">> PRESETS",
                            "Failed to save the preset.\nCheck disk space / permissions.",
                            "CLOSE", {}),
                        nullptr, 460, 200);
                    return;
                }

                safeThis->processor.presetSession.name = sessionName;
                safeThis->processor.presetSession.file = sessionFile;
            });
    }

    // Save: overwrite the current file, or Save-As when there is none.
    void savePreset()
    {
        const auto& sessionFile = processor.presetSession.file;
        if (sessionFile.getFullPathName().isNotEmpty())
        {
            if (! StretchPresets::saveToFile (sessionFile, processor.parameters,
                                              currentViewState(),
                                              processor.getSourceFile().getFullPathName()))
                StretchExportDialogs::openWindow (
                    new StretchExportDialog (">> PRESETS",
                        "Failed to save the preset.\nCheck disk space / permissions.",
                        "CLOSE", {}),
                    nullptr, 460, 200);
            return;
        }

        savePresetAsDialog();
    }

    // Init confirm card; full reset on INIT.
    void initPresetDialog()
    {
        auto* dialog = new StretchExportDialog (">> INIT",
            "Reset all parameters to defaults?",
            "INIT", "CANCEL");
        juce::Component::SafePointer<StretchTopPanel> safeThis { this };
        dialog->onConfirm = [safeThis]
        {
            if (safeThis == nullptr)
                return;
            StretchPresets::initDefaults (safeThis->processor.parameters);
            safeThis->processor.unloadSample();
            safeThis->processor.setLoopRegion (0.0, 1.0);
            safeThis->processor.setWaveViewStart (0.0);
            safeThis->processor.setWaveViewLen (1.0);
            safeThis->applyViewState (safeThis->currentViewState());
            if (safeThis->onSampleUnloaded)
                safeThis->onSampleUnloaded();
            safeThis->processor.presetSession.name = "Untitled";
            safeThis->processor.presetSession.file = juce::File();
        };
        StretchExportDialogs::openWindow (dialog, this, 500, 240);
    }

    // Full preset recall: params + view/selection + the stored sample.
    // Missing audio skips silently; params/view still apply.
    void loadPresetFile (const juce::File& file)
    {
        StretchPresets::ViewState vs;
        juce::String sourcePath;
        StretchPresets::load (file, processor.parameters, vs, sourcePath);
        applyViewState (vs);

        const juce::File audio (sourcePath);
        if (audio.existsAsFile())
            processor.loadAudioFilePreservingView (audio);

        processor.presetSession.name = file.getFileNameWithoutExtension();
        processor.presetSession.file = file;
    }

    // File browser for a single .xml preset (any folder, not just presets).
    void loadPresetFileDialog()
    {
        auto chooser = std::make_shared<juce::FileChooser> (
            "Load Preset File",
            StretchPresets::presetsFolder(),
            "*.xml");

        juce::Component::SafePointer<StretchTopPanel> safeThis { this };
        chooser->launchAsync (juce::FileBrowserComponent::openMode
                                | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser] (const juce::FileChooser& fc)
            {
                if (safeThis == nullptr)
                    return;
                const auto result = fc.getResult();
                if (result.existsAsFile())
                    safeThis->loadPresetFile (result);
            });
    }

    // Folders of presetPaths under dir become ">> name" submenus (walked
    // in list order, so IDs stay plain indices into presetPaths).
    void addPresetLevel (juce::PopupMenu& parent, const juce::File& dir)
    {
        juce::Array<juce::File> subdirs;
        for (int i = 0; i < presetPaths.size(); ++i)
        {
            const auto& f = presetPaths[i];
            if (! f.isAChildOf (dir))
                continue;
            if (f.getParentDirectory() == dir)
            {
                parent.addItem (kPresetBaseId + i, f.getFileNameWithoutExtension());
            }
            else
            {
                auto child = f.getParentDirectory();
                while (child.getParentDirectory() != dir)
                    child = child.getParentDirectory();
                if (! subdirs.contains (child))
                    subdirs.add (child);
            }
        }
        for (auto& sub : subdirs)
        {
            juce::PopupMenu sm;
            addPresetLevel (sm, sub);
            parent.addSubMenu (">> " + sub.getFileName(), sm);
        }
    }

    // Global export options; read by the next background render.
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

    // Preset actions ride the fixed low zone (1-13 taken by other actions).
    static constexpr int kPresetInitId = 14;
    static constexpr int kPresetSaveId = 15;
    static constexpr int kPresetLoadFileId = 16;

    // Menu item ids: preset block is open-ended (subfolders), the rest are
    // small fixed blocks with wide gaps so ranges never overlap.
    static constexpr int kRecentBaseId = 100;
    static constexpr int kPresetBaseId = 1000;
    static constexpr int kPresetSaveAsId = 10000;
    static constexpr int kRecentClearId = 11000;
    static constexpr int kZoomBaseId = 12000;
    static constexpr int kCrtStrengthBaseId = 30;  // 30/31/32 = Low/Medium/High

    juce::StringArray recentPaths;      // snapshot taken when the menu opened
    juce::Array<juce::File> presetPaths;

    StretchAudioProcessor& processor;
    StretchHamburgerButton menuButton;
    StretchFxTextButton presetDisplay;
    StretchExportButton exportButton;

    // Larger VT323 readout for the gain box; follows zoom via getFont().
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
