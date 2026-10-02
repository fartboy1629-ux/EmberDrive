cmake_minimum_required(VERSION 3.22)

# macOS: build a universal binary that runs on Intel and Apple Silicon
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")

project(EmberDrive VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows: build the C++ runtime into the plug-in, so it works on any PC without
# the Visual C++ Redistributable having to be installed first.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ---- JUCE ---------------------------------------------------------------
# Downloaded automatically the first time you configure, as a plain archive, so
# Git doesn't need to be installed. To use a copy of JUCE you already have:
#   cmake -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)   # extracted files get the current time stamp
endif()

set(EMBER_JUCE_URL "https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.15.tar.gz"
    CACHE STRING "Where to download the JUCE 8.0.15 source archive from")

include(FetchContent)
FetchContent_Declare(JUCE URL "${EMBER_JUCE_URL}")
FetchContent_MakeAvailable(JUCE)

# ---- Plugin -------------------------------------------------------------
option(EMBER_COPY_AFTER_BUILD
       "Copy the plug-in into the system plug-in folder after building (may need admin rights)" OFF)

set(EMBER_FORMATS VST3)
if(APPLE)
    list(APPEND EMBER_FORMATS AU)
endif()

juce_add_plugin(EmberDrive
    COMPANY_NAME                "Ember Audio"
    BUNDLE_ID                   com.emberaudio.emberdrive
    PLUGIN_MANUFACTURER_CODE    Embr
    PLUGIN_CODE                 Emdr
    FORMATS                     ${EMBER_FORMATS}
    PRODUCT_NAME                "Ember Drive"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     ${EMBER_COPY_AFTER_BUILD}
    VST3_CATEGORIES             Fx Distortion
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(EmberDrive PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Controls.cpp
    Source/Displays.cpp)

target_compile_definitions(EmberDrive PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(EmberDrive
    PRIVATE
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
