cmake_minimum_required(VERSION 3.22)

project(TheSecretSauce VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Apple Silicon + Intel universal binaries by default when building on macOS.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

# ------------------------------------------------------------------ JUCE ----
# Use a local JUCE checkout if provided (SECRET_SAUCE_JUCE_PATH), otherwise
# fetch the pinned release. Pinning keeps builds reproducible for years.
set(SECRET_SAUCE_JUCE_TAG "8.0.14")
set(SECRET_SAUCE_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(SECRET_SAUCE_JUCE_PATH)
    add_subdirectory(${SECRET_SAUCE_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${SECRET_SAUCE_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ----------------------------------------------------------------- Plugin ---
set(SECRET_SAUCE_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND SECRET_SAUCE_FORMATS AU)
endif()

# AAX builds require the Avid AAX SDK; point SECRET_SAUCE_AAX_SDK_PATH at it.
set(SECRET_SAUCE_AAX_SDK_PATH "" CACHE PATH "Path to the Avid AAX SDK (optional)")
if(SECRET_SAUCE_AAX_SDK_PATH)
    juce_set_aax_sdk_path(${SECRET_SAUCE_AAX_SDK_PATH})
    list(APPEND SECRET_SAUCE_FORMATS AAX)
endif()

juce_add_plugin(SecretSauce
    PRODUCT_NAME                "The Secret Sauce"
    COMPANY_NAME                "Secret Sauce Audio"
    COMPANY_WEBSITE             "https://github.com/jtekkk/the-secret-sauce-"
    BUNDLE_ID                   com.secretsauceaudio.thesecretsauce
    PLUGIN_MANUFACTURER_CODE    Scsa
    PLUGIN_CODE                 Ssce
    FORMATS                     ${SECRET_SAUCE_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Dynamics EQ Mastering
    AU_MAIN_TYPE                kAudioUnitType_Effect
    AAX_CATEGORY                AAX_ePlugInCategory_Harmonic)

set(SECRET_SAUCE_SOURCES
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_sources(SecretSauce PRIVATE ${SECRET_SAUCE_SOURCES})
target_include_directories(SecretSauce PRIVATE src)

set(SECRET_SAUCE_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_compile_definitions(SecretSauce PUBLIC ${SECRET_SAUCE_DEFS})

target_link_libraries(SecretSauce
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------ Tests ---
# Offline test harness: exercises the real AudioProcessor across sample rates
# and buffer sizes, checks null behaviour, latency reporting, state round
# trips, snapshots, and NaN/denormal safety.
option(SECRET_SAUCE_BUILD_TESTS "Build the offline test harness" ON)

if(SECRET_SAUCE_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(SecretSauceTests PRODUCT_NAME "SecretSauceTests")

    target_sources(SecretSauceTests PRIVATE
        tests/TestMain.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp)

    target_include_directories(SecretSauceTests PRIVATE src)

    target_compile_definitions(SecretSauceTests PRIVATE
        ${SECRET_SAUCE_DEFS}
        SECRET_SAUCE_TESTS=1
        JUCE_STANDALONE_APPLICATION=1)

    target_link_libraries(SecretSauceTests PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

    add_test(NAME SecretSauceTests COMMAND SecretSauceTests)
endif()
