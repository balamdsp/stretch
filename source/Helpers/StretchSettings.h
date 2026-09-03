#pragma once

#include <juce_core/juce_core.h>

// Machine-wide UI settings (settings.xml). Message thread writes;
// readers re-parse so other instances' changes show on next read.
namespace StretchSettings
{
    inline juce::File settingsFile()
    {
        return juce::File::getSpecialLocation (
                   juce::File::userApplicationDataDirectory)
                   .getChildFile ("BalamDSP")
                   .getChildFile ("Stretch")
                   .getChildFile ("settings.xml");
    }

    inline bool getCrtEnabled()
    {
        if (auto xml = juce::parseXML (settingsFile()))
            return xml->getBoolAttribute ("crtEnabled", true);
        return true;
    }

    // 0/1/2 = Low/Med/High.
    inline int getCrtStrength()
    {
        if (auto xml = juce::parseXML (settingsFile()))
            return juce::jlimit (0, 2, xml->getIntAttribute ("crtStrength", 2));
        return 2;
    }

    inline void setCrtEnabled (bool enabled)
    {
        auto xml = juce::parseXML (settingsFile());
        if (xml == nullptr)
            xml = std::make_unique<juce::XmlElement> ("StretchSettings");
        xml->setAttribute ("crtEnabled", enabled);
        settingsFile().getParentDirectory().createDirectory();
        xml->writeTo (settingsFile());
    }

    inline void setCrtStrength (int strength)
    {
        strength = juce::jlimit (0, 2, strength);
        auto xml = juce::parseXML (settingsFile());
        if (xml == nullptr)
            xml = std::make_unique<juce::XmlElement> ("StretchSettings");
        xml->setAttribute ("crtStrength", strength);
        settingsFile().getParentDirectory().createDirectory();
        xml->writeTo (settingsFile());
    }

    // Preset folder (default %APPDATA%\BalamDSP\Stretch\Presets).
    inline juce::File defaultPresetDirectory()
    {
        auto dir = juce::File::getSpecialLocation (
                       juce::File::userApplicationDataDirectory)
                       .getChildFile ("BalamDSP")
                       .getChildFile ("Stretch")
                       .getChildFile ("Presets");
        if (! dir.isDirectory())
            dir.createDirectory();
        return dir;
    }

    inline juce::File getPresetDirectory()
    {
        juce::File dir;
        if (auto xml = juce::parseXML (settingsFile()))
            dir = juce::File (xml->getStringAttribute ("presetFolder"));

        if (! dir.isDirectory())
            dir = defaultPresetDirectory();
        return dir;
    }

    inline void setPresetDirectory (const juce::File& newDir)
    {
        if (newDir == getPresetDirectory())
            return;

        auto xml = juce::parseXML (settingsFile());
        if (xml == nullptr)
            xml = std::make_unique<juce::XmlElement> ("StretchSettings");
        xml->setAttribute ("presetFolder", newDir.getFullPathName());
        settingsFile().getParentDirectory().createDirectory();
        xml->writeTo (settingsFile());
    }

    inline void resetPresetDirectory()
    {
        if (auto xml = juce::parseXML (settingsFile()))
        {
            if (xml->hasAttribute ("presetFolder"))
            {
                xml->removeAttribute ("presetFolder");
                settingsFile().getParentDirectory().createDirectory();
                xml->writeTo (settingsFile());
            }
        }
    }
}