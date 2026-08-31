#include "PluginEditor.h"

StretchAudioProcessorEditor::StretchAudioProcessorEditor (StretchAudioProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      screen (p),
      crtOverlay (&screen, &p.getCrtEnabledFlag())
{
    static StretchLookAndFeel lookAndFeel;
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    addAndMakeVisible (screen);
    addAndMakeVisible (crtOverlay);
    crtOverlay.toFront (false);

    screen.mainPanel.waveform.onFileDropped = [this] (const juce::File& file)
    {
        onFileDropped (file);
    };

    screen.topPanel.onSetExportFolder = [this]
    {
        onSetExportFolder();
    };

    screen.topPanel.onViewStateChanged = [this]
    {
        screen.mainPanel.waveform.restoreFromProcessor();
    };

    // Loads complete off-thread; the callback arrives on the message
    // thread. SafePointer guards the gap where this editor is destroyed
    // while a decode is still in flight.
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

    // Editor may be (re)created long after a file was loaded (host recreates
    // UI, session reload): seed the waveform from the live processor buffer.
    if (processor.hasLoadedFile())
    {
        screen.mainPanel.waveform.setBuffer (processor.getOriginalBuffer(),
                                             processor.getFileSampleRate());
        screen.mainPanel.waveform.restoreFromProcessor();
    }

    // Host state restore for an already-loaded, unchanged file reaches the
    // editor here rather than through the async load path.
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

    // The window is always sized to STRETCH_PANEL * uiScale and free
    // drag-resize is disabled; the zoom menu (and the persisted "ui_scale"
    // APVTS param) is the only way the editor changes size. Everything scales
    // off Zoom::uiScale via Metrics.
    if (auto* scaleParam = processor.parameters.getParameter (StretchZoom::UI_SCALE_ID))
    {
        const int idx = juce::roundToInt (scaleParam->getValue()
                                          * (StretchZoom::ZOOM_PERCENTS.size() - 1));
        uiScale = StretchZoom::ZOOM_PERCENTS [juce::jlimit (0,
            (int) StretchZoom::ZOOM_PERCENTS.size() - 1, idx)] / 100.0f;
    }

    processor.parameters.addParameterListener (StretchZoom::UI_SCALE_ID, this);

    updateZoomLimits();
    applyZoom (uiScale);

    // Standalone startup-fit race: the wrapper's initial layout pass clamps
    // the editor back to its resize minimum, silently discarding the zoom
    // applied here. Re-assert the zoom once the window has settled.
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
}

void StretchAudioProcessorEditor::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID != StretchZoom::UI_SCALE_ID)
        return;

    auto* scaleParam = processor.parameters.getParameter (StretchZoom::UI_SCALE_ID);
    if (scaleParam == nullptr)
        return;

    const int idx = juce::roundToInt (scaleParam->getValue()
                                      * (StretchZoom::ZOOM_PERCENTS.size() - 1));
    applyZoom (StretchZoom::ZOOM_PERCENTS [juce::jlimit (0,
        (int) StretchZoom::ZOOM_PERCENTS.size() - 1, idx)] / 100.0f);
}

void StretchAudioProcessorEditor::applyZoom (float scale)
{
    scale = juce::jlimit (Zoom::Min, Zoom::Max, scale);
    uiScale = scale;
    Zoom::uiScale = scale;

    applyScaledFonts();
    updateZoomLimits();

    setSize (juce::roundToInt (Zoom::BaseW * uiScale),
             juce::roundToInt (Zoom::BaseH * uiScale));

    // Standalone: follow the editor size exactly (native title bar keeps the
    // OS frame outside the client, so the content area == editor bounds).
    if (processor.isRunningAsStandalone())
        if (auto* tl = getTopLevelComponent())
            if (tl != this)
                tl->setSize (juce::roundToInt (Zoom::BaseW * uiScale),
                             juce::roundToInt (Zoom::BaseH * uiScale));

    resized();
    repaint();
}

void StretchAudioProcessorEditor::updateZoomLimits()
{
    setResizeLimits (juce::roundToInt (Zoom::BaseW * Zoom::Min),
                     juce::roundToInt (Zoom::BaseH * Zoom::Min),
                     juce::roundToInt (Zoom::BaseW * Zoom::Max),
                     juce::roundToInt (Zoom::BaseH * Zoom::Max));

    setResizable (false, false);
}

void StretchAudioProcessorEditor::applyScaledFonts()
{
    // Ctrl labels and the volume box carry an explicit setFont()/textbox size
    // (not L&F-driven), so they need a re-set on zoom. Everything else
    // (value boxes, combos) re-reads the L&F via lookAndFeelChanged() below.
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

// Keyboard shortcuts: Space = play/pause, L = loop, F = freeze, R = reverse.
// Cards ride APVTS attachments, so driving the processor directly keeps
// every button visual in sync.
//
// Host-side Space handling DECISION: keys stay best-effort inside hosts
// (some intercept Space for their own transport) and are only guaranteed in
// the Standalone build. Documented in the About card.
bool StretchAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Never steal keys from an open value-box edit field.
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
