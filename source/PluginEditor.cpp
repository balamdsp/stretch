#include "PluginEditor.h"

// X11 standalone: _NET_FRAME_EXTENTS for outer sizing. Xlib TU is separate
// (Xlib headers clash with JUCE); queried rarely (zoom + window settle).
extern "C" int stretchGetFrameExtents (unsigned long windowH,
                                       int* outFrameW, int* outFrameH);

juce::Point<int> StretchAudioProcessorEditor::getNativeFrameSize (juce::Component* topLevelWindow)
{
    if (topLevelWindow != nullptr)
        if (auto* peer = topLevelWindow->getPeer())
            if (peer->getNativeHandle() != nullptr)
            {
                int frameW = 0, frameH = 0;

                if (stretchGetFrameExtents ((unsigned long) peer->getNativeHandle(),
                                            &frameW, &frameH) != 0)
                    return { frameW, frameH };
            }

    return {};
}

StretchAudioProcessorEditor::StretchAudioProcessorEditor (StretchAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      screen (p),
      crtOverlay (&screen, &p.getCrtEnabledFlag())
{
    static StretchLookAndFeel stretchLookAndFeel;
    setLookAndFeel (&stretchLookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&stretchLookAndFeel);

    addAndMakeVisible (screen);
    addAndMakeVisible (crtOverlay);
    crtOverlay.toFront (false);

    // CRT strength is machine-wide; apply the stored choice on recreate.
    crtOverlay.setCrtStrength (processor.getCrtStrength());

    screen.mainPanel.waveform.onFileDropped = [this] (const juce::File& file)
    {
        onFileDropped (file);
    };

    screen.topPanel.onSetExportFolder = [this]
    {
        onSetExportFolder();
    };

    screen.topPanel.onCrtStrengthChanged = [safeThis = juce::Component::SafePointer<StretchAudioProcessorEditor> (this)] (int)
    {
        if (safeThis == nullptr)
            return;
        // The processor already persisted the choice; apply it to the overlay.
        safeThis->crtOverlay.setCrtStrength (safeThis->processor.getCrtStrength());
    };

    screen.topPanel.onViewStateChanged = [this]
    {
        screen.mainPanel.waveform.restoreFromProcessor();
    };

    screen.topPanel.onSampleUnloaded = [this]
    {
        screen.mainPanel.waveform.clearBuffer();
    };

    // Loads finish off-thread; SafePointer guards editor death mid-flight.
    processor.onFileLoaded = [safeThis = juce::Component::SafePointer<StretchAudioProcessorEditor> (this)]
    {
        if (safeThis == nullptr)
            return;

        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr || ! safeThis->processor.hasLoadedFile())
                return;

            safeThis->screen.mainPanel.waveform.setBuffer (
                safeThis->processor.getOriginalBuffer(),
                safeThis->processor.getFileSampleRate());
            safeThis->screen.mainPanel.waveform.restoreFromProcessor();
        });
    };

    // Decode failures arrive on the message thread; surface them.
    processor.onFileLoadFailed = [safeThis = juce::Component::SafePointer<StretchAudioProcessorEditor> (this)]
                                 (const juce::String& reason)
    {
        if (safeThis == nullptr)
            return;

        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Couldn't open audio file",
                                                reason);
    };

    // Editor may postdate the load (host recreates UI); seed from live buffer.
    if (processor.hasLoadedFile())
    {
        screen.mainPanel.waveform.setBuffer (processor.getOriginalBuffer(),
                                             processor.getFileSampleRate());
        screen.mainPanel.waveform.restoreFromProcessor();
    }

    // State restore for an already-loaded file bypasses the async load path.
    processor.onViewStateRestored = [safeThis = juce::Component::SafePointer<StretchAudioProcessorEditor> (this)]
    {
        if (safeThis == nullptr)
            return;
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis == nullptr)
                return;
            safeThis->screen.mainPanel.waveform.restoreFromProcessor();
        });
    };

    setSize (STRETCH_PANEL_WIDTH, STRETCH_PANEL_HEIGHT);

    // Window always STRETCH_PANEL * uiScale; zoom menu is the only resizer.
    if (auto* scaleParam = processor.parameters.getParameter (StretchZoom::UI_SCALE_ID))
    {
        const float f = scaleParam->getValue()
                        * (float) (StretchZoom::ZOOM_PERCENTS.size() - 1);
        const int idx = juce::roundToInt (f);
        uiScale = StretchZoom::ZOOM_PERCENTS [static_cast<size_t> (juce::jlimit (0,
            (int) StretchZoom::ZOOM_PERCENTS.size() - 1, idx))] / 100.0f;
    }

    processor.parameters.addParameterListener (StretchZoom::UI_SCALE_ID, this);

    updateZoomLimits();
    applyZoom (uiScale);

    // Standalone startup race: the wrapper clamps back to resize minimum,
    // discarding this zoom. Re-assert once settled.
    if (processor.isRunningAsStandalone())
        juce::Timer::callAfterDelay (250, [safeThis = juce::Component::SafePointer<StretchAudioProcessorEditor> (this)]
        {
            if (safeThis != nullptr)
                safeThis->applyZoom (safeThis->uiScale);
        });
}

StretchAudioProcessorEditor::~StretchAudioProcessorEditor()
{
    processor.parameters.removeParameterListener (StretchZoom::UI_SCALE_ID, this);

    // Detach callbacks so late workers can't paint a dead UI (reopen seeds).
    processor.onFileLoaded = nullptr;
    processor.onFileLoadFailed = nullptr;
    processor.onViewStateRestored = nullptr;
}

void StretchAudioProcessorEditor::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID != StretchZoom::UI_SCALE_ID)
        return;

    auto* scaleParam = processor.parameters.getParameter (StretchZoom::UI_SCALE_ID);
    if (scaleParam == nullptr)
        return;

    const int idx = juce::roundToInt (scaleParam->getValue()
                                      * (float) (StretchZoom::ZOOM_PERCENTS.size() - 1));
    applyZoom (StretchZoom::ZOOM_PERCENTS [static_cast<size_t> (juce::jlimit (0,
        (int) StretchZoom::ZOOM_PERCENTS.size() - 1, idx))] / 100.0f);
}

void StretchAudioProcessorEditor::applyZoom (float scale)
{
    scale = juce::jlimit (Zoom::Min, Zoom::Max, scale);
    uiScale = scale;
    Zoom::uiScale = scale;

    applyScaledFonts();
    updateZoomLimits();

    const int pixW = juce::roundToInt (Zoom::BaseW * uiScale);
    const int pixH = juce::roundToInt (Zoom::BaseH * uiScale);

    // X11: frame lives inside the outer window, so add it back; the client
    // then lands exactly on the editor size (peer frame size stays empty).
    const auto frame = getNativeFrameSize (processor.isRunningAsStandalone()
                                           ? getTopLevelComponent() : nullptr);

    const int outerW = juce::jmax (1, pixW + frame.x);
    const int outerH = juce::jmax (1, pixH + frame.y);

    // Refresh the window pin BEFORE resizing (WM publishes pinned min==max).
    if (processor.isRunningAsStandalone())
        if (auto* tl = getTopLevelComponent())
            if (tl != this)
                if (auto* rw = dynamic_cast<juce::ResizableWindow*> (tl))
                    if (auto* c = rw->getConstrainer())
                        c->setSizeLimits (outerW, outerH, outerW, outerH);

    setSize (pixW, pixH);

    // Follow the editor size exactly (outer == editor + frame).
    if (processor.isRunningAsStandalone())
        if (auto* tl = getTopLevelComponent())
            if (tl != this)
                tl->setSize (outerW, outerH);

    resized();
    repaint();
}

void StretchAudioProcessorEditor::updateZoomLimits()
{
    // Standalone: pin constrainer to the zoom size (edge-drag clamp + WM
    // hints). Hosts keep the zoom range for host-managed sizing.
    if (processor.isRunningAsStandalone())
        setResizeLimits (juce::roundToInt (Zoom::BaseW * uiScale),
                         juce::roundToInt (Zoom::BaseH * uiScale),
                         juce::roundToInt (Zoom::BaseW * uiScale),
                         juce::roundToInt (Zoom::BaseH * uiScale));
    else
        setResizeLimits (juce::roundToInt (Zoom::BaseW * Zoom::Min),
                         juce::roundToInt (Zoom::BaseH * Zoom::Min),
                         juce::roundToInt (Zoom::BaseW * Zoom::Max),
                         juce::roundToInt (Zoom::BaseH * Zoom::Max));

    setResizable (false, false);
}

void StretchAudioProcessorEditor::applyScaledFonts()
{
    // Labels/boxes with explicit fonts need re-set on zoom; the rest
    // re-reads the L&F via lookAndFeelChanged().
    screen.mainPanel.controls.applyFontScale (uiScale);
    screen.topPanel.applyFontScale (uiScale);

    refreshAllLookAndFells();
}

void StretchAudioProcessorEditor::refreshAllLookAndFells()
{
    struct Walker
    {
        static void visit (juce::Component& c)
        {
            if (dynamic_cast<const StretchLookAndFeel*> (&c.getLookAndFeel()) != nullptr)
                c.lookAndFeelChanged();

            for (auto* child : c.getChildren())
                if (child != nullptr)
                    visit (*child);
        }
    };
    Walker::visit (*this);
}

void StretchAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (GUI::Color::Background);
}

void StretchAudioProcessorEditor::resized()
{
    crtOverlay.setBounds (getLocalBounds());
    screen.setBounds (getLocalBounds());

    const float pad = CRTScreen::getFrameSize();
    const float scale = 1.0f / (1.0f + 2.0f * pad);
    const float tx = getWidth()  * 0.5f * (1.0f - scale);
    const float ty = getHeight() * 0.5f * (1.0f - scale);

    screen.setTransform (processor.isCrtEnabled()
                             ? juce::AffineTransform::scale (scale, scale).translated (tx, ty)
                             : juce::AffineTransform());
}

void StretchAudioProcessorEditor::onFileDropped (const juce::File& file)
{
    processor.loadAudioFile (file);
}

// Shortcuts: Space play/pause, L loop, F freeze, R reverse. Processor-direct
// keeps button visuals in sync. Space is best-effort in hosts (guaranteed
// standalone only).
bool StretchAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Never steal keys from a value-box edit field.
    if (auto* focused = juce::Component::getCurrentlyFocusedComponent();
        focused != nullptr && dynamic_cast<juce::TextEditor*> (focused) != nullptr)
        return false;

    if (key.isKeyCode (juce::KeyPress::spaceKey))
    {
        processor.transportPlay(); // toggle play/pause
        return true;
    }

    switch (key.getKeyCode())
    {
        case 'L': case 'l':
            processor.setLooping (! processor.isLooping());
            return true;

        case 'F': case 'f':
        {
            const bool on = ! processor.isFrozen();
            processor.setFrozen (on);
            if (on && ! processor.isPlaying())
                processor.transportPlay();
            return true;
        }

        case 'R': case 'r':
            processor.setReversed (! processor.isReversed());
            return true;

        default:
            return false;
    }
}

void StretchAudioProcessorEditor::onSetExportFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        "Select Export Folder",
        processor.getExportFolder().isDirectory()
            ? processor.getExportFolder()
            : juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
        "");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                           | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser] (const juce::FileChooser& fc)
        {
            auto result = fc.getResult();
            if (result.isDirectory())
                processor.setExportFolder (result);
        });
}
