cmake_minimum_required(VERSION 3.24)

project(StretchPlugin VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use a local JUCE checkout if STRETCH_JUCE_PATH is set, otherwise fetch JUCE 9.0.1.
set(STRETCH_JUCE_PATH "" CACHE PATH "Optional local JUCE checkout")

include(FetchContent)
if (EXISTS "${STRETCH_JUCE_PATH}")
    message(STATUS "Using local JUCE checkout '${STRETCH_JUCE_PATH}'")
    add_subdirectory("${STRETCH_JUCE_PATH}" ${CMAKE_BINARY_DIR}/juce_build EXCLUDE_FROM_ALL)
else()
    message(STATUS "STRETCH_JUCE_PATH '${STRETCH_JUCE_PATH}' not found -- fetching JUCE 9.0.1")
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Signalsmith Stretch (header-only)
add_library(signalsmith-stretch INTERFACE)
set_target_properties(signalsmith-stretch PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Fetch signalsmith-linear (FFT dependency)
if(NOT TARGET signalsmith-linear)
    include(FetchContent)
    FetchContent_Declare(
        signalsmith-linear
        GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git
        GIT_TAG 0.3.1
        GIT_SHALLOW ON
    )
    FetchContent_MakeAvailable(signalsmith-linear)
endif()

target_link_libraries(signalsmith-stretch INTERFACE signalsmith-linear)

# CLAP via clap-juce-extensions (JUCE 9 has no native CLAP in CMake).
include(FetchContent)
FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    # Pin a commit: JUCE 9 support and the embedded-editor scale fix are unreleased.
    GIT_TAG c1a5ad025f
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "clap-libs/clap;clap-libs/clap-helpers"
)
FetchContent_GetProperties(clap-juce-extensions)
if(NOT clap-juce-extensions_POPULATED)
    FetchContent_Populate(clap-juce-extensions)
    add_subdirectory(${clap-juce-extensions_SOURCE_DIR}
                     ${clap-juce-extensions_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

# Copy built plugins into the system plugin folders after every successful build.
option(STRETCH_COPY_AFTER_BUILD "Copy built plugins into the system plugin folders" ON)

if (STRETCH_COPY_AFTER_BUILD)
    set_property(GLOBAL PROPERTY JUCE_COPY_PLUGIN_AFTER_BUILD TRUE)
endif()

juce_add_plugin(StretchPlugin
    PRODUCT_NAME "Stretch"
    COMPANY_NAME "BalamDSP"
    PLUGIN_MANUFACTURER_CODE Bala
    PLUGIN_CODE Str1
    FORMATS VST3 AU Standalone
    VERSION ${PROJECT_VERSION}
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    IS_SYNTH FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES "Fx" "Pitch Shift"
)

# Source files (StretchEngine is header-only)
target_sources(StretchPlugin PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
)

# Binary data (font)
juce_add_binary_data(StretchPluginData
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES
        source/Fonts/VT323-Regular.ttf
)

target_link_libraries(StretchPlugin
    PRIVATE
        StretchPluginData
        signalsmith-stretch
        juce::juce_audio_basics
        juce::juce_audio_processors
        juce::juce_audio_plugin_client
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

target_compile_definitions(StretchPlugin
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        # Off keeps the UI at 1:1 physical scaling (CRT_UI_RESIZE.md §8).
        JUCE_WIN_PER_MONITOR_DPI_AWARE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
)

# NOTE: do NOT try to force JUCE_STANDALONE_APPLICATION=0 here. Plugin
# sources are compiled ONCE into Stretch_SharedCode.lib (only the wrapper
# TUs are per-format), so any compile-time value applies to every format.
# Standalone-vs-host gating must be done at RUNTIME (see
# StretchAudioProcessor::isRunningAsStandalone).

# Source files use UTF-8 glyphs (e.g. the "\u2630" menu button) without a BOM.
if(MSVC)
    target_compile_options(StretchPlugin PRIVATE /utf-8)
endif()

# Custom standalone entry (native OS title bar).
# JUCE's module TU then only provides the main() and expects us to define
# juce_CreateApplication() via StretchStandaloneApp in CustomStandalone.cpp.
target_compile_definitions(StretchPlugin_Standalone PRIVATE
    JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP=1
)
target_sources(StretchPlugin_Standalone PRIVATE
    source/Standalone/CustomStandalone.cpp
)
if(MSVC)
    target_compile_options(StretchPlugin_Standalone PRIVATE /utf-8)
endif()

# Wrap the JUCE target into a CLAP plugin (produces StretchPlugin_CLAP ->
# Stretch.clap next to the other artefacts, under .../CLAP).
clap_juce_extensions_plugin(TARGET StretchPlugin
    CLAP_ID "com.balamdsp.Stretch"
    CLAP_FEATURES "audio-effect"
)
