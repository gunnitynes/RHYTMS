cmake_minimum_required(VERSION 3.22)
project(RHYTMS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Link the MSVC runtime statically so the plugin loads on machines without
# the Visual C++ redistributable installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Point at a local JUCE checkout with -DRHYTMS_JUCE_DIR=/path/to/JUCE,
# otherwise JUCE 8.0.4 is fetched automatically.
set(RHYTMS_JUCE_DIR "" CACHE PATH "Optional local JUCE checkout")

if (RHYTMS_JUCE_DIR)
    add_subdirectory(${RHYTMS_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    set(JUCE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(JUCE_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE
    )
    set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(juce)
endif()

set(RHYTMS_FORMATS VST3 Standalone)
if (APPLE)
    list(APPEND RHYTMS_FORMATS AU)
endif()

juce_add_plugin(RHYTMS
    COMPANY_NAME "gunnitynes"
    PRODUCT_NAME "RHYTMS"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Gnty
    PLUGIN_CODE Rhtm
    FORMATS ${RHYTMS_FORMATS}
    VST3_CATEGORIES Fx Delay
    AU_MAIN_TYPE kAudioUnitType_Effect
)

target_sources(RHYTMS PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/state/MorphEngine.cpp
    Source/state/PresetManager.cpp
)

target_include_directories(RHYTMS PRIVATE Source)

target_compile_definitions(RHYTMS PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
)

target_link_libraries(RHYTMS
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
