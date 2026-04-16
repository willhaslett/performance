cmake_minimum_required(VERSION 3.24)
project(Performance VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(lib/JUCE)

# --- Lua (C library) ---
include(FetchContent)

FetchContent_Declare(lua
    URL https://www.lua.org/ftp/lua-5.4.7.tar.gz
)
FetchContent_MakeAvailable(lua)

# Build Lua as a static library from fetched source
file(GLOB LUA_SOURCES "${lua_SOURCE_DIR}/src/*.c")
list(REMOVE_ITEM LUA_SOURCES "${lua_SOURCE_DIR}/src/lua.c" "${lua_SOURCE_DIR}/src/luac.c")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC "${lua_SOURCE_DIR}/src")

# --- sol2 (header-only C++ Lua bindings) ---
FetchContent_Declare(sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG develop
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sol2)

# --- Factory theme resources ---
# Bundles runtime/themes/*.json into a C++ static library that exposes the
# files via BinaryData::minimal_dark_json, BinaryData::minimal_light_json,
# etc. The .json files stay as first-class editable files in the repo —
# editing one triggers a rebuild and the new content is baked in.
juce_add_binary_data(PerformanceResources
    SOURCES
        runtime/themes/minimal_dark.json
        runtime/themes/minimal_light.json
)

# --- Application ---
juce_add_gui_app(Performance
    PRODUCT_NAME "Performance"
    BUNDLE_ID "com.performance.app"
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    APP_SANDBOX_ENABLED FALSE
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Performance needs microphone access for audio input tracks."
)

target_sources(Performance PRIVATE
    src/main.mm
    src/api/StateAPI.cpp
    src/api/EngineAPI.cpp
    src/api/PerformanceCoordinator.cpp
    src/persistence/PersistenceLayer.cpp
    src/gui/Theme.cpp
    src/gui/MainLayout.cpp
    src/gui/MixerView.cpp
    src/gui/TrackStrip.cpp
    src/gui/BusStrip.cpp
    src/gui/OutputStrip.cpp
    src/gui/PluginSlot.cpp
    src/gui/FaderMeter.cpp
    src/gui/SendsPanel.cpp
    src/gui/RegistryTree.cpp
    src/gui/Sidebar.cpp
    src/gui/DebugPane.cpp
    src/gui/SettingsWindow.cpp
    src/gui/TransportBar.cpp
    src/gui/ProducePane.cpp
    src/gui/MusicalTyping.cpp
    src/gui/MorphEditor.cpp
    src/gui/KeyBindingManager.cpp
    src/gui/KeyBindingEditor.cpp
    src/daw/InternalSequencer.cpp
    src/daw/Arrangement.cpp
    src/gui/ControllersPane.cpp
    src/gui/SongMappingsPane.cpp
    src/gui/LogPane.cpp
    src/gui/ChatView.cpp
    src/api/ClaudeClient.cpp
    src/scripting/LuaEngine.cpp
    src/ipc/IPCServer.cpp
    src/automation/AutomationEngine.cpp
    src/engine/EngineSync.cpp
    src/engine/AudioEngine.mm
    src/engine/MIDIEngine.cpp
    src/engine/Log.cpp
    src/song/SongRuntime.cpp
)

target_include_directories(Performance PRIVATE src /opt/homebrew/include)

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
    juce::juce_cryptography
    PerformanceResources
    lua_static
    sol2
    sqlite3
)

# --- Tests ---
juce_add_console_app(PerformanceTests PRODUCT_NAME "PerformanceTests")

target_sources(PerformanceTests PRIVATE
    tests/PerformanceTests.cpp
    src/api/StateAPI.cpp
    src/api/EngineAPI.cpp
    src/api/PerformanceCoordinator.cpp
    src/persistence/PersistenceLayer.cpp
    src/engine/EngineSync.cpp
    src/engine/AudioEngine.mm
    src/engine/MIDIEngine.cpp
    src/engine/Log.cpp
    src/automation/AutomationEngine.cpp
    src/scripting/LuaEngine.cpp
    src/ipc/IPCServer.cpp
    src/song/SongRuntime.cpp
    src/daw/InternalSequencer.cpp
    src/daw/Arrangement.cpp
    src/gui/Theme.cpp
)

target_include_directories(PerformanceTests PRIVATE src /opt/homebrew/include)

target_compile_definitions(PerformanceTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=0
)

target_link_libraries(PerformanceTests PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_events
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_cryptography
    PerformanceResources
    lua_static
    sol2
    sqlite3
)
