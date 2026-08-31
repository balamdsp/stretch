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

    // Decodes on a worker thread (never blocks the message thread); the
    // newest request wins. Completion fires onFileLoaded on the message
    // thread. Used by drag-drop, recent files and session recall alike.
    void loadAudioFile (const juce::File& file);
    void unloadSample();
    bool hasLoadedFile() const noexcept { return originalBuffer.getNumSamples() > 0; }
    const juce::AudioBuffer<float>& getOriginalBuffer() const { return originalBuffer; }
    double getFileSampleRate() const { return fileSampleRate; }

    // File the current/last load came from; persisted in plugin state so a
    // host session restore can re-request it (project recall).
    const juce::File& getSourceFile() const noexcept { return sourceFile; }

    // Global list (%APPDATA%\BalamDSP\Stretch) shared by every format and
    // instance, like the export folder. Move-to-front, capped.
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

    // Fractions of the source (0..1). Full range when no selection exists,
    // so the transport wraps the whole file exactly as before. The waveform
    // selection is the single writer; processBlock reads both atomics.
    void setLoopRegion (double start, double end) noexcept
    {
        loopStart.store (juce::jlimit (0.0, 1.0, start));
        loopEnd.store (juce::jlimit (0.0, 1.0, end));
    }
    double getLoopStart() const noexcept { return loopStart.load(); }
    double getLoopEnd() const noexcept { return loopEnd.load(); }

    // Current RATE parameter as a raw multiplier (1.0 = 100 %).
    float getRateValue() const
    {
        return parameters.getRawParameterValue ("TimeRatio")->load();
    }

    juce::File getExportFolder() const { return exportFolder; }

    // Persists system-wide (%APPDATA%\BalamDSP\Stretch) so every format and
    // instance remembers it until reset or replaced.
    void setExportFolder (const juce::File& folder);

    bool hasExportFolder() const { return exportFolder.isDirectory(); }

    // Starts rendering the CURRENT settings offline on a worker thread to a
    // timestamped WAV in the export folder (or temp when unset). Returns
    // false when nothing is loaded or an export is already running.
    bool startBackgroundExport();

    bool isExportRunning() const noexcept;

    // Render progress 0..1 (valid while an export runs).
    double getExportProgress() const noexcept { return exportProgress.load(); }

    // Asks the running export to stop at the next chunk boundary (~100 ms).
    void cancelExport() noexcept { exportCancelFlag.store (true); }

    // Completion fires ON THE MESSAGE THREAD: success=false means failure
    // (or cancel, see flag), outFile is empty unless success=true.
    std::function<void (bool success, juce::File outFile, bool cancelled)> onExportFinished;

    // Exports above this size raise a warning before rendering.
    static constexpr juce::int64 kMaxExportBytes = 100 * 1024 * 1024;

    // Global (system-wide) choices shared by every format/instance, applied
    // by the background render: WAV / AIFF / FLAC, 16 / 24 / 32-float bits
    // (32F is WAV-only; non-WAV formats clamp to 24), source or fixed rate.
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

    // Rough WAV/AIFF/FLAC size (bytes) an export would produce right now
    // with the current global export options; 0 when no source is loaded.
    juce::int64 estimateExportBytes() const;

    bool isCrtEnabled() const noexcept { return crtEnabled.load(); }
    void setCrtEnabled (bool b) noexcept { crtEnabled = b; }
    const std::atomic<bool>& getCrtEnabledFlag() const noexcept { return crtEnabled; }

    // Runtime wrapper check. Plugin sources compile once into the shared
    // code lib, so JUCE_STANDALONE_APPLICATION is 1 for ALL formats here —
    // only wrapperType knows how this instance was actually loaded.
    bool isRunningAsStandalone() const noexcept
    {
        return wrapperType == juce::AudioProcessor::wrapperType_Standalone;
    }

    // Tape-style freeze: holds the source window under the stretcher so the
    // current texture sustains indefinitely. Automatable: backed by the
    // "Freeze" APVTS parameter; the atomic is the audio-thread's copy.
    bool isFrozen() const noexcept { return frozen.load(); }
    void setFrozen (bool b);

    // REWIND button: latching reverse playback -- inverts the RATE sign so
    // the material plays backwards at the same rate. Automatable via the
    // "Rewind" APVTS parameter.
    bool isReversed() const noexcept { return reversed.load(); }
    void setReversed (bool b);

    std::function<void()> onFileLoaded;

private:
    // APVTS listener: mirrors the Freeze/Rewind parameters into the audio
    // thread's atomics and applies the engage side-effects once, no matter
    // who moved the value (host automation, UI toggle, keyboard shortcut).
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Decodes an audio file on this worker thread and installs it into the
    // processor (see loadAudioFile). One live instance at a time: replacing
    // the unique_ptr joins the old worker first, which discards any partial
    // result and makes the newest request authoritative.
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

    // Worker-side completion of a decode: swaps buffers under the mutex,
    // restarts transport and posts onFileLoaded to the message thread.
    void installLoadedFile (juce::AudioBuffer<float>&& decoded,
                            double sampleRate, const juce::File& file);

    // Renders a private snapshot of the source offline and writes the WAV/
    // AIFF/FLAC file. The snapshot decouples the render from live state: the
    // loader thread may swap originalBuffer mid-export without tearing it.
    // Progress is published to exportProgress; exportCancelFlag stops it
    // chunk-wise. Options are snapshotted on the message thread at start.
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
    std::atomic<bool> frozen { false };
    std::atomic<bool> reversed { false };
    std::mutex processingMutex;                // guards source swaps vs audio thread

    std::atomic<bool> playing { false };
    std::atomic<bool> looping { true };
    std::atomic<double> loopStart { 0.0 };     // loop region (fractions 0..1)
    std::atomic<double> loopEnd { 1.0 };
    std::atomic<int64_t> playPosition { 0 };   // source-domain samples (device rate)
    std::atomic<int64_t> pendingSeek { -1 };
    std::atomic<double> transportFraction { 0.0 };

    // Freeze de-click: the effective rate GLIDES toward its target over
    // ~20 ms whenever FREEZE engages/disengages, so the stretcher's input
    // consumption tapers smoothly instead of stepping (grain-boundary click).
    // Audio-thread only, guarded by processingMutex.
    double glideRate = 1.0;

    // Audio-thread scratch (sized for worst-case input consumption at ratio 0.25).
    juce::AudioBuffer<float> scratchInput;
    std::vector<float*> scratchInPtrs;
    std::vector<const float*> srcPtrs;
    std::vector<float*> outPtrs;

    juce::File sourceFile;                      // last requested/successful load

    std::atomic<double> exportProgress { 0.0 };
    std::atomic<bool> exportCancelFlag { false };

    // Declared LAST so they are destroyed FIRST (before engine/buffers they
    // touch); the destructor also joins them explicitly.
    std::unique_ptr<FileLoadThread> fileLoader;
    std::unique_ptr<ExportThread> exportThread;

    void ensureDeviceSource();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchAudioProcessor)
};
