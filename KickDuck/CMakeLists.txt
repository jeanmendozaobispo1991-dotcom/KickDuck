cmake_minimum_required(VERSION 3.22)
project(KickDuck VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

option(KICKDUCK_COPY "Copiar el plugin al sistema tras compilar" ON)

juce_add_plugin(KickDuck
    COMPANY_NAME "KickDuck"
    PLUGIN_MANUFACTURER_CODE Kdck
    PLUGIN_CODE Kd01
    FORMATS VST3 Standalone
    PRODUCT_NAME "KickDuck"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${KICKDUCK_COPY}
    VST3_CATEGORIES Fx Dynamics)

target_sources(KickDuck PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(KickDuck PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_options(KickDuck PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_link_libraries(KickDuck
    PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
