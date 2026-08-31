// Custom standalone entry point for StretchPlugin.
//
// Derived from juce_audio_plugin_client_Standalone.cpp (JUCE 9) so the
// standalone window can be created with a native OS title bar from the start
// (see ERRATA.md -- editor-driven title-bar switching glitches and can freeze
// the window at its 128px minimum). Compiled into StretchPlugin_Standalone
// with JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#include "CustomStandaloneFilterWindow.h"
#include "../Helpers/StretchDefines.h"

namespace juce
{

class StretchStandaloneApp final : public JUCEApplication
{
public:
    StretchStandaloneApp()
    {
        PropertiesFile::Options options;

        options.applicationName     = CharPointer_UTF8 (JucePlugin_Name);
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
       #if JUCE_LINUX || JUCE_BSD
        options.folderName          = "~/.config";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);
    }

    const String getApplicationName() override              { return CharPointer_UTF8 (JucePlugin_Name); }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }
    void anotherInstanceStarted (const String&) override    {}

    StretchFilterWindow* createWindow()
    {
        if (Desktop::getInstance().getDisplays().displays.isEmpty())
        {
            // No displays are available, so no window will be created!
            jassertfalse;
            return nullptr;
        }

        // Our own background colour from the very first frame (avoids the
        // one-frame grey flash described in ERRATA.md).
        return new StretchFilterWindow (getApplicationName(),
                                        GUI::Color::Background,
                                        createPluginHolder());
    }

    std::unique_ptr<StretchPluginHolder> createPluginHolder()
    {
        constexpr auto autoOpenMidiDevices = false;

       #ifdef JucePlugin_PreferredChannelConfigurations
        constexpr StandalonePluginHolder::PluginInOuts channels[] { JucePlugin_PreferredChannelConfigurations };
        const Array<StandalonePluginHolder::PluginInOuts> channelConfig (channels, juce::numElementsInArray (channels));
       #else
        const Array<StretchPluginHolder::PluginInOuts> channelConfig;
       #endif

        return std::make_unique<StretchPluginHolder> (appProperties.getUserSettings(),
                                                      false,
                                                      String{},
                                                      nullptr,
                                                      channelConfig,
                                                      autoOpenMidiDevices);
    }

    void initialise (const String&) override
    {
        mainWindow = rawToUniquePtr (createWindow());

        if (mainWindow != nullptr)
        {
           #if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
           #endif

            mainWindow->setVisible (true);
        }
        else
        {
            pluginHolder = createPluginHolder();
        }
    }

    void shutdown() override
    {
        pluginHolder = nullptr;
        mainWindow = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (pluginHolder != nullptr)
            pluginHolder->savePluginState();

        if (mainWindow != nullptr)
            mainWindow->pluginHolder->savePluginState();

        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []()
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

protected:
    ApplicationProperties appProperties;
    std::unique_ptr<StretchFilterWindow> mainWindow;

private:
    std::unique_ptr<StretchPluginHolder> pluginHolder;
};

} // namespace juce

JUCE_CREATE_APPLICATION_DEFINE (juce::StretchStandaloneApp)
