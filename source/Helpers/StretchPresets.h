#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

// ---------------------------------------------------------------------------
// StretchPresets - user preset storage for the automatable parameters.
//
// XML files live in %APPDATA%\BalamDSP\Stretch\Presets — global, shared by
// the VST3 and Standalone binaries like every other global setting.
//
// Values are stored NORMALIZED (0..1) so presets survive range/curve tweaks.
// FREEZE / REWIND are automatable parameters but stay excluded: recalling a
// preset must not freeze playback or flip direction.
// ---------------------------------------------------------------------------
namespace StretchPresets
{
    static constexpr const char* kParamIds[] = {
        "PitchSemitones", "TimeRatio", "FormantPreserve", "FormantSemitones", "OutputGain"
    };

    inline juce::File presetsFolder()
    {
        auto dir = juce::File::getSpecialLocation (
                       juce::File::userApplicationDataDirectory)
                       .getChildFile ("BalamDSP")
                       .getChildFile ("Stretch")
                       .getChildFile ("Presets");

        if (! dir.isDirectory())
            dir.createDirectory(); // failure surfaces in save()

        return dir;
    }

    inline juce::Array<juce::File> listPresets()
    {
        return presetsFolder().findChildFiles (juce::File::findFiles, false, "*.xml");
    }

    // Filesystem-hostile characters are stripped; empty result is rejected.
    inline bool save (const juce::AudioProcessorValueTreeState& apvts,
                      const juce::String& rawName)
    {
        const juce::String safe = rawName.trim()
            .retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_()");

        if (safe.isEmpty())
            return false;

        juce::ValueTree tree ("StretchPreset");
        tree.setProperty ("version", 1, nullptr);

        for (auto* id : kParamIds)
            if (auto* p = apvts.getParameter (id))
                tree.setProperty (juce::Identifier (id), p->getValue(), nullptr);

        const auto xml = tree.createXml();
        if (xml == nullptr)
            return false;

        return xml->writeTo (presetsFolder().getChildFile (safe + ".xml"));
    }

    // Unknown/stale ids are skipped so older presets survive changes to the
    // parameter set. Missing file or malformed XML is a silent no-op.
    inline void load (const juce::File& file, juce::AudioProcessorValueTreeState& apvts)
    {
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
    }
}
