#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <atomic>
#include <array>
#include <functional>
#include <memory>
#include <mutex>

#include "Core/StretchEngine.h"
#include "Helpers/StretchPresets.h"

// Persistent UI zoom. Choice parameter, 100% floor.
namespace StretchZoom
{
    inline constexpr const char* UI_SCALE_ID = "ui_scale";
    inline constexpr std::array<float, 5> ZOOM_PERCENTS { 100.0f, 125.0f, 150.0f, 200.0f, 300.0f };
    inline constexpr int UI_SCALE_DEFAULT = 0;   // index of 100%
}

class StretchAudioProcessor : public juce::AudioProcessor,
                              private juce::AudioProcessorValueTreeState::Listener
{
public:
    using juce::AudioProcessor::processBlock;

    StretchAudioProcessor();
    ~StretchAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
#endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState parameters;

    // Papalote-style preset session (Init/Save/Save-As tracking).
    StretchPresets::Session presetSession;

    // Async decode; newest request wins, completion on the message thread.
    void loadAudioFile (const juce::File& file);
    // Same, but keeps the current view/selection across the reload.
    void loadAudioFilePreservingView (const juce::File& file);
    void unloadSample();
    bool hasLoadedFile() const noexcept { return originalBuffer.getNumSamples() > 0; }
    const juce::AudioBuffer<float>& getOriginalBuffer() const { return originalBuffer; }
    double getFileSampleRate() const { return fileSampleRate; }

    // Current source file; persisted in state for session recall.
    const juce::File& getSourceFile() const noexcept { return sourceFile; }

    // Global recent list (%APPDATA%\BalamDSP\Stretch); move-to-front, capped.
    static constexpr int kMaxRecentFiles = 10;
    void recordRecentFile (const juce::File& file);
    juce::StringArray getRecentFiles() const;
    void clearRecentFiles();

    void transportPlay();
    void transportPause();
    void transportStop();
    void setLooping (bool shouldLoop) noexcept { looping.store (shouldLoop); }
    bool isPlaying() const noexcept { return playing.load(); }
    bool isLooping() const noexcept { return looping.load(); }
    void seekToFraction (double fraction);
    double getTransportFraction() const noexcept { return transportFraction.load(); }

    // Loop-region fractions (0..1); full range when no selection exists.
    void setLoopRegion (double start, double end) noexcept
    {
        loopStart.store (juce::jlimit (0.0, 1.0, start));
        loopEnd.store (juce::jlimit (0.0, 1.0, end));
    }
    // (0,1) means none; export renders the slice when active.
    bool hasActiveSelection() const noexcept;
    void getExportRange (int totalSamples, int& startOut, int& lengthOut) const noexcept;
    double getLoopStart() const noexcept { return loopStart.load(); }
    double getLoopEnd() const noexcept { return loopEnd.load(); }

    double getWaveViewStart() const noexcept { return waveViewStart.load(); }
    double getWaveViewLen()  const noexcept { return waveViewLen.load(); }
    void setWaveViewStart (double s) noexcept { waveViewStart.store (juce::jlimit (0.0, 1.0, s)); }
    void setWaveViewLen  (double l) noexcept { waveViewLen.store (juce::jlimit (0.0, 1.0, l)); }

    std::function<void()> onViewStateRestored;

    // RATE as a raw multiplier (1.0 = 100 %).
    float getRateValue() const
    {
        return parameters.getRawParameterValue ("TimeRatio")->load();
    }

    juce::File getExportFolder() const { return exportFolder; }

    // System-wide folder shared by every format/instance.
    void setExportFolder (const juce::File& folder);

    bool hasExportFolder() const { return exportFolder.isDirectory(); }

    // Offline render of current settings to a timestamped file (export
    // folder, else temp). False when empty or already running.
    bool startBackgroundExport();

    bool isExportRunning() const noexcept;

    // Render progress 0..1.
    double getExportProgress() const noexcept { return exportProgress.load(); }

    // Stop at the next chunk boundary.
    void cancelExport() noexcept { exportCancelFlag.store (true); }

    // Fires on the message thread; outFile empty unless success.
    std::function<void (bool success, juce::File outFile, bool cancelled)> onExportFinished;

    // Above this size, warn before rendering.
    static constexpr juce::int64 kMaxExportBytes = 100 * 1024 * 1024;

    // System-wide choices for the background render. 32F is WAV-only;
    // other formats clamp to 24. Rate 0 = source rate.
    enum class ExportFormat { wav = 0, aiff = 1, flac = 2 };

    struct ExportOptions
    {
        int sampleRate = 0;      // 0 = use the source's rate
        int bitDepth = 16;       // 16 | 24 | 32 (32 == IEEE float)
        ExportFormat format = ExportFormat::wav;
    };

    static ExportOptions getExportOptions();
    static void setExportOptions (const ExportOptions& opts);

    // Bytes per sample for the given depth (32 counts as float storage).
    static int bytesPerSampleForDepth (int bitDepth) noexcept
    {
        return bitDepth >= 32 ? 4 : (bitDepth >= 24 ? 3 : 2);
    }

    // Rough export size in bytes with current options; 0 when empty.
    juce::int64 estimateExportBytes() const;

    bool isCrtEnabled() const noexcept { return crtEnabled.load(); }
    void setCrtEnabled (bool b);
    const std::atomic<bool>& getCrtEnabledFlag() const noexcept { return crtEnabled; }

    // CRT strength 0/1/2 = Low/Med/High; machine-wide via settings.xml.
    int getCrtStrength() const noexcept { return crtStrength.load(); }
    void setCrtStrength (int strength);

    // JUCE_STANDALONE_APPLICATION is 1 for all formats here (shared lib);
    // only wrapperType tells how this instance was loaded.
    bool isRunningAsStandalone() const noexcept
    {
        return wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    }

    // Freeze: rate 0 holds the stretcher grains (sustains the texture).
    // Backed by the "Freeze" param; the atomic is the audio-thread copy.
    bool isFrozen() const noexcept { return frozen.load(); }
    void setFrozen (bool b);

    // Rewind: latching reverse; flips the RATE sign at the same rate.
    bool isReversed() const noexcept { return reversed.load(); }
    void setReversed (bool b);

    std::function<void()> onFileLoaded;

    // Fires on the message thread when a decode fails, with a reason.
    std::function<void (const juce::String& reason)> onFileLoadFailed;

private:
    // Mirrors Freeze/Rewind params into audio atomics + engage side-effects.
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Decode worker; newest request wins (old worker joins first).
    class FileLoadThread : public juce::Thread
    {
    public:
        FileLoadThread (StretchAudioProcessor& ownerProc, const juce::File& f)
            : juce::Thread ("StretchFileLoader"), owner (ownerProc), file (f) {}

        void run() override;

    private:
        StretchAudioProcessor& owner;
        const juce::File file;
    };

    // Swaps decoded buffers under the mutex, posts onFileLoaded.
    void installLoadedFile (juce::AudioBuffer<float>&& decoded,
                            double sampleRate, const juce::File& file);

    // Offline render on a private source snapshot (loader can't tear it).
    // Options snapshot on the message thread; progress/cancel chunk-wise.
    class ExportThread : public juce::Thread
    {
    public:
        ExportThread (StretchAudioProcessor& ownerProc, juce::AudioBuffer<float>&& src,
                      double rate, const ExportOptions& opts)
            : juce::Thread ("StretchExport"), owner (ownerProc),
              source (std::move (src)), fileSampleRate (rate), options (opts) {}

        void run() override;

    private:
        StretchAudioProcessor& owner;
        juce::AudioBuffer<float> source;         // mutable: reversed in place for backward exports
        const double fileSampleRate;
        const ExportOptions options;
    };

    StretchEngine engine;

    juce::AudioBuffer<float> originalBuffer;   // at the FILE's sample rate
    double fileSampleRate = 0.0;

    juce::File exportFolder;

    std::atomic<bool> crtEnabled { true };
    std::atomic<int>  crtStrength { 2 };
    std::atomic<bool> frozen { false };
    std::atomic<bool> reversed { false };
    std::mutex processingMutex;                // guards source swaps vs audio thread

    std::atomic<bool> playing { false };
    std::atomic<bool> looping { true };
    std::atomic<double> loopStart { 0.0 };     // loop region (fractions 0..1)
    std::atomic<double> loopEnd { 1.0 };
    std::atomic<double> waveViewStart { 0.0 };  // waveform scroll position
    std::atomic<double> waveViewLen  { 1.0 };   // waveform zoom level
    std::atomic<int64_t> playPosition { 0 };   // source-domain samples (device rate)
    std::atomic<int64_t> pendingSeek { -1 };
    std::atomic<double> transportFraction { 0.0 };

    // Freeze glide: effective rate eases over ~20ms to avoid grain clicks.
    // Audio-thread only, under processingMutex.
    double glideRate = 1.0;

    // Scratch sized for worst-case consumption at ratio 0.25.
    juce::AudioBuffer<float> scratchInput;
    std::vector<float*> scratchInPtrs;
    std::vector<const float*> srcPtrs;
    std::vector<float*> outPtrs;

    juce::File sourceFile;                      // last requested/successful load

    std::atomic<double> exportProgress { 0.0 };
    std::atomic<bool> exportCancelFlag { false };

    // Set by setStateInformation before reload; consumed by installLoadedFile.
    std::atomic<bool> pendingViewStateRestore { false };

    // Declared LAST: destroyed FIRST (destructor also joins them).
    std::unique_ptr<FileLoadThread> fileLoader;
    std::unique_ptr<ExportThread> exportThread;

    void ensureDeviceSource();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchAudioProcessor)
};
