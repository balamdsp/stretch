#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../Helpers/StretchDefines.h"
#include "../Helpers/StretchLookAndFeel.h"
#include "StretchFxCard.h"

namespace StretchExportDialogs { StretchLookAndFeel& sharedLookAndFeel(); }

// Fixed-size non-modal card dialogs.
class StretchExportDialog : public juce::Component
{
public:
    StretchExportDialog (const juce::String& title,
                         const juce::String& message,
                         const juce::String& confirmText,
                         const juce::String& cancelText,
                         const juce::File& pathToShow = {},
                         float scale = Zoom::uiScale)
        : layerScale (scale)
    {
        setLookAndFeel (&StretchExportDialogs::sharedLookAndFeel());

        titleLabel.setText (title, juce::dontSendNotification);
        titleLabel.setFont (StretchLookAndFeel::makeFont (19.0f * layerScale));
        titleLabel.setColour (juce::Label::textColourId, GUI::Color::Logo.withAlpha (0.5f));
        addAndMakeVisible (titleLabel);

        messageLabel.setText (message, juce::dontSendNotification);
        messageLabel.setFont (StretchLookAndFeel::makeFont (22.0f * layerScale));
        messageLabel.setColour (juce::Label::textColourId, StretchColors::textPrimary);
        messageLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (messageLabel);

        if (! pathToShow.getFullPathName().isEmpty())
        {
            pathEditor.setReadOnly (true);
            pathEditor.setMultiLine (false, false);
            pathEditor.setReturnKeyStartsNewLine (false);
            pathEditor.setFont (StretchLookAndFeel::makeFont (20.0f * layerScale));
            pathEditor.setText (pathToShow.getFullPathName());
            addAndMakeVisible (pathEditor);
        }

        if (cancelText.isNotEmpty())
        {
            cancelButton.setButtonText (cancelText);
            cancelButton.onClick = [this] { closeWindow(); };
            addAndMakeVisible (cancelButton);
        }

        confirmButton.setButtonText (confirmText);
        confirmButton.onClick = [this]
        {
            if (onConfirm)
                onConfirm();
            closeWindow();
        };
        addAndMakeVisible (confirmButton);
    }

    ~StretchExportDialog() override
    {
        setLookAndFeel (nullptr);
    }

    std::function<void()> onConfirm;

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (GUI::Color::CardDark);
        g.fillRoundedRectangle (bounds, GUI::Layout::CardCorner());
        GUI::Paint::drawCardOutline (g, bounds, GUI::Layout::CardCorner());

        auto innerBounds = bounds.reduced (GUI::Layout::CardInset());
        g.setColour (GUI::Color::Card);
        g.fillRoundedRectangle (innerBounds, GUI::Layout::InnerCardCorner());
        GUI::Paint::drawCardOutline (g, innerBounds, GUI::Layout::InnerCardCorner(), 0.25f);
    }

    void resized() override
    {
        const Metrics m (layerScale);
        auto area = getLocalBounds()
                        .reduced (juce::roundToInt (GUI::Layout::CardInset() + GUI::Layout::ContentInset()));

        titleLabel.setBounds (area.removeFromTop (m.sc (kTitleHeightPx + kTitleInsetPx)));
        area.removeFromTop (m.sc (kTitleInsetPx));

        const int rowGap = m.sc (10);
        auto buttonsRow = area.removeFromBottom (m.sc (30));
        area.removeFromBottom (rowGap);

        const int cancelW = cancelButton.isVisible() ? m.sc (150) : 0;
        if (cancelButton.isVisible())
        {
            cancelButton.setBounds (buttonsRow.removeFromRight (cancelW));
            buttonsRow.removeFromRight (rowGap);
        }
        confirmButton.setBounds (buttonsRow.removeFromRight (m.sc (190)));

        if (pathEditor.isVisible())
        {
            pathEditor.setBounds (area.removeFromBottom (m.sc (32)));
            area.removeFromBottom (rowGap);
        }

        messageLabel.setBounds (area);
    }

private:
    void closeWindow()
    {
        if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
            dw->closeButtonPressed();
    }

    // Full-width Metrics invalid here; dialogs are narrower than 1000px.
    const float layerScale;

    static constexpr int kTitleInsetPx = 5;
    static constexpr int kTitleHeightPx = 16;

    juce::Label titleLabel;
    juce::Label messageLabel;
    juce::TextEditor pathEditor;
    StretchFxTextButton confirmButton;
    StretchFxTextButton cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchExportDialog)
};

// Live progress card; polls the getter on a timer (never touches UI off-thread).
class StretchExportProgressDialog : public juce::Component,
                                    private juce::Timer
{
public:
    StretchExportProgressDialog (const juce::String& title,
                                 std::function<double()> progressGetter,
                                 std::function<void()> onCancel,
                                 float scale = Zoom::uiScale)
        : layerScale (scale), getProgress (std::move (progressGetter)), cancelHandler (std::move (onCancel))
    {
        setLookAndFeel (&StretchExportDialogs::sharedLookAndFeel());

        titleLabel.setText (title, juce::dontSendNotification);
        titleLabel.setFont (StretchLookAndFeel::makeFont (19.0f * layerScale));
        titleLabel.setColour (juce::Label::textColourId, GUI::Color::Logo.withAlpha (0.5f));
        addAndMakeVisible (titleLabel);

        percentLabel.setFont (StretchLookAndFeel::makeFont (30.0f * layerScale));
        percentLabel.setColour (juce::Label::textColourId, StretchColors::textPrimary);
        percentLabel.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (percentLabel);

        cancelButton.setButtonText ("CANCEL");
        cancelButton.onClick = [this]
        {
            if (cancelHandler)
                cancelHandler();
        };
        addAndMakeVisible (cancelButton);

        startTimerHz (30);
    }

    ~StretchExportProgressDialog() override
    {
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (GUI::Color::CardDark);
        g.fillRoundedRectangle (bounds, GUI::Layout::CardCorner());
        GUI::Paint::drawCardOutline (g, bounds, GUI::Layout::CardCorner());

        auto innerBounds = bounds.reduced (GUI::Layout::CardInset());
        g.setColour (GUI::Color::Card);
        g.fillRoundedRectangle (innerBounds, GUI::Layout::InnerCardCorner());
        GUI::Paint::drawCardOutline (g, innerBounds, GUI::Layout::InnerCardCorner(), 0.25f);

        if (barArea.getWidth() > 0)
        {
            const float s = layerScale;
            const float trackH = 14.0f * s;
            auto track = barArea.toFloat().withSizeKeepingCentre (barArea.getWidth(), trackH);

            g.setColour (GUI::Color::CardDark.brighter (0.06f));
            g.fillRoundedRectangle (track, trackH / 2.0f);
            GUI::Paint::drawCardOutline (g, track, trackH / 2.0f, 0.25f);

            const double p = juce::jlimit (0.0, 1.0, shownFraction);
            if (p > 0.0)
            {
                auto fill = track.removeFromLeft ((float) (track.getWidth() * p));
                g.setColour (GUI::Color::Accent.withAlpha (0.85f));
                g.fillRoundedRectangle (fill, fill.getHeight() / 2.0f);
            }
        }
    }

    void resized() override
    {
        const Metrics m (layerScale);
        auto area = getLocalBounds()
                        .reduced (juce::roundToInt (GUI::Layout::CardInset() + GUI::Layout::ContentInset()));

        titleLabel.setBounds (area.removeFromTop (m.sc (kTitleHeightPx + kTitleInsetPx)));
        area.removeFromTop (m.sc (kTitleInsetPx));

        const int rowGap = m.sc (10);
        auto buttonsRow = area.removeFromBottom (m.sc (30));
        area.removeFromBottom (rowGap);

        cancelButton.setBounds (buttonsRow.removeFromRight (m.sc (150)));

        barArea = area.removeFromBottom (m.sc (18));
        area.removeFromBottom (rowGap);

        percentLabel.setBounds (area);
    }

private:
    void timerCallback() override
    {
        const double p = getProgress ? juce::jlimit (0.0, 1.0, getProgress()) : 0.0;

        shownFraction = p;
        percentLabel.setText (">> " + juce::String (juce::roundToInt (p * 100.0)) + " %",
                              juce::dontSendNotification);
        repaint();
    }

    const float layerScale;

    static constexpr int kTitleInsetPx = 5;
    static constexpr int kTitleHeightPx = 16;

    std::function<double()> getProgress;
    std::function<void()> cancelHandler;

    juce::Rectangle<int> barArea;
    double shownFraction = 0.0;

    juce::Label titleLabel;
    juce::Label percentLabel;
    StretchFxTextButton cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchExportProgressDialog)
};

class StretchNameEntryDialog : public juce::Component
{
public:
    StretchNameEntryDialog (const juce::String& title,
                            const juce::String& message,
                            const juce::String& initialText,
                            std::function<void (const juce::String&)> onAcceptFn,
                            float scale = Zoom::uiScale)
        : onAccept (std::move (onAcceptFn)), layerScale (scale)
    {
        setLookAndFeel (&StretchExportDialogs::sharedLookAndFeel());

        titleLabel.setText (title, juce::dontSendNotification);
        titleLabel.setFont (StretchLookAndFeel::makeFont (19.0f * layerScale));
        titleLabel.setColour (juce::Label::textColourId, GUI::Color::Logo.withAlpha (0.5f));
        addAndMakeVisible (titleLabel);

        messageLabel.setText (message, juce::dontSendNotification);
        messageLabel.setFont (StretchLookAndFeel::makeFont (22.0f * layerScale));
        messageLabel.setColour (juce::Label::textColourId, StretchColors::textPrimary);
        messageLabel.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (messageLabel);

        nameEditor.setMultiLine (false, false);
        nameEditor.setReturnKeyStartsNewLine (false);
        nameEditor.setFont (StretchLookAndFeel::makeFont (22.0f * layerScale));
        nameEditor.setText (initialText);
        nameEditor.setSelectAllWhenFocused (true);
        nameEditor.onReturnKey = [this] { accept(); };
        addAndMakeVisible (nameEditor);

        cancelButton.setButtonText ("CANCEL");
        cancelButton.onClick = [this] { closeWindow(); };
        addAndMakeVisible (cancelButton);

        saveButton.setButtonText ("SAVE");
        saveButton.onClick = [this] { accept(); };
        addAndMakeVisible (saveButton);
    }

    ~StretchNameEntryDialog() override
    {
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (GUI::Color::CardDark);
        g.fillRoundedRectangle (bounds, GUI::Layout::CardCorner());
        GUI::Paint::drawCardOutline (g, bounds, GUI::Layout::CardCorner());

        auto innerBounds = bounds.reduced (GUI::Layout::CardInset());
        g.setColour (GUI::Color::Card);
        g.fillRoundedRectangle (innerBounds, GUI::Layout::InnerCardCorner());
        GUI::Paint::drawCardOutline (g, innerBounds, GUI::Layout::InnerCardCorner(), 0.25f);
    }

    void resized() override
    {
        const Metrics m (layerScale);
        auto area = getLocalBounds()
                        .reduced (juce::roundToInt (GUI::Layout::CardInset() + GUI::Layout::ContentInset()));

        titleLabel.setBounds (area.removeFromTop (m.sc (kTitleHeightPx + kTitleInsetPx)));
        area.removeFromTop (m.sc (kTitleInsetPx));

        const int rowGap = m.sc (10);
        auto buttonsRow = area.removeFromBottom (m.sc (30));
        area.removeFromBottom (rowGap);

        cancelButton.setBounds (buttonsRow.removeFromRight (m.sc (150)));
        buttonsRow.removeFromRight (rowGap);
        saveButton.setBounds (buttonsRow.removeFromRight (m.sc (190)));

        area.removeFromBottom (rowGap);
        nameEditor.setBounds (area.removeFromBottom (m.sc (36)));
        area.removeFromBottom (rowGap);
        messageLabel.setBounds (area);
    }

private:
    void accept()
    {
        const juce::String name = nameEditor.getText();
        if (onAccept)
            onAccept (name);
        closeWindow();
    }

    void closeWindow()
    {
        if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
            dw->closeButtonPressed();
    }

    static constexpr int kTitleInsetPx = 5;
    static constexpr int kTitleHeightPx = 16;

    std::function<void (const juce::String&)> onAccept;
    const float layerScale;

    juce::Label titleLabel;
    juce::Label messageLabel;
    juce::TextEditor nameEditor;
    StretchFxTextButton saveButton;
    StretchFxTextButton cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchNameEntryDialog)
};

// RATE / DEPTH / FORMAT toggles. 32F is WAV-only: AIFF/FLAC fall back to 24.
class StretchExportOptionsDialog : public juce::Component
{
public:
    StretchExportOptionsDialog (int initialRate, int initialDepth, int initialFormat,
                                std::function<void (int, int, int)> onAcceptFn,
                                float scale = Zoom::uiScale)
        : onAccept (std::move (onAcceptFn)),
          layerScale (scale),
          sampleRate ((initialRate == 44100 || initialRate == 48000
                       || initialRate == 96000) ? initialRate : 0),
          bitDepth ((initialDepth == 24 || initialDepth == 32) ? initialDepth : 16),
          formatIndex (juce::jlimit (0, 2, initialFormat))
    {
        setLookAndFeel (&StretchExportDialogs::sharedLookAndFeel());

        titleLabel.setText (">> EXPORT OPTIONS", juce::dontSendNotification);
        titleLabel.setFont (StretchLookAndFeel::makeFont (19.0f * layerScale));
        titleLabel.setColour (juce::Label::textColourId, GUI::Color::Logo.withAlpha (0.5f));
        addAndMakeVisible (titleLabel);

        rateButtons = makeRow (rateRow, "RATE", { "SRC", "44100", "48000", "96000" },
            [this] (int i) { sampleRate = (i == 0) ? 0 : kRates[i - 1]; });

        depthButtons = makeRow (depthRow, "DEPTH", { "16", "24", "32F" },
            [this] (int i) { bitDepth = kDepths[i]; });

        formatButtons = makeRow (formatRow, "FORMAT", { "WAV", "AIFF", "FLAC" },
            [this] (int i)
            {
                formatIndex = i;
                if (i != 0 && bitDepth == 32)
                {
                    bitDepth = 24; // float is WAV-only
                    setExclusive (depthRow, 1);
                }
            });

        cancelButton.setButtonText ("CANCEL");
        cancelButton.onClick = [this] { closeWindow(); };
        addAndMakeVisible (cancelButton);

        okButton.setButtonText ("OK");
        okButton.onClick = [this]
        {
            if (onAccept)
                onAccept (sampleRate, bitDepth, formatIndex);
            closeWindow();
        };
        addAndMakeVisible (okButton);

        syncToggles();
    }

    ~StretchExportOptionsDialog() override { setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        g.setColour (GUI::Color::CardDark);
        g.fillRoundedRectangle (bounds, GUI::Layout::CardCorner());
        GUI::Paint::drawCardOutline (g, bounds, GUI::Layout::CardCorner());

        auto innerBounds = bounds.reduced (GUI::Layout::CardInset());
        g.setColour (GUI::Color::Card);
        g.fillRoundedRectangle (innerBounds, GUI::Layout::InnerCardCorner());
        GUI::Paint::drawCardOutline (g, innerBounds, GUI::Layout::InnerCardCorner(), 0.25f);
    }

    void resized() override
    {
        const Metrics m (layerScale);
        auto area = getLocalBounds()
                        .reduced (juce::roundToInt (GUI::Layout::CardInset() + GUI::Layout::ContentInset()));

        titleLabel.setBounds (area.removeFromTop (m.sc (kTitleHeightPx + kTitleInsetPx)));
        area.removeFromTop (m.sc (kTitleInsetPx));

        const int rowGap = m.sc (10);
        auto buttonsRow = area.removeFromBottom (m.sc (30));
        area.removeFromBottom (rowGap);

        cancelButton.setBounds (buttonsRow.removeFromRight (m.sc (150)));
        buttonsRow.removeFromRight (rowGap);
        okButton.setBounds (buttonsRow.removeFromRight (m.sc (150)));

        const int vertGap = m.sc (16);
        const int rowH = m.sc (34);
        const int buttonH = m.sc (24);
        const int blockH = 3 * rowH + 2 * vertGap;
        area.removeFromTop (juce::jmax (0, (area.getHeight() - blockH) / 2));

        for (int r = 0; r < 3; ++r)
        {
            auto row = area.removeFromTop (juce::jmin (rowH, area.getHeight()));
            area.removeFromTop (vertGap);

            auto* label = rowLabels[r];
            label->setBounds (row.removeFromLeft (m.sc (110)).translated (0, m.sc (-2)));

            auto& buttons = (r == 0) ? rateButtons : (r == 1) ? depthButtons : formatButtons;
            const int bw = (row.getWidth() - (buttons.size() - 1) * rowGap) / buttons.size();
            const int topInset = (rowH - buttonH) / 2;

            for (auto* b : buttons)
            {
                b->setBounds (row.getX(), row.getY() + topInset, bw, buttonH);
                row.removeFromLeft (bw);
                row.removeFromLeft (rowGap);
            }
        }
    }

private:
    // Exclusive-toggle row; onSelect fires with the index in this row.
    juce::Array<juce::Button*> makeRow (juce::OwnedArray<StretchFxTextButton>& storage,
                                        const juce::String& label,
                                        const juce::StringArray& captions,
                                        std::function<void (int)> onSelect)
    {
        auto* rowLabel = new juce::Label();
        rowLabels.add (rowLabel);
        rowLabel->setText (label, juce::dontSendNotification);
        rowLabel->setFont (StretchLookAndFeel::makeFont (20.0f * layerScale));
        rowLabel->setColour (juce::Label::textColourId, StretchColors::textMid);
        rowLabel->setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (rowLabel);

        juce::Array<juce::Button*> buttons;
        for (int i = 0; i < captions.size(); ++i)
        {
            auto* b = storage.add (std::make_unique<StretchFxTextButton>());
            b->setButtonText (captions[i]);
            b->setClickingTogglesState (true);
            b->onClick = [this, b, &storage, onSelect]
            {
                if (! b->getToggleState())
                    b->setToggleState (true, juce::dontSendNotification);

                int chosen = 0;
                for (int j = 0; j < storage.size(); ++j)
                {
                    storage[j]->setToggleState (storage[j] == b,
                                                juce::dontSendNotification);
                    if (storage[j] == b)
                        chosen = j;
                }

                onSelect (chosen);
            };
            addAndMakeVisible (b);
            buttons.add (b);
        }
        return buttons;
    }

    void setExclusive (juce::OwnedArray<StretchFxTextButton>& buttons, int index)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == index, juce::dontSendNotification);
    }

    void syncToggles()
    {
        for (int i = 0; i < rateButtons.size(); ++i)
            rateButtons[i]->setToggleState (
                (i == 0 && sampleRate == 0) || (i > 0 && sampleRate == kRates[i - 1]),
                juce::dontSendNotification);
        setExclusive (depthRow, (bitDepth == 24) ? 1 : (bitDepth == 32) ? 2 : 0);
        setExclusive (formatRow, formatIndex);
    }

    void closeWindow()
    {
        if (auto* dw = findParentComponentOfClass<juce::DocumentWindow>())
            dw->closeButtonPressed();
    }

    static constexpr int kTitleInsetPx = 5;
    static constexpr int kTitleHeightPx = 16;
    static constexpr int kRates[3] = { 44100, 48000, 96000 };
    static constexpr int kDepths[3] = { 16, 24, 32 };

    std::function<void (int, int, int)> onAccept;
    const float layerScale;

    int sampleRate = 0;   // 0 = source
    int bitDepth = 16;
    int formatIndex = 0;  // 0 WAV / 1 AIFF / 2 FLAC

    juce::Label titleLabel;
    juce::OwnedArray<juce::Label> rowLabels;
    juce::OwnedArray<StretchFxTextButton> rateRow;
    juce::OwnedArray<StretchFxTextButton> depthRow;
    juce::OwnedArray<StretchFxTextButton> formatRow;
    juce::Array<juce::Button*> rateButtons;
    juce::Array<juce::Button*> depthButtons;
    juce::Array<juce::Button*> formatButtons;
    StretchFxTextButton okButton;
    StretchFxTextButton cancelButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchExportOptionsDialog)
};

// Non-modal wrapper; self-deletes on close.
class StretchExportDialogWindow : public juce::DocumentWindow
{
public:
    StretchExportDialogWindow (const juce::String& title, juce::Component* content)
        : juce::DocumentWindow (title, juce::Colour (0xff0c0c0c), allButtons)
    {
        setContentOwned (content, true);
        setResizable (false, false);
    }

    // Base closeButtonPressed is a no-op in JUCE 9; without this nothing closes.
    void closeButtonPressed() override { delete this; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchExportDialogWindow)
};

namespace StretchExportDialogs
{
    // setLookAndFeel takes no ownership; one shared static for all dialogs.
    inline StretchLookAndFeel& sharedLookAndFeel()
    {
        static StretchLookAndFeel lnf;
        return lnf;
    }

    // Window takes ownership; sizes scale with the dialog zoom.
    template <typename DialogType>
    inline StretchExportDialogWindow* openWindow (DialogType* ownedDialog,
                                                  juce::Component* centreOn,
                                                  int w, int h,
                                                  float scale = Zoom::uiScale)
    {
        w = juce::roundToInt ((float) w * scale);
        h = juce::roundToInt ((float) h * scale);

        auto* win = new StretchExportDialogWindow ("Stretch", ownedDialog);

        if (centreOn != nullptr && centreOn->getPeer() != nullptr)
        {
            const auto centre = centreOn->getScreenBounds().getCentre();
            win->setTopLeftPosition (centre.getX() - w / 2, centre.getY() - h / 2);
            win->setSize (w, h);
        }
        else
        {
            win->centreWithSize (w, h);
        }

        win->setVisible (true);
        return win;
    }

    // Generic warning card: EXPORT ANYWAY runs onConfirm, CANCEL dismisses.
    inline void showWarning (const juce::String& title,
                             const juce::String& message,
                             juce::Component* centreOn,
                             std::function<void()> onConfirm)
    {
        auto* dialog = new StretchExportDialog (title, message,
                                                "EXPORT ANYWAY", "CANCEL");
        dialog->onConfirm = std::move (onConfirm);
        openWindow (dialog, centreOn, 500, 240);
    }

    // Rendered-file confirmation with a selectable path editor.
    inline void showSavedPath (const juce::File& f)
    {
        openWindow (new StretchExportDialog (
                        ">> EXPORT",
                        "Audio rendered to:",
                        "CLOSE", {},
                        f),
                    nullptr, 620, 230);
    }

    // Caller closes it when the render completes.
    inline StretchExportDialogWindow* showProgress (const juce::String& title,
                                                    std::function<double()> progressGetter,
                                                    std::function<void()> onCancel,
                                                    juce::Component* centreOn)
    {
        return openWindow (new StretchExportProgressDialog (title,
                                                            std::move (progressGetter),
                                                            std::move (onCancel)),
                           centreOn, 500, 240);
    }

    // onAccept fires with the typed text (may be empty), then closes.
    inline void askForName (const juce::String& title,
                            const juce::String& message,
                            const juce::String& initialText,
                            juce::Component* centreOn,
                            std::function<void (const juce::String&)> onAccept)
    {
        openWindow (new StretchNameEntryDialog (title, message, initialText, std::move (onAccept)),
                    centreOn, 500, 240);
    }

    // Rate 0 == source. OK fires onAccept, CANCEL just closes.
    inline void showExportOptions (int currentRate, int currentDepth, int currentFormat,
                                   juce::Component* centreOn,
                                   std::function<void (int, int, int)> onAccept)
    {
        openWindow (new StretchExportOptionsDialog (currentRate, currentDepth,
                                                    currentFormat,
                                                    std::move (onAccept)),
                    centreOn, 520, 296);
    }
}
