cmake_minimum_required(VERSION 3.22)
project(SanekChordFinder VERSION 0.3.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SANEKCHORD_TESTS_ONLY "Build pure C++ chord tests without JUCE" OFF)
option(SANEKCHORD_BUILD_ANALYZER_TESTS "Build the JUCE time-domain analyzer test" OFF)
if(SANEKCHORD_TESTS_ONLY)
    enable_testing()
    add_executable(ChordMatcherTests Tests/ChordMatcherTests.cpp)
    target_include_directories(ChordMatcherTests PRIVATE Source)
    add_test(NAME ChordMatcher COMMAND ChordMatcherTests)
    add_executable(SpectrumChromaTests Tests/SpectrumChromaTests.cpp)
    target_include_directories(SpectrumChromaTests PRIVATE Source)
    add_test(NAME SpectrumChroma COMMAND SpectrumChromaTests)
    add_executable(TempoTrackerTests Tests/TempoTrackerTests.cpp)
    target_include_directories(TempoTrackerTests PRIVATE Source)
    add_test(NAME TempoTracker COMMAND TempoTrackerTests)
    return()
endif()

include(FetchContent)

# Reuse JUCE already downloaded for our earlier plug-ins when the projects are
# stored next to each other under C:\Users\Alexander\Projects.
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_JUCE)
    foreach(SANEK_JUCE_CANDIDATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../SanekGain/build/_deps/juce-src"
            "${CMAKE_CURRENT_SOURCE_DIR}/../SanekDrive/build/_deps/juce-src")
        if(EXISTS "${SANEK_JUCE_CANDIDATE}/CMakeLists.txt")
            set(FETCHCONTENT_SOURCE_DIR_JUCE "${SANEK_JUCE_CANDIDATE}"
                CACHE PATH "Existing JUCE source tree" FORCE)
            message(STATUS "Reusing JUCE from ${FETCHCONTENT_SOURCE_DIR_JUCE}")
            break()
        endif()
    endforeach()
endif()

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.12
    GIT_SHALLOW TRUE
    GIT_PROGRESS TRUE
)
message(STATUS "Preparing JUCE 8.0.12. A download is needed only if no reusable copy was found.")
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(SanekChordFinder
    COMPANY_NAME "Sanek Audio"
    BUNDLE_ID "com.sanekaudio.sanekchordfinder"
    PLUGIN_MANUFACTURER_CODE Snek
    PLUGIN_CODE SCFd
    PRODUCT_NAME "Sanek Chord Finder"
    DESCRIPTION "Real-time major and minor chord detector with sequence history"
    FORMATS VST3
    VST3_CATEGORIES Fx Analyzer
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(SanekChordFinder PRIVATE
    Source/ChordMatcher.h
    Source/SpectrumChroma.h
    Source/TempoTracker.h
    Source/ChordAnalyzer.h
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)
target_compile_definitions(SanekChordFinder PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)
target_link_libraries(SanekChordFinder
    PRIVATE juce::juce_audio_processors juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags
)
if(MSVC)
    target_compile_options(SanekChordFinder PRIVATE /utf-8)
endif()

if(SANEKCHORD_BUILD_ANALYZER_TESTS)
    enable_testing()
    add_executable(ChordAnalyzerTests Tests/ChordAnalyzerTests.cpp)
    target_include_directories(ChordAnalyzerTests PRIVATE Source)
    target_link_libraries(ChordAnalyzerTests PRIVATE juce::juce_dsp)
    add_test(NAME ChordAnalyzer COMMAND ChordAnalyzerTests)
    add_executable(AnalyzeChordWav Tests/AnalyzeChordWav.cpp)
    target_include_directories(AnalyzeChordWav PRIVATE Source)
    target_link_libraries(AnalyzeChordWav PRIVATE juce::juce_audio_formats juce::juce_dsp)
endif()
