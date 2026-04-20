cmake_minimum_required(VERSION 3.24)
project(Performance VERSION 0.0.3)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0")

# Git build info — captured at BUILD time via generate_build_version
# target (see cmake/GenerateBuildVersion.cmake). Produces
# ${CMAKE_BINARY_DIR}/generated/BuildVersion.h with #defines for
# BUILD_GIT_COMMIT (includes -dirty suffix), BUILD_GIT_TAG,
# BUILD_GIT_DIRTY, BUILD_VERSION. Consumers #include "BuildVersion.h".
set(BUILD_VERSION_HEADER ${CMAKE_BINARY_DIR}/generated/BuildVersion.h)

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
        runtime/CLAUDE.md
        runtime/bundled-plugins/manifest.json
        runtime/bundled-plugins/LICENSES/NOTICE-mda.txt
        runtime/bundled-plugins/LICENSES/NOTICE-Airwindows.txt
        runtime/bundled-plugins/LICENSES/NOTICE-Surge-XT.txt
        runtime/bundled-plugins/LICENSES/NOTICE-Dexed.txt
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
    src/state/ParamSchemaJson.cpp
    src/state/ActionRefs.cpp
    src/state/ActionAlgebra.cpp
    src/state/ActionInterpreter.cpp
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
    src/gui/ActionInstanceForm.cpp
    src/gui/ActionPicker.cpp
    src/gui/KeyBindingManager.cpp
    src/gui/KeyBindingEditor.cpp
    src/daw/InternalSequencer.cpp
    src/daw/Arrangement.cpp
    src/gui/ControllersPane.cpp
    src/gui/SongMappingsPane.cpp
    src/gui/PerformPane.cpp
    src/telemetry/InstallId.cpp
    src/telemetry/TelemetryShipper.cpp
    src/install/BundledPluginInstaller.cpp
    src/gui/PluginInstallDialog.cpp
    src/gui/LogPane.cpp
    src/gui/ChatView.cpp
    src/gui/PerformanceLookAndFeel.cpp
    src/api/ClaudeClient.cpp
    src/scripting/LuaEngine.cpp
    src/ipc/IPCServer.cpp
    src/automation/AutomationEngine.cpp
    src/engine/EngineSync.cpp
    src/engine/AudioEngine.mm
    src/rendering/OfflineRenderer.cpp
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

# Regenerate BuildVersion.h on every build — idempotent write means
# consumers only recompile when git state actually changed.
add_custom_target(generate_build_version ALL
    BYPRODUCTS ${BUILD_VERSION_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT=${BUILD_VERSION_HEADER}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildVersion.cmake
    COMMENT "Generating BuildVersion.h"
    VERBATIM
)
add_dependencies(Performance generate_build_version)
target_include_directories(Performance PRIVATE ${CMAKE_BINARY_DIR}/generated)

# Telemetry endpoint + token baked from keys/telemetry.json (absent on
# fresh checkouts — run scripts/fetch-telemetry-config.sh to populate).
set(BUILD_CONFIG_HEADER ${CMAKE_BINARY_DIR}/generated/BuildConfig.h)
add_custom_target(generate_build_config ALL
    BYPRODUCTS ${BUILD_CONFIG_HEADER}
    COMMAND ${CMAKE_COMMAND}
        -DOUTPUT=${BUILD_CONFIG_HEADER}
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildConfig.cmake
    COMMENT "Generating BuildConfig.h"
    VERBATIM
)
add_dependencies(Performance generate_build_config)

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

# Sign with Developer ID so TCC (macOS privacy) identifies the app the
# same way across rebuilds. Ad-hoc linker-signing changes the CDHash on
# every build, which invalidates stored TCC grants and causes re-prompts
# for Documents / Desktop / Microphone on every launch. With a stable
# Developer ID signature, TCC recognizes dev and release builds as the
# same app. Release re-signs with --options runtime for notarization.
if(APPLE)
    add_custom_command(TARGET Performance POST_BUILD
        COMMAND codesign --force --sign
            "Developer ID Application: William Haslett (H25TK2U8FA)"
            "$<TARGET_BUNDLE_DIR:Performance>"
        VERBATIM
    )
endif()

# --- Tests ---
juce_add_console_app(PerformanceTests PRODUCT_NAME "PerformanceTests")

target_sources(PerformanceTests PRIVATE
    tests/PerformanceTests.cpp
    src/api/StateAPI.cpp
    src/api/EngineAPI.cpp
    src/api/PerformanceCoordinator.cpp
    src/state/ParamSchemaJson.cpp
    src/state/ActionRefs.cpp
    src/state/ActionAlgebra.cpp
    src/state/ActionInterpreter.cpp
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
    src/rendering/OfflineRenderer.cpp
    src/install/BundledPluginInstaller.cpp
    src/telemetry/InstallId.cpp
)

target_include_directories(PerformanceTests PRIVATE src /opt/homebrew/include ${CMAKE_BINARY_DIR}/generated)

target_compile_definitions(PerformanceTests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_PLUGINHOST_AU=1
    JUCE_PLUGINHOST_VST3=0
)

add_dependencies(PerformanceTests generate_build_config generate_build_version)

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
