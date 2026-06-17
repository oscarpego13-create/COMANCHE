cmake_minimum_required(VERSION 3.22)
project(COMANCHE VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# macOS deployment target
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()

# JUCE as subdirectory (git submodule at libs/JUCE)
add_subdirectory(libs/JUCE)

# Embed font as binary data
juce_add_binary_data(ComanacheResources
    SOURCES
        Resources/Roboto-Mono-Regular.ttf
)

# Define the plugin
juce_add_plugin(COMANCHE
    COMPANY_NAME                "AudioDev"
    PLUGIN_MANUFACTURER_CODE    "AuDv"
    PLUGIN_CODE                 "Cmch"
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "COMANCHE"
    BUNDLE_ID                   "com.AudioDev.vst3.COMANCHE"
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES             "Instrument|Sampler"
)

target_sources(COMANCHE PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SamplerVoice.cpp
    Source/SamplerInstrument.cpp
    Source/SampleLibrary.cpp
    Source/EffectsChain.cpp
    Source/MacroController.cpp
    Source/PresetManager.cpp
    Source/CustomLookAndFeel.cpp
    Source/Components/SampleListPanel.cpp
    Source/Components/EffectsPanel.cpp
    Source/Components/CustomKnob.cpp
)

target_compile_definitions(COMANCHE PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(COMANCHE PRIVATE
    ComanacheResources
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
