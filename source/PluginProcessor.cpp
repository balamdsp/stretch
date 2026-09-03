#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <utility>

#include "Helpers/StretchSettings.h"

static juce::PropertiesFile& getGlobalSettings();

StretchAudioProcessor::StretchAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
#if ! JucePlugin_IsSynth
          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
#endif
          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
          ),
      parameters (*this, nullptr, juce::Identifier ("StretchParams"), createParameterLayout())
#endif
{
    // Seed shared choices so every format/instance starts alike.
    if (getGlobalSettings().containsKey ("exportFolder"))
        exportFolder = juce::File (getGlobalSettings().getValue ("exportFolder"));

    // Mirror automatable toggles into atomics + engage side-effects.
    parameters.addParameterListener ("Freeze", this);
    parameters.addParameterListener ("Rewind", this);

    frozen.store (parameters.getRawParameterValue ("Freeze")->load() > 0.5f);
    reversed.store (parameters.getRawParameterValue ("Rewind")->load() > 0.5f);

    // CRT choices are machine-wide (settings.xml), not plugin state.
    crtEnabled.store (StretchSettings::getCrtEnabled());
    crtStrength.store (StretchSettings::getCrtStrength());
}

StretchAudioProcessor::~StretchAudioProcessor()
{
    parameters.removeParameterListener ("Freeze", this);
    parameters.removeParameterListener ("Rewind", this);

    // Join workers before the members they touch die.
    if (fileLoader)
        fileLoader->stopThread (15000);
    if (exportThread)
        exportThread->stopThread (15000);
}

void StretchAudioProcessor::setCrtEnabled (bool enabled)
{
    crtEnabled = enabled;
    StretchSettings::setCrtEnabled (enabled);
}

void StretchAudioProcessor::setCrtStrength (int strength)
{
    crtStrength.store (juce::jlimit (0, 2, strength));
    StretchSettings::setCrtStrength (strength);
}

void StretchAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    const bool on = newValue > 0.5f;

    if (parameterID == "Freeze")
    {
        frozen.store (on);
        // Freezing from a stopped transport must still produce sound.
        if (on && ! playing.load())
            transportPlay();
    }
    else if (parameterID == "Rewind")
    {
        reversed.store (on);
    }
}

const juce::String StretchAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool StretchAudioProcessor::acceptsMidi() const { return false; }
bool StretchAudioProcessor::producesMidi() const { return false; }
bool StretchAudioProcessor::isMidiEffect() const { return false; }
double StretchAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int StretchAudioProcessor::getNumPrograms() { return 1; }
int StretchAudioProcessor::getCurrentProgram() { return 0; }
void StretchAudioProcessor::setCurrentProgram (int) {}
const juce::String StretchAudioProcessor::getProgramName (int) { return {}; }
void StretchAudioProcessor::changeProgramName (int, const juce::String&) {}

void StretchAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const double preservedFraction = transportFraction.load();

    engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));
    engine.reset();
    glideRate = parameters.getRawParameterValue ("TimeRatio")->load();

    // Worst-case consumption: 400% eats 4x per output block.
    const int worstInput = StretchEngine::inputSamplesForOutput (samplesPerBlock, 4.0f);
    const int scratchSize = juce::jmax (4096, worstInput + 16);

    scratchInput.setSize (engine.getChannels(), scratchSize);
    scratchInPtrs.resize ((size_t) engine.getChannels());
    for (int c = 0; c < engine.getChannels(); ++c)
        scratchInPtrs[(size_t) c] = scratchInput.getWritePointer (c);

    {
        std::lock_guard<std::mutex> lock (processingMutex);
        if (originalBuffer.getNumSamples() > 0)
            engine.setSource (originalBuffer, fileSampleRate);
    }

    const int64_t len = (int64_t) engine.getSourceLength();
    if (len > 0)
    {
        const int64_t restored = (int64_t) ((double) len * juce::jlimit (0.0, 1.0, preservedFraction));
        pendingSeek.store (restored);
    }
}

void StretchAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool StretchAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
#endif

void StretchAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int totalNumInputChannels = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, numSamples);

    // Nothing loaded: pass host/device audio through.
    if (! engine.hasSource())
        return;

    std::lock_guard<std::mutex> lock (processingMutex);

    const float pitchSemi = parameters.getRawParameterValue ("PitchSemitones")->load();
    const float timeRatio = parameters.getRawParameterValue ("TimeRatio")->load();
    const bool formantPreserve = parameters.getRawParameterValue ("FormantPreserve")->load() > 0.5f;
    const float formantSemi = parameters.getRawParameterValue ("FormantSemitones")->load();
    const float outputGain = juce::Decibels::decibelsToGain (
        parameters.getRawParameterValue ("OutputGain")->load());

    engine.updateStretcherParams (pitchSemi, formantPreserve, formantSemi);

    const bool loop = looping.load();
    const bool frozenState = frozen.load();

    // REWIND flips the RATE sign for reverse playback.
    const float dirRate = reversed.load() ? -timeRatio : timeRatio;

    // FROZEN = rate 0: no input consumed, playhead parked, no seeks.
    const float effRate = frozenState ? 0.0f : dirRate;
    const bool backward = dirRate < 0.0f;

    // Pending seeks (scrub / seek-while-stopped).
    const int64_t seekTarget = pendingSeek.exchange (-1);
    if (seekTarget >= 0)
    {
        playPosition.store (seekTarget);
        engine.reset();
        glideRate = effRate; // position jumped: no continuity to preserve
    }

    if (! playing.load())
    {
        buffer.clear (0, numSamples);
        if (totalNumOutputChannels > 1)
            buffer.clear (1, numSamples);
        return;
    }

    const int len = engine.getSourceLength();
    if (len <= 0)
        return;

    const int numOutChannels = juce::jmin (totalNumOutputChannels, engine.getChannels());

    // Freeze glide: ease the rate (~5ms tau) so input consumption never
    // steps at grain boundaries. Rewind flips glide through zero too.
    {
        const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
        const double tau = juce::jmax (1.0, 0.005 * sr);
        const double coeff = 1.0 - std::exp (- (double) numSamples / tau);
        glideRate += ((double) effRate - glideRate) * coeff;

        // Snap when close so the tail doesn't linger.
        if (std::abs (glideRate - (double) effRate) < 1e-4 * juce::jmax (1.0f, std::abs (effRate)))
            glideRate = effRate;
    }

    // Loop region as samples; degenerate selections use the full file.
    const int64_t len64 = (int64_t) len;
    int64_t regionA = 0;
    int64_t regionB = len64;
    if (loop)
    {
        const double fs = juce::jlimit (0.0, 1.0, loopStart.load());
        const double fe = juce::jlimit (0.0, 1.0, loopEnd.load());

        regionA = juce::jlimit<int64_t> (0, len64 - 1, (int64_t) (fs * (double) len64));
        regionB = juce::jlimit<int64_t> (regionA + 1, len64, (int64_t) std::ceil (fe * (double) len64));

        if (regionB <= regionA)
            { regionA = 0; regionB = len64; }
    }
    const int64_t regionSpan = regionB - regionA;

    int64_t pos = playPosition.load();

    // Clamp stray playheads back into the region before filling.
    if (loop && (pos < regionA || pos >= regionB))
    {
        int64_t rel = (pos - regionA) % regionSpan;
        if (rel < 0)
            rel += regionSpan;
        pos = regionA + rel;
    }

    const int numIn = juce::jmin (StretchEngine::inputSamplesForOutput (numSamples, (float) glideRate),
                                  scratchInput.getNumSamples());
    const int sourceChannels = engine.getSourceChannels();

    // Fill scratch in read order (backward when reversed), wrapping or
    // zero-padding past the ends. iWrapIn marks the seam for the fade below.
    bool reachedEnd = false;
    int iWrapIn = -1;
    for (int c = 0; c < engine.getChannels(); ++c)
    {
        const float* src = engine.getSource().getReadPointer (juce::jmin (c, sourceChannels - 1));
        float* dst = scratchInPtrs[(size_t) c];

        for (int i = 0; i < numIn; ++i)
        {
            int64_t idx = backward ? (pos - (int64_t) i) : (pos + (int64_t) i);

            if (backward ? (idx < regionA) : (idx >= regionB))
            {
                if (loop)
                {
                    if (iWrapIn < 0)
                        iWrapIn = i;

                    idx = regionA + (((idx - regionA) % regionSpan) + regionSpan) % regionSpan;
                }
                else
                {
                    dst[i] = 0.0f;
                    reachedEnd = true;
                    continue;
                }
            }

            dst[i] = src[(int) idx];
        }
    }

    outPtrs.resize ((size_t) numOutChannels);
    for (int c = 0; c < numOutChannels; ++c)
        outPtrs[(size_t) c] = buffer.getWritePointer (c);

    engine.renderBlock (scratchInPtrs.data(), engine.getChannels(), numIn,
                        outPtrs.data(), numOutChannels, numSamples);

    // Loop-seam de-click: the wrap resets the stretcher (clicks), so mask
    // it with a short equal-power dip at the seam.
    if (iWrapIn >= 0 && numSamples > 0)
    {
        const double sr = engine.getSampleRate() > 0.0 ? engine.getSampleRate() : 48000.0;
        const int fadeLen = juce::jmax (8, (int) (0.010 * sr));
        const int seam = juce::jlimit (0, numSamples,
            (int) ((double) iWrapIn / juce::jmax (0.001, std::abs (glideRate))));

        auto applyDip = [&] (int chan)
        {
            float* d = buffer.getWritePointer (chan);

            for (int n = juce::jmax (0, seam - fadeLen); n < juce::jmin (seam, numSamples); ++n)
                d[n] *= (float) std::cos (0.5 * juce::MathConstants<double>::pi
                                          * (double) (n - (seam - fadeLen)) / (double) fadeLen);

            for (int n = seam; n < juce::jmin (seam + fadeLen, numSamples); ++n)
                d[n] *= (float) std::sin (0.5 * juce::MathConstants<double>::pi
                                          * (double) (n - seam) / (double) fadeLen);
        };

        for (int c = 0; c < numOutChannels; ++c)
            applyDip (c);
    }

    if (std::abs (outputGain - 1.0f) > 1.0e-5f)
        for (int c = 0; c < numOutChannels; ++c)
            buffer.applyGain (c, 0, numSamples, outputGain);

    if (! frozenState)
    {
        pos += backward ? -(int64_t) numIn : (int64_t) numIn;

        if (reachedEnd)
        {
            pos = juce::jlimit<int64_t> (regionA, regionB, pos);
            playing.store (false);
        }
        else if (loop && (pos >= regionB || pos < regionA))
        {
            pos = regionA + (((pos - regionA) % regionSpan) + regionSpan) % regionSpan;
            engine.reset(); // avoid smearing across the loop seam
        }
    }

    playPosition.store (pos);
    transportFraction.store ((double) pos / (double) len);
}

juce::AudioProcessorEditor* StretchAudioProcessor::createEditor()
{
    return new StretchAudioProcessorEditor (*this);
}

bool StretchAudioProcessor::hasEditor() const { return true; }

void StretchAudioProcessor::transportPlay()
{
    if (! engine.hasSource())
        return;

    if (! playing.load())
    {
        const double frac = transportFraction.load();
        // Rewind flips RATE sign; restart mirrors playback direction.
        const float time = parameters.getRawParameterValue ("TimeRatio")->load();
        const bool backward = (reversed.load() ? -time : time) < 0.0f;

        // Restart at the directional "end" when at end-of-file.
        const bool atEnd = backward ? (frac <= 0.001) : (frac >= 0.999);
        if (! looping.load() && atEnd)
            pendingSeek.store (backward ? engine.getSourceLength() : 0);

        playing.store (true);
    }
    else
    {
        playing.store (false); // toggle acts as pause
    }
}

void StretchAudioProcessor::transportPause()
{
    playing.store (false);
}

void StretchAudioProcessor::transportStop()
{
    playing.store (false);
    pendingSeek.store (0);
    transportFraction.store (0.0);
}

void StretchAudioProcessor::seekToFraction (double fraction)
{
    if (! engine.hasSource())
        return;

    fraction = juce::jlimit (0.0, 1.0, fraction);
    const int64_t len = (int64_t) engine.getSourceLength();
    pendingSeek.store ((int64_t) ((double) len * fraction));
    transportFraction.store (fraction);
}

// UI/keyboard drive the params; the listener mirrors into audio atomics.
void StretchAudioProcessor::setFrozen (bool b)
{
    auto* param = parameters.getParameter ("Freeze");
    if (param == nullptr)
        return;

    // Sync here too: a matching value skips host notify, so no listener fires.
    frozen.store (b);
    if ((param->getValue() > 0.5f) == b)
        return;

    param->beginChangeGesture();
    param->setValueNotifyingHost (b ? 1.0f : 0.0f);
    param->endChangeGesture();
}

void StretchAudioProcessor::setReversed (bool b)
{
    auto* param = parameters.getParameter ("Rewind");
    if (param == nullptr)
        return;

    reversed.store (b);
    if ((param->getValue() > 0.5f) == b)
        return;

    param->beginChangeGesture();
    param->setValueNotifyingHost (b ? 1.0f : 0.0f);
    param->endChangeGesture();
}

// Async load; newest request wins (old worker joins first).
void StretchAudioProcessor::loadAudioFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    if (fileLoader)
        fileLoader->stopThread (15000);

    // Remember now so a host saving state right after still sees the path;
    // cleared again if the decode fails.
    sourceFile = file;
    recordRecentFile (file);

    fileLoader = std::make_unique<FileLoadThread> (*this, file);
    fileLoader->startThread();
}

void StretchAudioProcessor::loadAudioFilePreservingView (const juce::File& file)
{
    pendingViewStateRestore.store (true); // keep restored view on the reload
    loadAudioFile (file);
}

void StretchAudioProcessor::FileLoadThread::run()
{
    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));

    if (reader == nullptr || threadShouldExit())
    {
        owner.sourceFile = juce::File(); // don't persist an unloadable path
        if (reader == nullptr)
        {
            DBG ("Stretch: no decoder for " << file.getFullPathName());
            auto callback = owner.onFileLoadFailed;
            const juce::String path = file.getFullPathName();
            juce::MessageManager::callAsync ([callback, path]
            {
                if (callback)
                    callback ("No decoder for:\n" + path
                              + "\n\nUnsupported or corrupt file.");
            });
        }
        return;
    }

    if (reader->lengthInSamples <= 0 || reader->numChannels <= 0)
    {
        DBG ("Stretch: empty stream for " << file.getFullPathName());
        owner.sourceFile = juce::File();
        auto callback = owner.onFileLoadFailed;
        const juce::String path = file.getFullPathName();
        juce::MessageManager::callAsync ([callback, path]
        {
            if (callback)
                callback ("No audio in:\n" + path);
        });
        return;
    }

    const double rate = reader->sampleRate;
    juce::AudioBuffer<float> decoded ((int) reader->numChannels,
                                      (int) reader->lengthInSamples);
    reader->read (&decoded, 0, (int) reader->lengthInSamples, 0, true, true);

    if (threadShouldExit())
    {
        DBG ("Stretch: load superseded for " << file.getFullPathName());
        return;
    }

    owner.installLoadedFile (std::move (decoded), rate, file);
}

void StretchAudioProcessor::installLoadedFile (juce::AudioBuffer<float>&& decoded,
                                               double sampleRate, const juce::File& /*file*/)
{
    {
        std::lock_guard<std::mutex> lock (processingMutex);
        originalBuffer = std::move (decoded);
        fileSampleRate = sampleRate;

        if (engine.getSampleRate() <= 0)
            engine.prepare (sampleRate, juce::jmax (1, getTotalNumOutputChannels()));

        engine.setSource (originalBuffer, sampleRate);

        if (! pendingViewStateRestore)
        {
            waveViewStart.store (0.0);
            waveViewLen.store (1.0);
            loopStart.store (0.0);
            loopEnd.store (1.0);
        }
        pendingViewStateRestore = false;
    }

    pendingSeek.store (0);
    transportFraction.store (0.0);
    playing.store (true); // start playback immediately on load
    DBG ("Stretch: loaded " << originalBuffer.getNumSamples()
         << " samples @ " << fileSampleRate << " Hz");

    if (onFileLoaded)
    {
        // Copy: the editor may swap/clear it while queued.
        auto callback = onFileLoaded;
        juce::MessageManager::callAsync ([callback] { callback(); });
    }
}

void StretchAudioProcessor::unloadSample()
{
    transportStop();

    std::lock_guard<std::mutex> lock (processingMutex);
    engine.clearSource();
    originalBuffer.setSize (0, 0);
    fileSampleRate = 0.0;
    sourceFile = juce::File();
}

// Recorded on the message thread (PropertiesFile isn't thread-safe).
void StretchAudioProcessor::recordRecentFile (const juce::File& file)
{
    auto& settings = getGlobalSettings();

    juce::StringArray list;
    list.addLines (settings.getValue ("recentFiles"));
    list.removeEmptyStrings();

    const juce::String path = file.getFullPathName();
    list.removeString (path, true);
    list.insert (0, path);

    while (list.size() > kMaxRecentFiles)
        list.remove (list.size() - 1);

    settings.setValue ("recentFiles", list.joinIntoString ("\n"));
    settings.saveIfNeeded();
}

juce::StringArray StretchAudioProcessor::getRecentFiles() const
{
    juce::StringArray list;
    list.addLines (getGlobalSettings().getValue ("recentFiles"));
    list.removeEmptyStrings();
    return list;
}

void StretchAudioProcessor::clearRecentFiles()
{
    auto& settings = getGlobalSettings();
    settings.removeValue ("recentFiles");
    settings.saveIfNeeded();
}

// Frozen/near-zero rates cap at two minutes of output.
static constexpr double kExportCapSeconds = 120.0;

// System-wide settings (%APPDATA%\BalamDSP\Stretch), shared by all formats.
static juce::PropertiesFile& getGlobalSettings()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName      = "Stretch";
    opts.folderName           = "BalamDSP/Stretch";
    opts.filenameSuffix       = ".settings";
    opts.storageFormat        = juce::PropertiesFile::storeAsXML;
    opts.commonToAllUsers     = false;
    opts.ignoreCaseOfKeyNames = false;

    static juce::PropertiesFile settings (opts);
    return settings;
}

void StretchAudioProcessor::setExportFolder (const juce::File& folder)
{
    exportFolder = folder;

    auto& settings = getGlobalSettings();
    if (exportFolder == juce::File())
        settings.removeValue ("exportFolder");
    else
        settings.setValue ("exportFolder", exportFolder.getFullPathName());
    settings.saveIfNeeded();
}

StretchAudioProcessor::ExportOptions StretchAudioProcessor::getExportOptions()
{
    auto& settings = getGlobalSettings();

    ExportOptions opts;
    opts.sampleRate = settings.getIntValue ("exportSampleRate", 0);
    opts.bitDepth   = settings.getIntValue ("exportBitDepth", 16);
    opts.format     = (ExportFormat) juce::jlimit (0, 2,
                        settings.getIntValue ("exportFormat", 0));

    // 32F is WAV-only; other formats clamp to 24.
    if (opts.bitDepth != 16 && opts.bitDepth != 24 && opts.bitDepth != 32)
        opts.bitDepth = 16;
    if (opts.bitDepth == 32 && opts.format != ExportFormat::wav)
        opts.bitDepth = 24;

    return opts;
}

void StretchAudioProcessor::setExportOptions (const ExportOptions& opts)
{
    auto& settings = getGlobalSettings();
    settings.setValue ("exportSampleRate", opts.sampleRate);
    settings.setValue ("exportBitDepth", opts.bitDepth);
    settings.setValue ("exportFormat", (int) opts.format);
    settings.saveIfNeeded();
}

bool StretchAudioProcessor::hasActiveSelection() const noexcept
{
    const double s = loopStart.load();
    const double e = loopEnd.load();
    return e > s + 1e-6 && (s > 0.0 || e < 1.0);
}

void StretchAudioProcessor::getExportRange (int totalSamples, int& startOut, int& lengthOut) const noexcept
{
    startOut = 0;
    lengthOut = totalSamples;

    if (totalSamples <= 0 || ! hasActiveSelection())
        return;

    const int start = (int) (juce::jlimit (0.0, 1.0, loopStart.load()) * (double) totalSamples);
    const int end = (int) std::ceil (juce::jlimit (0.0, 1.0, loopEnd.load()) * (double) totalSamples);

    const int s = juce::jlimit (0, totalSamples - 1, start);
    const int e = juce::jlimit (s + 1, totalSamples, end);

    if (e > s)
    {
        startOut = s;
        lengthOut = e - s;
    }
}

juce::int64 StretchAudioProcessor::estimateExportBytes() const
{
    if (! hasLoadedFile())
        return 0;

    int start = 0, length = originalBuffer.getNumSamples();
    getExportRange (originalBuffer.getNumSamples(), start, length);

    const double safeRate = juce::jlimit (0.05, 20.0,
        (double) std::abs (parameters.getRawParameterValue ("TimeRatio")->load()));

    double outputSamples = (double) length / safeRate;

    const ExportOptions opts = getExportOptions();
    if (opts.sampleRate > 0 && fileSampleRate > 0.0)
        outputSamples *= (double) opts.sampleRate / fileSampleRate;

    return (juce::int64) (outputSamples
                          * (double) originalBuffer.getNumChannels()
                          * (double) bytesPerSampleForDepth (opts.bitDepth));
}

bool StretchAudioProcessor::startBackgroundExport()
{
    if (! hasLoadedFile())
        return false;

    if (isExportRunning())
        return false;

    if (exportThread)
        exportThread->stopThread (15000); // join stale worker

    // Private copy under the mutex; backward reversal happens on the copy.
    juce::AudioBuffer<float> snapshot;
    double srcRate = 0.0;
    int selStart = 0, selLen = 0;

    {
        std::lock_guard<std::mutex> lock (processingMutex);
        snapshot = originalBuffer;
        srcRate = fileSampleRate;
        getExportRange (snapshot.getNumSamples(), selStart, selLen);
    }

    // Render the selection slice when one exists.
    if (selLen > 0 && selLen < snapshot.getNumSamples())
    {
        juce::AudioBuffer<float> sliced (snapshot.getNumChannels(), selLen);
        for (int c = 0; c < snapshot.getNumChannels(); ++c)
            sliced.copyFrom (c, 0, snapshot, c, selStart, selLen);
        snapshot = std::move (sliced);
    }

    exportCancelFlag.store (false);
    exportProgress.store (0.0);

    // Snapshot options here: PropertiesFile isn't thread-safe.
    const ExportOptions opts = getExportOptions();

    exportThread = std::make_unique<ExportThread> (*this, std::move (snapshot), srcRate, opts);
    exportThread->startThread();
    return true;
}

bool StretchAudioProcessor::isExportRunning() const noexcept
{
    return exportThread != nullptr && exportThread->isThreadRunning();
}

void StretchAudioProcessor::ExportThread::run()
{
    // Raw param slots are stable atomics (joined before members die).
    const float pitch = owner.parameters.getRawParameterValue ("PitchSemitones")->load();
    const float time = owner.parameters.getRawParameterValue ("TimeRatio")->load();
    // Rewind flips RATE sign; export mirrors playback.
    const float dirRate = owner.isReversed() ? -time : time;
    const float rateMag = std::abs (dirRate);
    const bool formantPreserve = owner.parameters.getRawParameterValue ("FormantPreserve")->load() > 0.5f;
    const float formantSemi = owner.parameters.getRawParameterValue ("FormantSemitones")->load();

    auto finish = [this] (bool ok, const juce::File& f, bool cancelled)
    {
        owner.exportProgress.store (cancelled ? 0.0 : 1.0);

        if (owner.onExportFinished)
        {
            auto callback = owner.onExportFinished; // editor may swap it
            juce::MessageManager::callAsync ([callback, ok, f, cancelled]
                { callback (ok, f, cancelled); });
        }
    };

    // Frozen/near-zero rates cap at two minutes.
    const int maxOutputSamples = (owner.isFrozen() || rateMag < 0.10f)
        ? (int) ((double) fileSampleRate * kExportCapSeconds)
        : 0;

    // Backward: stretcher runs forward only, so reverse and use magnitude.
    if (dirRate < 0.0f)
    {
        const int numCh = source.getNumChannels();
        const int numSamples = source.getNumSamples();

        for (int c = 0; c < numCh; ++c)
        {
            float* d = source.getWritePointer (c);
            for (int i = 0, j = numSamples - 1; i < j; ++i, --j)
                std::swap (d[i], d[j]);
        }
    }

    auto progressCb = [this] (double p) -> bool
    {
        owner.exportProgress.store (p);
        return ! owner.exportCancelFlag.load();
    };

    auto rendered = owner.engine.processOffline (
        source, fileSampleRate, pitch, rateMag,
        formantPreserve, formantSemi, maxOutputSamples, progressCb);

    if (owner.exportCancelFlag.load())
        { finish (false, {}, true); return; }

    if (rendered.getNumSamples() <= 0)
        { finish (false, {}, false); return; }

    // Fixed-rate export: resample the render (worker context, pure static).
    const int targetRate = options.sampleRate;
    if (targetRate > 0 && fileSampleRate > 0.0
        && std::abs (fileSampleRate - (double) targetRate) >= 1.0)
    {
        juce::AudioBuffer<float> converted;
        StretchEngine::resampleInto (rendered, fileSampleRate,
                                     converted, (double) targetRate);
        rendered = std::move (converted);

        if (rendered.getNumSamples() <= 0)
            { finish (false, {}, false); return; }
    }

    const double outRate = (targetRate > 0) ? (double) targetRate : fileSampleRate;

    const char* extension = ".wav";
    std::unique_ptr<juce::AudioFormat> format;

    if (options.format == ExportFormat::aiff)
    {
        format = std::make_unique<juce::AiffAudioFormat>();
        extension = ".aiff";
    }
    else if (options.format == ExportFormat::flac)
    {
        format = std::make_unique<juce::FlacAudioFormat>();
        extension = ".flac";
    }
    else
    {
        format = std::make_unique<juce::WavAudioFormat>();
        extension = ".wav";
    }

    const juce::File targetDir = owner.hasExportFolder()
        ? owner.getExportFolder()
        : juce::File::getSpecialLocation (juce::File::tempDirectory);

    juce::File out = targetDir.getChildFile ("stretch_export_"
        + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + extension);

    out.deleteFile();

    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    if (stream == nullptr)
        { finish (false, {}, false); return; }

    // 32-bit WAV = IEEE float; AIFF/FLAC clamp to 24 (re-checked here).
    int bits = options.bitDepth;
    if (options.format != ExportFormat::wav && bits == 32)
        bits = 24;

    juce::AudioFormatWriterOptions writerOptions;
    writerOptions = writerOptions.withSampleRate (outRate)
        .withNumChannels ((int) rendered.getNumChannels())
        .withBitsPerSample (bits);

    if (options.format == ExportFormat::wav && bits == 32)
        writerOptions = writerOptions.withSampleFormat (
            juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);

    std::unique_ptr<juce::AudioFormatWriter> writer (
        format->createWriterFor (stream, writerOptions));

    if (writer == nullptr)
        { finish (false, {}, false); return; }

    stream.release(); // writer owns the stream now

    writer->writeFromAudioSampleBuffer (rendered, 0, rendered.getNumSamples());
    writer.reset();

    finish (true, out, false);
}

// One decimal, e.g. "-12.0 st".
static juce::String semitonesText (float v)
{
    return juce::String (v, 1) + " st";
}

juce::AudioProcessorValueTreeState::ParameterLayout StretchAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    using SemiAttributes = juce::AudioParameterFloatAttributes;

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "PitchSemitones", "Pitch",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.01f, 1.0f),
        0.0f,
        SemiAttributes().withLabel ("st")
            .withStringFromValueFunction ([] (float v, int)
                { return semitonesText (v); })
            .withValueFromStringFunction ([] (const juce::String& s)
                { return s.retainCharacters ("-+.0123456789").getFloatValue(); })));

    // RATE -400..+400%; negative = backward. 0% centre, +-100% quarter points.
    juce::NormalisableRange<float> rateRange (-4.0f, 4.0f, 0.01f, 0.5f, true);

    using RateAttributes = juce::AudioParameterFloatAttributes;
    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "TimeRatio", "Rate",
        rateRange,
        1.0f,
        RateAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int)
                { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& s)
                { return s.retainCharacters ("-+.0123456789").getFloatValue() / 100.0f; })));

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "FormantPreserve", "Formant Preserve", false));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "FormantSemitones", "Formant Shift",
        juce::NormalisableRange<float> (-12.0f, 12.0f, 0.01f, 1.0f),
        0.0f,
        SemiAttributes().withLabel ("st")
            .withStringFromValueFunction ([] (float v, int)
                { return semitonesText (v); })
            .withValueFromStringFunction ([] (const juce::String& s)
                { return s.retainCharacters ("-+.0123456789").getFloatValue(); })));

    params.push_back (std::make_unique<juce::AudioParameterFloat>(
        "OutputGain", "Master",
        juce::NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 1.0f),
        0.0f));

    // FREEZE/REWIND: automatable; UI + shortcuts drive the params, audio
    // reads the mirrored atomics.
    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "Freeze", "Freeze", false));

    params.push_back (std::make_unique<juce::AudioParameterBool>(
        "Rewind", "Rewind", false));

    // UI zoom: persisted choice; the editor resizes off it.
    juce::StringArray zoomChoices;
    for (const auto p : StretchZoom::ZOOM_PERCENTS)
        zoomChoices.add (juce::String ((int) p) + "%");
    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        StretchZoom::UI_SCALE_ID, "UI Scale", zoomChoices, StretchZoom::UI_SCALE_DEFAULT));

    return { params.begin(), params.end() };
}

void StretchAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree tree ("StretchState");

    tree.setProperty ("Pitch", parameters.getRawParameterValue ("PitchSemitones")->load(), nullptr);
    tree.setProperty ("Time", parameters.getRawParameterValue ("TimeRatio")->load(), nullptr);
    tree.setProperty ("Formant", parameters.getRawParameterValue ("FormantPreserve")->load(), nullptr);
    tree.setProperty ("FormantShift", parameters.getRawParameterValue ("FormantSemitones")->load(), nullptr);
    tree.setProperty ("Gain", parameters.getRawParameterValue ("OutputGain")->load(), nullptr);

    // Toggles ride in state for session reload (automation overwrites).
    tree.setProperty ("Frozen", frozen.load(), nullptr);
    tree.setProperty ("Rewinded", reversed.load(), nullptr);

    // Project recall: source path only (audio is too large for state).
    tree.setProperty ("SourcePath", sourceFile.getFullPathName(), nullptr);

    // CRT overlay: file may be unwritable (sandboxed AU), so state carries it.
    tree.setProperty ("CrtEnabled", crtEnabled.load(), nullptr);
    tree.setProperty ("CrtStrength", crtStrength.load(), nullptr);

    // View + selection persist across editor open/close and presets.
    tree.setProperty ("WaveViewStart", waveViewStart.load(), nullptr);
    tree.setProperty ("WaveViewLen",  waveViewLen.load(), nullptr);
    tree.setProperty ("LoopStart",   loopStart.load(), nullptr);
    tree.setProperty ("LoopEnd",     loopEnd.load(), nullptr);

    // Export folder NOT saved here: system-wide, shared by all instances.
    juce::MemoryOutputStream stream;
    tree.writeToStream (stream);
    destData.setSize (stream.getDataSize());
    destData.copyFrom (stream.getData(), 0, stream.getDataSize());
}

void StretchAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::MemoryInputStream stream (data, (size_t) sizeInBytes, false);
    juce::ValueTree tree = juce::ValueTree::readFromStream (stream);

    if (tree.isValid())
    {
        if (tree.hasProperty ("Pitch"))
            parameters.getParameter ("PitchSemitones")->setValueNotifyingHost (
                parameters.getParameter ("PitchSemitones")->convertTo0to1 (
                    (float) tree.getProperty ("Pitch", 0.0f)));
        if (tree.hasProperty ("Time"))
            parameters.getParameter ("TimeRatio")->setValueNotifyingHost (
                parameters.getParameter ("TimeRatio")->convertTo0to1 (
                    (float) tree.getProperty ("Time", 1.0f)));
        if (tree.hasProperty ("Formant"))
            parameters.getParameter ("FormantPreserve")->setValueNotifyingHost (
                (float) tree.getProperty ("Formant", false));
        if (tree.hasProperty ("FormantShift"))
            parameters.getParameter ("FormantSemitones")->setValueNotifyingHost (
                parameters.getParameter ("FormantSemitones")->convertTo0to1 (
                    (float) tree.getProperty ("FormantShift", 0.0f)));
        if (tree.hasProperty ("Gain"))
            parameters.getParameter ("OutputGain")->setValueNotifyingHost (
                parameters.getParameter ("OutputGain")->convertTo0to1 (
                    (float) tree.getProperty ("Gain", 0.0f)));

        if (tree.hasProperty ("Frozen"))
            setFrozen ((bool) tree.getProperty ("Frozen", false));
        if (tree.hasProperty ("Rewinded"))
            setReversed ((bool) tree.getProperty ("Rewinded", false));

        // CRT overlay (also re-persisted to file best-effort by the setters).
        if (tree.hasProperty ("CrtStrength"))
            setCrtStrength ((int) tree.getProperty ("CrtStrength", 2));
        if (tree.hasProperty ("CrtEnabled"))
            setCrtEnabled ((bool) tree.getProperty ("CrtEnabled", true));

        // View + selection; the editor re-syncs from these.
        if (tree.hasProperty ("WaveViewStart"))
            setWaveViewStart ((double) tree.getProperty ("WaveViewStart", 0.0));
        if (tree.hasProperty ("WaveViewLen"))
            setWaveViewLen ((double) tree.getProperty ("WaveViewLen", 1.0));
        if (tree.hasProperty ("LoopStart") && tree.hasProperty ("LoopEnd"))
            setLoopRegion ((double) tree.getProperty ("LoopStart", 0.0),
                           (double) tree.getProperty ("LoopEnd", 1.0));

        // Project recall via the async load path; missing files skip silently.
        const auto sourcePath = tree.getProperty ("SourcePath").toString();
        const juce::File file (sourcePath);

        if (file.existsAsFile())
        {
            if (! (hasLoadedFile() && file == sourceFile))
            {
                pendingViewStateRestore = true; // keep restored view on reload
                loadAudioFile (file);
            }
            else
            {
                pendingViewStateRestore = false;
                auto cb = onViewStateRestored;
                if (cb)
                    juce::MessageManager::callAsync ([cb] { cb(); });
            }
        }
        else if (hasLoadedFile())
        {
            unloadSample(); // no path -> nothing loaded
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new StretchAudioProcessor();
}
