cmake_minimum_required(VERSION 3.22)

project(KeyBpmDetector VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Статический рантайм на Windows — не нужен отдельный Visual C++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# JUCE: локальная папка (-DJUCE_DIR=...) или скачивание с GitHub
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(KBD_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND KBD_FORMATS AU)
endif()

juce_add_plugin(KeyBpmDetector
    COMPANY_NAME "103 MUSIC"
    COMPANY_WEBSITE ""
    BUNDLE_ID com.onezerothreemusic.keybpmdetector
    PLUGIN_MANUFACTURER_CODE Otmc
    PLUGIN_CODE Kbpm
    FORMATS ${KBD_FORMATS}
    PRODUCT_NAME "KeyBPM Detector"
    DESCRIPTION "Key and BPM detector"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(KeyBpmDetector PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/core/KeyBpmCore.cpp)

target_compile_definitions(KeyBpmDetector PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_USE_WINDOWS_MEDIA_FORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(MSVC)
    target_compile_options(KeyBpmDetector PRIVATE /utf-8)
endif()

target_link_libraries(KeyBpmDetector
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Консольный тест ядра (не входит в плагин)
option(KBD_BUILD_TESTS "Build analysis CLI" OFF)
if(KBD_BUILD_TESTS)
    add_executable(kbd_cli tests/cli.cpp Source/core/KeyBpmCore.cpp)
    # Снимок окна плагина + проверка анализа через сам процессор
    add_executable(kbd_snapshot tests/snapshot.cpp)
    target_include_directories(kbd_snapshot PRIVATE $<TARGET_PROPERTY:KeyBpmDetector,INCLUDE_DIRECTORIES>)
    target_compile_definitions(kbd_snapshot PRIVATE $<TARGET_PROPERTY:KeyBpmDetector,COMPILE_DEFINITIONS>)
    target_link_libraries(kbd_snapshot PRIVATE KeyBpmDetector)
endif()
