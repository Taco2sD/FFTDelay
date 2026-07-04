cmake_minimum_required(VERSION 3.22)

# C is required too: on macOS JUCE compiles C / Objective-C sources internally.
project(FFTDelay VERSION 0.2.0 LANGUAGES C CXX)

# --- Pull JUCE in automatically (no git submodule needed) ---------------------
# FetchContent clones JUCE at configure time. The first configure is slow
# (it downloads JUCE); after that it's cached under the build directory.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# --- Declare the plugin target ------------------------------------------------
juce_add_plugin(FFTDelay
    COMPANY_NAME              "RadicallyDecent"
    PLUGIN_MANUFACTURER_CODE  Rdcl          # 4 chars, >=1 uppercase
    PLUGIN_CODE               Fdly          # 4 chars, unique per plugin
    FORMATS                   VST3 AU Standalone
    PRODUCT_NAME              "FFTDelay"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          TRUE          # MIDI Learn: receive CCs from the host
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    COPY_PLUGIN_AFTER_BUILD   TRUE)         # installs to your user plugin folder

target_sources(FFTDelay PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/SpectralDelay.cpp)

# Generates a JuceHeader-free build; we include modules directly in source.
target_compile_definitions(FFTDelay PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FFTDelay
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# --- Unit tests ---------------------------------------------------------------
# A small console app driven by JUCE's UnitTest framework, registered with CTest.
# Build everything with `cmake --build build`, then run `ctest --test-dir build`.
option(FFTDELAY_BUILD_TESTS "Build the FFTDelay unit tests" ON)

if(FFTDELAY_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(FFTDelayTests PRODUCT_NAME "FFTDelayTests")

    target_sources(FFTDelayTests PRIVATE
        tests/SpectralTests.cpp
        source/SpectralDelay.cpp)

    target_compile_definitions(FFTDelayTests PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)

    target_link_libraries(FFTDelayTests
        PRIVATE
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)

    add_test(NAME unit COMMAND FFTDelayTests)
endif()
