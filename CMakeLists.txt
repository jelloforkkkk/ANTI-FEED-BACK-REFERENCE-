cmake_minimum_required(VERSION 3.22)
project(FeedbackSuppressor VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Pull in JUCE ------------------------------------------------------
# Requires network access when you configure the build the first time.
# If you already have a local JUCE checkout, comment this block out and
# instead: add_subdirectory(/path/to/JUCE JUCE)
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        7.0.12
)
FetchContent_MakeAvailable(JUCE)

# --- Plugin target -------------------------------------------------------
juce_add_plugin(FeedbackSuppressor
    COMPANY_NAME "YourCompany"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Yout
    PLUGIN_CODE Fsup
    FORMATS VST3 Standalone AU
    PRODUCT_NAME "Feedback Suppressor"
)

target_sources(FeedbackSuppressor
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DSP/FeedbackSuppressorDSP.cpp
        Source/DSP/SpectralProcessor.cpp
        Source/DSP/AdaptiveFeedbackCanceller.cpp
)

target_compile_definitions(FeedbackSuppressor
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(FeedbackSuppressor
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
