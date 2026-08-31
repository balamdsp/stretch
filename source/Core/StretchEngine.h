#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

#include <cmath>
#include <functional>
#include <vector>

// ---------------------------------------------------------------------------
// StretchEngine - wraps the Signalsmith stretcher with two modes:
//
//   Streaming (audio thread): the processor feeds sequential chunks of the
//   device-rate source buffer through renderBlock(); the time ratio comes
//   from the input/output sample counts (this vendored API has no
//   setTimeFactor - see inputSamplesForOutput()). Pitch/formant targets are
//   cheap setters applied once per block by the processor.
//
//   Offline (export): processOffline() runs a dedicated local stretcher so
//   it never races the audio thread's instance.
// ---------------------------------------------------------------------------
class StretchEngine
{
public:
    StretchEngine() = default;

    void prepare (double sampleRate, int channels)
    {
        currentSampleRate = sampleRate;
        currentChannels = juce::jmax (1, channels);
        stretcher.presetDefault (currentChannels, currentSampleRate);
    }

    void reset() { stretcher.reset(); }

    // Message thread: build/install the device-rate copy of the loaded file.
    void setSource (const juce::AudioBuffer<float>& fileBuffer, double fileSampleRate)
    {
        if (currentSampleRate <= 0 || currentChannels <= 0)
            return;

        resampleInto (fileBuffer, fileSampleRate, sourceBuffer, currentSampleRate);
    }

    bool hasSource() const noexcept { return sourceBuffer.getNumSamples() > 0; }
    int  getSourceLength() const noexcept { return sourceBuffer.getNumSamples(); }
    int  getSourceChannels() const noexcept { return sourceBuffer.getNumChannels(); }
    const juce::AudioBuffer<float>& getSource() const noexcept { return sourceBuffer; }
    void clearSource() { sourceBuffer.setSize (0, 0); }

    // Cheap per-block stretcher configuration (audio thread).
    void updateStretcherParams (float pitchSemitones, bool formantPreserve, float formantSemitones)
    {
        stretcher.setTransposeSemitones (pitchSemitones);

        if (formantPreserve)
            stretcher.setFormantFactor (std::pow (2.0f, formantSemitones / 12.0f), true);
        else
            stretcher.setFormantFactor (1.0f, false);
    }

    // Source-domain samples consumed (MAGNITUDE ONLY) to produce numOutput
    // samples. RATE semantics: 100 % consumes 1:1, 400 % four times as fast,
    // a ratio of 0 consumes nothing -> the stretcher holds its grains
    // (stillness / playback freeze). Direction is the caller's concern: a
    // negative rate plays backward, feeding the source in decreasing order.
    static int inputSamplesForOutput (int numOutput, float rate) noexcept
    {
        const double safeRate = juce::jlimit (0.0, 20.0, (double) std::abs (rate));
        return (int) std::llround ((double) numOutput * safeRate);
    }

    // Audio thread: stretch numInput source samples into numOutput samples.
    void renderBlock (const float* const* inputChannelData, int numInputChannels, int numInputSamples,
                      float* const* outputChannelData, int numOutputChannels, int numOutputSamples)
    {
        struct InAccessor
        {
            const float* const* ch;
            const float* operator[] (int c) const { return ch[c]; }
        };
        struct OutAccessor
        {
            float* const* ch;
            float* operator[] (int c) const { return ch[c]; }
        };

        InAccessor inAcc { inputChannelData };
        OutAccessor outAcc { outputChannelData };

        stretcher.process (inAcc, numInputSamples, outAcc, numOutputSamples);
    }

    double getSampleRate() const noexcept { return currentSampleRate; }
    int getChannels() const noexcept { return currentChannels; }

    // Catmull-Rom resample of a whole buffer (non-realtime contexts: load
    // time and the export worker). Public so the exporter can convert its
    // render to a user-chosen fixed sample rate.
    static void resampleInto (const juce::AudioBuffer<float>& src, double srcRate,
                              juce::AudioBuffer<float>& dst, double dstRate)
    {
        if (src.getNumSamples() <= 0 || srcRate <= 0.0 || dstRate <= 0.0)
            return;

        if (std::abs (srcRate - dstRate) < 1.0)
        {
            dst = src;
            return;
        }

        const double ratio = srcRate / dstRate; // >1: downsample
        const int outLen = juce::jmax (1, (int) std::ceil ((double) src.getNumSamples() / ratio));

        dst.setSize (src.getNumChannels(), outLen);

        for (int c = 0; c < src.getNumChannels(); ++c)
        {
            const float* s = src.getReadPointer (c);
            float* d = dst.getWritePointer (c);
            const int lastIdx = src.getNumSamples() - 1;

            for (int i = 0; i < outLen; ++i)
            {
                const double p = (double) i * ratio;
                const int i1 = juce::jlimit (0, lastIdx, (int) std::floor (p));
                const int i0 = juce::jmax (i1 - 1, 0);
                const int i2 = juce::jmin (i1 + 1, lastIdx);
                const int i3 = juce::jmin (i1 + 2, lastIdx);
                const float t = (float) (p - (double) i1);

                d[i] = 0.5f * ((2.0f * s[i1])
                    + (-s[i0] + s[i2]) * t
                    + (2.0f * s[i0] - 5.0f * s[i1] + 4.0f * s[i2] - s[i3]) * t * t
                    + (-s[i0] + 3.0f * s[i1] - 3.0f * s[i2] + s[i3]) * t * t * t);
            }
        }
    }

    // Message thread (export): full offline render with a private stretcher.
    // timeStretchRatio is a MAGNITUDE (backward exports pass a reversed
    // buffer); maxOutputSamples > 0 hard-caps the rendered length.
    //
    // progress: called after each chunk with 0..1; return false to cancel
    // (the render then returns an empty buffer). Rendering is chunked so a
    // background export can report progress and stay cancellable — the
    // stretcher is a streaming STFT, sequential process() calls are its
    // native mode, so chunking is continuity-safe by construction.
    using OfflineProgressFn = std::function<bool (double progress)>;

    juce::AudioBuffer<float> processOffline (
        const juce::AudioBuffer<float>& input,
        double sourceSampleRate,
        float pitchSemitones,
        float timeStretchRatio,
        bool formantPreserve,
        float formantSemitones,
        int maxOutputSamples = 0,
        const OfflineProgressFn& progress = {})
    {
        if (sourceSampleRate <= 0)
            return {};

        const int inputSamples = input.getNumSamples();
        const int numChannels = juce::jmax (1, input.getNumChannels());

        if (inputSamples <= 0)
            return {};

        signalsmith::stretch::SignalsmithStretch<float> offlineStretcher;
        offlineStretcher.presetDefault (numChannels, sourceSampleRate);
        offlineStretcher.setTransposeSemitones (pitchSemitones);

        if (formantPreserve)
            offlineStretcher.setFormantFactor (std::pow (2.0f, formantSemitones / 12.0f), true);
        else
            offlineStretcher.setFormantFactor (1.0f, false);

        // RATE semantics: faster playback -> shorter output.
        const double safeRate = juce::jlimit (0.05, 20.0, (double) std::abs (timeStretchRatio));
        int outputSamples = juce::jmax (1, (int) ((double) inputSamples / safeRate));

        if (maxOutputSamples > 0)
            outputSamples = juce::jmin (outputSamples, maxOutputSamples);

        juce::AudioBuffer<float> output (numChannels, outputSamples);

        std::vector<const float*> inputPtrs ((size_t) numChannels);
        std::vector<float*> outputPtrs ((size_t) numChannels);
        for (int c = 0; c < numChannels; ++c)
        {
            inputPtrs[(size_t) c] = input.getReadPointer (c);
            outputPtrs[(size_t) c] = output.getWritePointer (c);
        }

        struct InAccessor
        {
            const float* const* ch;
            const float* operator[] (int c) const { return ch[c]; }
        };
        struct OutAccessor
        {
            float* const* ch;
            float* operator[] (int c) const { return ch[c]; }
        };

        // ~170 ms of audio at 48 kHz per chunk: fine enough for a smooth
        // progress bar, coarse enough that per-call overhead is negligible.
        constexpr int kChunkOut = 8192;

        int outDone = 0;
        int inDone = 0;

        while (outDone < outputSamples)
        {
            const bool lastChunk = (outputSamples - outDone) <= kChunkOut;
            const int chunkOut = juce::jmin (kChunkOut, outputSamples - outDone);

            // Match the aggregate consumption exactly: the final chunk takes
            // whatever input remains, absorbing per-chunk rounding.
            int chunkIn = juce::jmin (
                StretchEngine::inputSamplesForOutput (chunkOut, (float) safeRate),
                inputSamples - inDone);
            if (lastChunk)
                chunkIn = inputSamples - inDone;

            offlineStretcher.process (InAccessor { inputPtrs.data() + 0 },
                                      chunkIn,
                                      OutAccessor { outputPtrs.data() + 0 },
                                      chunkOut);

            // Sequential chunks continue where the previous one stopped.
            for (int c = 0; c < numChannels; ++c)
            {
                inputPtrs[(size_t) c] += chunkIn;
                outputPtrs[(size_t) c] += chunkOut;
            }

            inDone += chunkIn;
            outDone += chunkOut;

            if (progress && ! progress ((double) outDone / (double) outputSamples))
                return {}; // cancelled
        }

        return output;
    }

private:
    signalsmith::stretch::SignalsmithStretch<float> stretcher;
    juce::AudioBuffer<float> sourceBuffer; // device-rate copy (guarded by processor's mutex)

    double currentSampleRate = 0;
    int currentChannels = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StretchEngine)
};
