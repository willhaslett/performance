cmake_minimum_required(VERSION 3.24)
project(Performance VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(lib/JUCE)

juce_add_gui_app(Performance
    PRODUCT_NAME "Performance"
    BUNDLE_ID "com.performance.app"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
)

target_sources(Performance PRIVATE
    src/main.cpp
    src/engine/AudioEngine.mm
    src/engine/MIDIEngine.cpp
    src/song/SongRuntime.cpp
)

target_include_directories(Performance PRIVATE src)

target_compile_definitions(Performance PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=0
)

target_link_libraries(Performance PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_gui_extra
)
