#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "../Helpers/StretchDefines.h"
#include "../Helpers/StretchLookAndFeel.h"
#include "../PluginProcessor.h"
#include "StretchTransportCard.h"

// Drop target + scrub/zoom/scroll/selection view. SHIFT+drag selects the
// loop region, ALT+click clears, double-click resets zoom.
class WaveformDisplay : public juce::Component,
                          public juce::FileDragAndDropTarget,
                          private juce::Timer
{
public:
    explicit WaveformDisplay (StretchAudioProcessor& proc)
        : processor (proc)
    {
        // View controls: zoom in / out / fit.
        // Zooming IN shrinks the visible span; both stay centred.
        zoomInButton.onClick  = [this] { centreViewOn (viewStart + viewLen * 0.5); setZoom (viewLen / kWheelZoomStep); };
        zoomOutButton.onClick = [this] { centreViewOn (viewStart + viewLen * 0.5); setZoom (viewLen * kWheelZoomStep); };
        zoomFitButton.onClick = [this] { resetView(); };

        for (auto* b : { &zoomInButton, &zoomOutButton, &zoomFitButton })
            addAndMakeVisible (b);

        // Unload ("X").
        unloadButton.onClick = [this]
        {
            processor.unloadSample();
            clearBuffer();
        };
        addAndMakeVisible (unloadButton);
        unloadButton.setVisible (false);

        // 30 Hz repaint keeps the playhead live.
        startTimerHz (30);
    }

    void setBuffer (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        sourceBuffer = &buffer;
        fileSampleRate = sampleRate;
        createThumbnail();
        buildPeakMips();
        unloadButton.setVisible (! thumbnailSamples.isEmpty());

        // Local reset only; the processor holds the persisted view/loop.
        viewStart = 0.0;
        viewLen = 1.0;
        selStart = 0.0;
        selEnd = 0.0;
        restoreFromProcessor();

        repaint();
    }

    void clearBuffer()
    {
        sourceBuffer = nullptr;
        thumbnailSamples.clear();
        peakMips.clear();
        unloadButton.setVisible (false);
        resetView();
        clearSelection();
        repaint();
    }

    // Re-apply view + selection from the processor. Call after setBuffer().
    void restoreFromProcessor()
    {
        viewLen  = juce::jlimit (kMinViewLen, 1.0, processor.getWaveViewLen());
        viewStart = juce::jlimit (0.0, 1.0 - viewLen, processor.getWaveViewStart());

        const double ls = processor.getLoopStart();
        const double le = processor.getLoopEnd();
        // (0,1) is the "no selection" sentinel; only apply real sub-ranges.
        if (ls > 0.0 || le < 1.0)
        {
            selStart = ls;
            selEnd = le;
        }

        rebuildViewPeaks();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const float s = scaleFor (*this);

        g.setColour (GUI::Color::CardDark);
        g.fillRoundedRectangle (bounds, GUI::Layout::CardCorner());
        GUI::Paint::drawCardOutline (g, bounds, GUI::Layout::CardCorner());

        auto innerBounds = bounds.reduced (GUI::Layout::CardInset());
        g.setColour (GUI::Color::Card);
        g.fillRoundedRectangle (innerBounds, GUI::Layout::InnerCardCorner());

        if (thumbnailSamples.isEmpty())
        {
            g.setColour (GUI::Color::Logo.withAlpha (0.28f));
            g.setFont (StretchLookAndFeel::makeFont (22.0f * s));
            g.drawText ("> DROP AUDIO OR CLICK HERE <",
                        innerBounds, juce::Justification::centred, false);
            return;
        }

        auto waveArea = getWaveArea();

        drawSelection (g, waveArea);
        drawWaveform (g, waveArea);
        drawFreezeMarker (g, waveArea);
        drawPlayhead (g, waveArea);
        drawScrollbar (g, getScrollbarArea());
    }

    void resized() override
    {
        const float s = scaleFor (*this);
        auto inner = getLocalBounds().reduced ((int) GUI::Layout::CardInset());

        // Zoom cluster: top-right of the inner card.
        const int zw = juce::roundToInt (22.0f * s);
        const int zh = juce::roundToInt (18.0f * s);
        const int gap = juce::roundToInt (4.0f * s);
        int zy = inner.getY() + juce::roundToInt (5.0f * s);
        int zx = inner.getRight() - 3 * zw - 2 * gap - juce::roundToInt (5.0f * s);

        zoomInButton.setBounds  (zx, zy, zw, zh);
        zoomOutButton.setBounds (zx + zw + gap, zy, zw, zh);
        zoomFitButton.setBounds (zx + 2 * (zw + gap), zy, zw, zh);

        // Unload button: bottom-right of the inner card.
        const int unloadSize = juce::roundToInt (22.0f * s);
        const int inset = juce::roundToInt (5.0f * s);
        unloadButton.setBounds (inner.getRight() - unloadSize - inset,
                                inner.getBottom() - unloadSize - inset,
                                unloadSize, unloadSize);

        rebuildViewPeaks(); // bar geometry depends on the pixel width
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
        {
            auto file = juce::File (f);
            if (file.hasFileExtension ("wav;wave;mp3;aiff;aif;flac;ogg"))
                return true;
        }
        return false;
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        if (files.size() > 0)
        {
            juce::File file (files[0]);
            if (file.existsAsFile() && onFileDropped)
                onFileDropped (file);
        }
        isDragOver = false;
        repaint();
    }

    void fileDragEnter (const juce::StringArray&, int, int) override
    {
        isDragOver = true;
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        isDragOver = false;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (thumbnailSamples.isEmpty())
        {
            // Empty area click offers the file browser; deferred a turn so
            // modal-in-mouseDown can't swallow the selection signal.
            if (! fileChooserOpen && onFileDropped)
            {
                juce::Component::SafePointer<WaveformDisplay> safeThis { this };
                fileChooserOpen = true;
                juce::MessageManager::callAsync ([safeThis]
                {
                    if (safeThis == nullptr)
                        return;
                    safeThis->fileChooserOpen = false;
                    safeThis->openFileChooser();
                });
            }
            return;
        }

        // Scrollbar thumb takes priority when hit.
        if (getScrollbarArea().contains (e.position.toInt())
            && ! getThumbRect (getScrollbarArea()).isEmpty())
        {
            mode = Mode::scrollbar;
            scrollbarDragOffset = e.position.x - getThumbRect (getScrollbarArea()).getX();
            return;
        }

        if (e.mods.isAltDown())
        {
            clearSelection();
            repaint();
            return;
        }

        const auto wave = getWaveArea();
        if (! wave.contains (e.position))
            return;

        if (e.mods.isShiftDown())
        {
            // Anchor a fresh selection at the click point.
            mode = Mode::selecting;
            selAnchorFrac = xToFraction (e.position.x, wave);
            setSelection (selAnchorFrac, selAnchorFrac);
            return;
        }

        mode = Mode::scrubbing;
        seekFromEvent (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (mode == Mode::selecting)
        {
            const auto wave = getWaveArea();
            const double f = xToFraction (e.position.x, wave);
            setSelection (selAnchorFrac, f);
            return;
        }

        if (mode == Mode::scrollbar)
        {
            const auto track = getScrollbarArea();
            if (track.getWidth() <= 0)
                return;

            // Thumb left edge == view start, so dragging maps 1:1 to offset.
            const float x = e.position.x - scrollbarDragOffset - (float) track.getX();
            scrollToStart ((double) x / (double) track.getWidth());
            return;
        }

        if (mode == Mode::scrubbing)
            seekFromEvent (e);
    }

    void mouseUp (const juce::MouseEvent&) override { mode = Mode::none; }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (getScrollbarArea().contains (e.position.toInt()))
            return;

        resetView();
        clearSelection();
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (thumbnailSamples.isEmpty())
            return;

        const bool zooming = std::abs (wheel.deltaY) > std::abs (wheel.deltaX);

        if (zooming)
        {
            // Anchor: whatever fraction sits under the cursor stays there.
            const auto wave = getWaveArea();
            const double rel = (wave.getWidth() > 0.0f)
                ? juce::jlimit (0.0, 1.0,
                    (double) ((e.position.x - wave.getX()) / wave.getWidth()))
                : 0.5;
            const double anchor = viewStart + rel * viewLen;

            const double factor = std::pow ((double) kWheelZoomStep,
                                            - (double) wheel.deltaY * 3.0);
            setZoom (viewLen * factor);          // new viewLen
            scrollToStart (anchor - rel * viewLen);
        }
        else
        {
            scrollToStart (viewStart + (double) wheel.deltaX * viewLen * 0.25);
        }
    }

    std::function<void (const juce::File&)> onFileDropped;

private:
    // File browser; wildcard mirrors isInterestedInFileDrag.
    void openFileChooser()
    {
        static const juce::String audioWildcard =
            "*.wav;*.wave;*.mp3;*.aiff;*.aif;*.flac;*.ogg";

        auto chooser = std::make_shared<juce::FileChooser> (
            "Select Audio File",
            juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
            audioWildcard);

        juce::Component::SafePointer<WaveformDisplay> safeThis { this };
        chooser->launchAsync (
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
            [safeThis, chooser] (const juce::FileChooser& fc)
            {
                if (safeThis == nullptr)
                    return;
                const juce::File file = fc.getResult();
                if (file.existsAsFile() && safeThis->onFileDropped)
                    safeThis->onFileDropped (file);
            });
    }
    // Whole-file peak pyramid: level 0 maxes each 256-sample run, each next
    // level maxes 4 buckets (~1.33x one pass).
    struct MipLevel
    {
        int64_t bucketSize = 0;
        juce::Array<float> maxima;
    };

    static constexpr int64_t kMipBaseBucket = 256;

    // View window [viewStart, viewStart + viewLen] over normalized time.

    static constexpr double kMinViewLen = 1.0 / 512.0;   // max zoom-in
    static constexpr float  kWheelZoomStep = 1.15f;
    static constexpr int    kScrollBarH = 10;

    enum class Mode { none, scrubbing, selecting, scrollbar };
    Mode mode = Mode::none;

    StretchAudioProcessor& processor;
    const juce::AudioBuffer<float>* sourceBuffer = nullptr;
    double fileSampleRate = 0.0;
    juce::Array<float> thumbnailSamples;
    bool isDragOver = false;
    bool fileChooserOpen = false;

    double viewStart = 0.0;
    double viewLen = 1.0;

    double selStart = 0.0;   // selection (fractions); equal => empty
    double selEnd = 0.0;
    double selAnchorFrac = 0.0;

    juce::Array<float> viewPeaks;   // per-pixel peaks for the current view
    int peaksForWidth = 0;          // width the cache was built for
    std::vector<MipLevel> peakMips; // whole-file peak pyramid (built on load)

    float scrollbarDragOffset = 0.0f;

    StretchIconButton unloadButton { StretchIconButton::Glyph::X };
    StretchIconButton zoomInButton  { StretchIconButton::Glyph::Plus };
    StretchIconButton zoomOutButton { StretchIconButton::Glyph::Minus };
    StretchIconButton zoomFitButton { StretchIconButton::Glyph::Fit };

    // Geometry
    juce::Rectangle<float> getWaveArea() const
    {
        const float s = scaleFor (*this);
        // Inner card minus the bottom scrollbar strip.
        return getLocalBounds().toFloat()
                   .reduced (GUI::Layout::CardInset())
                   .withTrimmedBottom ((float) (kScrollBarH * s + 3 * s));
    }

    juce::Rectangle<int> getScrollbarArea() const
    {
        const float s = scaleFor (*this);
        auto inner = getLocalBounds().reduced ((int) GUI::Layout::CardInset());
        return inner.removeFromBottom (juce::roundToInt (kScrollBarH * s))
                        .removeFromBottom (juce::roundToInt (6.0f * s))
                        .withTrimmedLeft (juce::roundToInt (2.0f * s))
                        .withTrimmedRight (juce::roundToInt (2.0f * s));
    }

    juce::Rectangle<int> getThumbRect (const juce::Rectangle<int>& track) const
    {
        if (viewLen >= 1.0 || getWidth() <= 0)
            return {};

        const float s = scaleFor (*this);
        const float w = (float) track.getWidth() * (float) viewLen;
        const float x = (float) track.getX() + (float) track.getWidth() * (float) viewStart;

        return juce::Rectangle<int> ((int) x, track.getY(),
                                     juce::jmax (juce::roundToInt (12.0f * s), (int) w),
                                     track.getHeight());
    }

    // Absolute fraction <-> pixel x inside the wave area.
    double xToFraction (float x, const juce::Rectangle<float>& wave) const
    {
        const double rel = (wave.getWidth() > 0.0f)
            ? juce::jlimit (0.0, 1.0, (double) ((x - wave.getX()) / wave.getWidth()))
            : 0.0;
        return viewStart + rel * viewLen;
    }

    // View mutations
    void setZoom (double newLen)
    {
        viewLen = juce::jlimit (kMinViewLen, 1.0, newLen);
        // No peak rebuild: callers always scroll next (which rebuilds).
        viewStart = juce::jlimit (0.0, 1.0 - viewLen, viewStart);
    }

    void scrollToStart (double s)
    {
        viewStart = juce::jlimit (0.0, 1.0 - viewLen, s);
        processor.setWaveViewStart (viewStart);
        processor.setWaveViewLen (viewLen);
        rebuildViewPeaks();
    }

    void centreViewOn (double centreFrac)
    {
        scrollToStart (centreFrac - viewLen * 0.5);
    }

    void resetView()
    {
        viewStart = 0.0;
        viewLen = 1.0;
        processor.setWaveViewStart (0.0);
        processor.setWaveViewLen (1.0);
        rebuildViewPeaks();
    }

    void setSelection (double a, double b)
    {
        selStart = juce::jlimit (0.0, 1.0, juce::jmin (a, b));
        selEnd   = juce::jlimit (0.0, 1.0, juce::jmax (a, b));

        // Sub-8-sample drags count as empty.
        if (sourceBuffer != nullptr
            && (selEnd - selStart) * (double) sourceBuffer->getNumSamples() < 8.0)
            clearSelection();
        else
            processor.setLoopRegion (selStart, selEnd);

        repaint();
    }

    void clearSelection()
    {
        selStart = 0.0;
        selEnd = 0.0;
        processor.setLoopRegion (0.0, 1.0);
    }

    // Painting
    void timerCallback() override
    {
        if (isVisible())
            repaint();
    }

    void createThumbnail()
    {
        if (sourceBuffer == nullptr || sourceBuffer->getNumSamples() <= 0)
            return;

        const int numSamples = sourceBuffer->getNumSamples();
        const int numChannels = sourceBuffer->getNumChannels();
        const int thumbSize = 512;

        thumbnailSamples.clear();

        int samplesPerThumbSample = juce::jmax (1, numSamples / thumbSize);

        for (int i = 0; i < thumbSize; ++i)
        {
            int start = i * samplesPerThumbSample;
            int end = juce::jmin (start + samplesPerThumbSample, numSamples);

            float maxVal = 0.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                const float* data = sourceBuffer->getReadPointer (c);
                for (int s = start; s < end; ++s)
                {
                    float absVal = std::abs (data[s]);
                    if (absVal > maxVal)
                        maxVal = absVal;
                }
            }
            thumbnailSamples.add (maxVal);
        }
    }

    // Per-pixel peaks for the current view. Deep zoom reads the source
    // directly; wider views read the mip pyramid (O(pixels), not O(file)).
    void rebuildViewPeaks()
    {
        const int width = juce::jmax (1, (int) getWaveArea().getWidth());
        peaksForWidth = width;
        viewPeaks.clearQuick();

        if (sourceBuffer == nullptr || sourceBuffer->getNumSamples() <= 0)
            return;

        const int64_t total = sourceBuffer->getNumSamples();
        const int64_t startSample = (int64_t) std::floor (viewStart * (double) total);
        const int64_t spanSamples = juce::jmax<int64_t> (1,
            (int64_t) std::ceil (viewLen * (double) total));

        const int numChannels = sourceBuffer->getNumChannels();
        const int64_t samplesPerPixel = juce::jmax<int64_t> (1, spanSamples / width);

        const bool useMip = samplesPerPixel > kMipBaseBucket && ! peakMips.empty();
        const MipLevel* mip = useMip ? &pickMipLevel (samplesPerPixel) : nullptr;

        for (int px = 0; px < width; ++px)
        {
            const int64_t s0 = startSample + (int64_t) ((double) px * (double) spanSamples / (double) width);
            const int64_t s1 = juce::jmin (startSample + spanSamples,
                                           s0 + juce::jmax<int64_t> (1, spanSamples / width));

            float maxVal = 0.0f;

            if (mip != nullptr)
            {
                const int64_t bucket = mip->bucketSize;
                const int numBuckets = mip->maxima.size();

                const int b0 = (int) juce::jlimit<int64_t> (0, numBuckets - 1, s0 / bucket);
                const int b1 = (int) juce::jlimit<int64_t> (0, numBuckets - 1, (s1 - 1) / bucket);

                for (int b = b0; b <= b1; ++b)
                    maxVal = juce::jmax (maxVal, mip->maxima.getUnchecked (b));
            }
            else
            {
                for (int c = 0; c < numChannels; ++c)
                {
                    const float* data = sourceBuffer->getReadPointer (c);
                    for (int64_t s = s0; s < s1 && s < total; ++s)
                        maxVal = juce::jmax (maxVal, std::abs (data[s]));
                }
            }

            viewPeaks.add (maxVal);
        }
    }

    void buildPeakMips()
    {
        peakMips.clear();

        if (sourceBuffer == nullptr || sourceBuffer->getNumSamples() <= 0)
            return;

        const int64_t total = sourceBuffer->getNumSamples();
        const int numChannels = sourceBuffer->getNumChannels();

        MipLevel base;
        base.bucketSize = kMipBaseBucket;

        for (int64_t start = 0; start < total; start += kMipBaseBucket)
        {
            const int64_t end = juce::jmin (total, start + kMipBaseBucket);

            float maxVal = 0.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                const float* data = sourceBuffer->getReadPointer (c);
                for (int64_t s = start; s < end; ++s)
                    maxVal = juce::jmax (maxVal, std::abs (data[s]));
            }

            base.maxima.add (maxVal);
        }

        peakMips.push_back (std::move (base));

        while (peakMips.back().maxima.size() > 1)
        {
            const auto& prev = peakMips.back().maxima;

            MipLevel next;
            next.bucketSize = peakMips.back().bucketSize * 4;

            for (int i = 0; i < prev.size(); i += 4)
            {
                float maxVal = prev.getUnchecked (i);
                for (int j = i + 1; j < juce::jmin (i + 4, prev.size()); ++j)
                    maxVal = juce::jmax (maxVal, prev.getUnchecked (j));
                next.maxima.add (maxVal);
            }

            peakMips.push_back (std::move (next));
        }
    }

    // Smallest level whose bucket covers the per-pixel range.
    const MipLevel& pickMipLevel (int64_t samplesPerPixel) const
    {
        for (const auto& level : peakMips)
            if (level.bucketSize >= samplesPerPixel)
                return level;

        return peakMips.back();
    }

    void drawWaveform (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (viewPeaks.size() <= 0 || viewPeaks.size() != peaksForWidth)
        {
            rebuildViewPeaks();
            if (viewPeaks.isEmpty())
                return;
        }

        const int numBars = viewPeaks.size();
        const float barWidth = bounds.getWidth() / (float) numBars;
        const float midY = bounds.getCentreY();

        g.setColour (GUI::Color::Accent.withAlpha (0.7f));

        for (int i = 0; i < numBars; ++i)
        {
            float x = bounds.getX() + (float) i * barWidth;
            float amplitude = viewPeaks[i];
            float halfHeight = amplitude * bounds.getHeight() * 0.45f;

            g.fillRect (x, midY - halfHeight, juce::jmax (1.0f, barWidth - 0.5f), halfHeight * 2.0f);
        }
    }

    void drawPlayhead (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (sourceBuffer == nullptr)
            return;

        const double pos = juce::jlimit (0.0, 1.0, processor.getTransportFraction());
        if (pos < viewStart || pos > viewStart + viewLen)
            return; // outside the visible window

        const float x = bounds.getX()
            + (float) ((pos - viewStart) / viewLen) * bounds.getWidth();

        g.setColour (GUI::Color::KeyDown);
        g.drawLine (x, bounds.getY(), x, bounds.getBottom(), 2.0f * scaleFor (*this));
    }

    void drawFreezeMarker (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (! processor.isFrozen() || sourceBuffer == nullptr)
            return;

        const double pos = juce::jlimit (0.0, 1.0, processor.getTransportFraction());
        if (pos < viewStart || pos > viewStart + viewLen)
            return;

        const float x = bounds.getX()
            + (float) ((pos - viewStart) / viewLen) * bounds.getWidth();

        // Parked-playhead flag + dashed hold line.
        g.setColour (GUI::Color::Accent);
        const float s = scaleFor (*this);

        juce::Path diamond;
        const float r = 5.0f * s;
        diamond.addTriangle (x, bounds.getY(), x - r, bounds.getY() + r, x + r, bounds.getY() + r);
        diamond.addTriangle (x - r, bounds.getY() + r, x + r, bounds.getY() + r, x, bounds.getY() + 2.0f * r);
        g.fillPath (diamond);

        const float dash = 4.0f * s;
        for (float y = bounds.getY(); y < bounds.getBottom(); y += dash * 2.0f)
            g.drawLine (x, y, x, juce::jmin (y + dash, bounds.getBottom()), 1.0f);
    }

    void drawSelection (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        if (selEnd <= selStart)
            return;

        const float s = scaleFor (*this);

        const float x0 = bounds.getX()
            + (float) ((selStart - viewStart) / viewLen) * bounds.getWidth();
        const float x1 = bounds.getX()
            + (float) ((selEnd - viewStart) / viewLen) * bounds.getWidth();

        const float cx0 = juce::jmax (bounds.getX(), x0);
        const float cx1 = juce::jmin (bounds.getRight(), x1);
        if (cx1 <= cx0)
            return;

        auto region = bounds.withLeft (cx0).withRight (cx1);

        g.setColour (GUI::Color::Accent.withAlpha (0.14f));
        g.fillRect (region);

        g.setColour (GUI::Color::Accent.withAlpha (0.75f));
        g.drawLine (region.getX(), region.getY(), region.getX(), region.getBottom(), 1.5f * s);
        g.drawLine (region.getRight(), region.getY(), region.getRight(), region.getBottom(), 1.5f * s);
    }

    void drawScrollbar (juce::Graphics& g, const juce::Rectangle<int> track)
    {
        if (track.getWidth() <= 0 || thumbnailSamples.isEmpty())
            return;

        const float s = scaleFor (*this);
        auto area = track.toFloat();

        g.setColour (GUI::Color::CardDark.brighter (0.05f));
        g.fillRoundedRectangle (area, 3.0f * s);

        const auto thumb = getThumbRect (track).toFloat();
        if (thumb.getWidth() > 0.0f)
        {
            g.setColour (GUI::Color::Accent.withAlpha (0.55f));
            g.fillRoundedRectangle (thumb.reduced (0.0f, 1.0f * s), 2.0f * s);
        }
    }

    // Scrub maps the click into the visible window, not the whole file.
    void seekFromEvent (const juce::MouseEvent& e)
    {
        if (thumbnailSamples.isEmpty())
            return;

        const auto wave = getWaveArea();
        if (wave.getWidth() <= 0.0f || ! wave.contains (e.position))
            return;

        const double absFrac = xToFraction (e.position.x, wave);
        processor.seekToFraction (absFrac);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformDisplay)
};
