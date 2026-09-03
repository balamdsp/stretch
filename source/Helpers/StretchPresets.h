#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include "StretchSettings.h"

// User presets: normalized values (0..1) in the global preset folder.
// FREEZE/REWIND excluded (recall must not freeze or reverse).
namespace StretchPresets
{
    static constexpr const char* kParamIds[] = {
        "PitchSemitones", "TimeRatio", "FormantPreserve", "FormantSemitones", "OutputGain"
    };

    // View + selection travel with the params.
    struct ViewState
    {
        double viewStart = 0.0;
        double viewLen  = 1.0;
        double loopStart = 0.0;
        double loopEnd   = 1.0;
    };

    // Current preset tracking (Init/Save/Save-As source of truth).
    struct Session
    {
        juce::String name = "Untitled";
        juce::File file;
    };

    inline juce::File presetsFolder()
    {
        auto dir = StretchSettings::getPresetDirectory();
        if (! dir.isDirectory())
            dir.createDirectory(); // failure surfaces in save()
        return dir;
    }

    // Recursive; sorted by relative path so menu IDs are stable.
    inline juce::Array<juce::File> listPresets()
    {
        const auto root = presetsFolder();
        auto files = root.findChildFiles (juce::File::findFiles, true, "*.xml");
        std::sort (files.begin(), files.end(), [&root] (const juce::File& a, const juce::File& b)
        {
            return a.getRelativePathFrom (root) < b.getRelativePathFrom (root);
        });
        return files;
    }

    // Overwrite-capable save; save() delegates with the folder path.
    // Empty sourcePath = params-only preset (no sample to recall).
    inline bool saveToFile (const juce::File& file,
                            const juce::AudioProcessorValueTreeState& apvts,
                            const ViewState& vs = {},
                            const juce::String& sourcePath = {})
    {
        juce::ValueTree tree ("StretchPreset");
        tree.setProperty ("version", 1, nullptr);

        for (auto* id : kParamIds)
            if (auto* p = apvts.getParameter (id))
                tree.setProperty (juce::Identifier (id), p->getValue(), nullptr);

        tree.setProperty ("WaveViewStart", vs.viewStart, nullptr);
        tree.setProperty ("WaveViewLen",  vs.viewLen, nullptr);
        tree.setProperty ("LoopStart",    vs.loopStart, nullptr);
        tree.setProperty ("LoopEnd",      vs.loopEnd, nullptr);
        tree.setProperty ("SourcePath",   sourcePath, nullptr);

        const auto xml = tree.createXml();
        if (xml == nullptr)
            return false;

        return xml->writeTo (file);
    }

    // Save-As that also reports the written name/file for session tracking.
    inline bool saveAs (const juce::AudioProcessorValueTreeState& apvts,
                        const juce::String& rawName,
                        const ViewState& vs,
                        juce::String& outName,
                        juce::File& outFile,
                        const juce::String& sourcePath = {})
    {
        const juce::String safe = rawName.trim()
            .retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_()");

        if (safe.isEmpty())
            return false;

        const auto file = presetsFolder().getChildFile (safe + ".xml");
        if (! saveToFile (file, apvts, vs, sourcePath))
            return false;

        outName = safe;
        outFile = file;
        return true;
    }

    // Bad names rejected; hostile chars stripped.
    inline bool save (const juce::AudioProcessorValueTreeState& apvts,
                      const juce::String& rawName,
                      const ViewState& vs = {},
                      const juce::String& sourcePath = {})
    {
        juce::String sessionName;
        juce::File sessionFile;
        return saveAs (apvts, rawName, vs, sessionName, sessionFile, sourcePath);
    }

    // Every param back to its default (Init).
    inline void initDefaults (juce::AudioProcessorValueTreeState& apvts)
    {
        for (auto* id : kParamIds)
            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost (p->getDefaultValue());
    }

    // Unknown ids skipped (old presets survive); bad XML is a no-op.
    inline void load (const juce::File& file, juce::AudioProcessorValueTreeState& apvts,
                      ViewState& vs, juce::String& sourcePath)
    {
        sourcePath.clear();

        auto xml = juce::parseXML (file);
        if (xml == nullptr || ! xml->hasTagName ("StretchPreset"))
            return;

        const auto tree = juce::ValueTree::fromXml (*xml);

        for (auto* id : kParamIds)
        {
            if (! tree.hasProperty (id))
                continue;

            if (auto* p = apvts.getParameter (id))
                p->setValueNotifyingHost ((float) (double) tree.getProperty (id, 0.0));
        }

        vs.viewStart  = (double) tree.getProperty ("WaveViewStart", 0.0);
        vs.viewLen    = (double) tree.getProperty ("WaveViewLen", 1.0);
        vs.loopStart  = (double) tree.getProperty ("LoopStart", 0.0);
        vs.loopEnd    = (double) tree.getProperty ("LoopEnd", 1.0);
        sourcePath    = tree.getProperty ("SourcePath").toString();
    }

    // Load ignoring the stored sample.
    inline void load (const juce::File& file, juce::AudioProcessorValueTreeState& apvts,
                      ViewState& vs)
    {
        juce::String ignored;
        load (file, apvts, vs, ignored);
    }
}
