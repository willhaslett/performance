#include <juce_gui_basics/juce_gui_basics.h>
#include "api/PerformanceCoordinator.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "gui/MainLayout.h"
#include "gui/KeyBindings.h"
#include "scripting/LuaEngine.h"
#include "ipc/IPCServer.h"
#include "engine/Log.h"
#import <AppKit/AppKit.h>

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(StateAPI& state, EngineAPI& engine, LuaEngine& lua,
               PerformanceCoordinator& coordinator)
        : DocumentWindow("Performance",
                         juce::Colour(0xff121212),
                         DocumentWindow::allButtons),
          mainLayout(new MainLayout(state, engine, lua, coordinator)) {
        setUsingNativeTitleBar(true);
        setResizable(true, true);
        setContentOwned(mainLayout, false);

        auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (display) {
            auto area = display->userArea;
            setSize(area.getWidth(), area.getHeight());
            setTopLeftPosition(area.getX(), area.getY());
        } else {
            centreWithSize(1200, 800);
        }

        setVisible(true);
        toFront(true);

        keyMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown
            handler:^NSEvent* (NSEvent* event) {
                juce::juce_wchar c = event.characters.length > 0
                    ? [event.characters characterAtIndex:0] : 0;
                auto key = juce::KeyPress(c, juce::ModifierKeys::getCurrentModifiers(), c);

                if (event.keyCode == 53)
                    key = juce::KeyPress(juce::KeyPress::escapeKey);

                if (mainLayout->handleGlobalKey(key))
                    return nil;
                return event;
            }];
    }

    ~MainWindow() override {
        if (keyMonitor)
            [NSEvent removeMonitor:keyMonitor];
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainLayout* getMainLayout() { return mainLayout; }

private:
    MainLayout* mainLayout;
    id keyMonitor = nil;
};

// --- Menu Bar ---

enum CommandIDs {
    newSong = 1,
    saveSong = 3,
    closeSong = 4,
    newInstrumentTrack = 5,
    newEffectsBus = 6,
    toggleSidebar = 7,
    toggleMixer = 8,
};

class AppMenuBar : public juce::MenuBarModel {
public:
    AppMenuBar(PerformanceCoordinator& coord, LuaEngine& lua, MainLayout& layout)
        : coord(coord), lua(lua), layout(layout) {}

    juce::StringArray getMenuBarNames() override {
        return { "File", "Track", "View" };
    }

    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override {
        juce::PopupMenu menu;
        if (index == 0) {  // File
            menu.addItem(CommandIDs::newSong, "New Song");
            menu.addItem(CommandIDs::saveSong, "Save");
            menu.addSeparator();

            // Song list from state
            auto& songs = coord.state().allSongs();
            for (int i = 0; i < (int)songs.size(); ++i) {
                auto isCurrent = songs[i].id == coord.state().getMasterOutputId();
                menu.addItem(100 + i, juce::String(songs[i].name),
                             true, isCurrent);
            }

            if (!songs.empty())
                menu.addSeparator();
            menu.addItem(CommandIDs::closeSong, "Close Song");
        }
        else if (index == 1) {  // Track
            menu.addItem(CommandIDs::newInstrumentTrack, "New Virtual Instrument Track");
            menu.addItem(9, "New Audio Input Track");
            menu.addItem(CommandIDs::newEffectsBus, "New Effects Bus");
        }
        else if (index == 2) {  // View
            menu.addItem(CommandIDs::toggleSidebar, "Toggle Sidebar");
            menu.addItem(CommandIDs::toggleMixer, "Toggle Mixer");
        }
        return menu;
    }

    void menuItemSelected(int menuItemID, int) override {
        auto& state = coord.state();

        if (menuItemID == CommandIDs::newSong) {
            auto name = "Untitled " + juce::String(juce::Time::currentTimeMillis() % 10000);
            coord.createSong(name);
        }
        else if (menuItemID == CommandIDs::saveSong) {
            coord.save();
        }
        else if (menuItemID == CommandIDs::closeSong) {
            coord.unloadSong();
        }
        else if (menuItemID == CommandIDs::newInstrumentTrack) {
            auto tracks = state.listTracks();
            auto name = "Track " + juce::String((int)tracks.size() + 1);
            state.createTrack(name.toStdString());
        }
        else if (menuItemID == 9) {
            auto tracks = state.listTracks();
            auto name = "Audio " + juce::String((int)tracks.size() + 1);
            state.createAudioInputTrack(name.toStdString(), -1, 0);  // no input until user selects
        }
        else if (menuItemID == CommandIDs::newEffectsBus) {
            auto busses = state.listBusses();
            auto name = "Bus " + juce::String((int)busses.size() + 1);
            state.createBus(name.toStdString());
        }
        else if (menuItemID == CommandIDs::toggleSidebar) {
            layout.handleGlobalKey(KeyBindings::toggleSidebar);
        }
        else if (menuItemID == CommandIDs::toggleMixer) {
            layout.handleGlobalKey(KeyBindings::toggleMixer);
        }
        else if (menuItemID >= 100) {
            auto& songs = state.allSongs();
            int idx = menuItemID - 100;
            if (idx < (int)songs.size())
                coord.loadSong(songs[idx].id);
        }
    }

private:
    PerformanceCoordinator& coord;
    LuaEngine& lua;
    MainLayout& layout;
};

// --- App ---

class PerformanceApp : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "Performance"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override {
        setvbuf(stderr, nullptr, _IONBF, 0);
        initLog();
        perfLog("[App] Starting Performance\n");

        // New system: in-memory state + persistence
        coordinator = std::make_unique<PerformanceCoordinator>();
        coordinator->initialise();

        luaEngine = std::make_unique<LuaEngine>(
            coordinator->state(), coordinator->engine(), *coordinator);

        mainWindow = std::make_unique<MainWindow>(
            coordinator->state(), coordinator->engine(), *luaEngine, *coordinator);

        // Menu bar
        auto* layout = mainWindow->getMainLayout();
        menuBar = std::make_unique<AppMenuBar>(*coordinator, *luaEngine, *layout);
        juce::MenuBarModel::setMacMainMenu(menuBar.get());

        // Wire save
        layout->onSave = [this]() { coordinator->save(); };

        // Wire sidebar song loading
        layout->getSidebar().onLoadSong = [this](const std::string& songId) {
            coordinator->loadSong(songId);
        };

        // Wire sidebar device selection
        layout->getSidebar().onDeviceSelected = [layout](const std::string& deviceId, const std::string& portName) {
            layout->showLeftPane(&layout->deviceEditor);
            layout->getDeviceEditor().setDevice(deviceId, portName);
        };

        // Wire sidebar audio output device selection
        layout->getSidebar().onAudioOutputSelected = [this](const std::string& deviceName) {
            auto& dm = coordinator->engine().getDeviceManager();
            auto setup = dm.getAudioDeviceSetup();
            auto jName = juce::String(deviceName);
            if (setup.outputDeviceName == jName) return;
            setup.outputDeviceName = jName;
            coordinator->state().setConfig("audio_output_device", deviceName);
            dm.setAudioDeviceSetup(setup, true);
        };

        // Wire sidebar audio input device selection
        layout->getSidebar().onAudioInputSelected = [this](const std::string& deviceName) {
            auto& dm = coordinator->engine().getDeviceManager();
            auto setup = dm.getAudioDeviceSetup();
            auto jName = juce::String(deviceName);
            if (setup.inputDeviceName == jName) return;
            setup.inputDeviceName = jName;
            coordinator->state().setConfig("audio_input_device", deviceName);
            dm.setAudioDeviceSetup(setup, true);
        };

        // Wire sidebar pane selection
        layout->getSidebar().onDebugSelected = [layout]() {
            layout->showLeftPane(&layout->debugPane);
        };
        layout->getSidebar().onLogsSelected = [layout]() {
            layout->showRightPane(&layout->logPane);
        };
        layout->getSidebar().onChatSelected = [layout]() {
            layout->showRightPane(&layout->chatView);
        };

        // Wire track preset callbacks — MixerView applies these to each new TrackStrip
        layout->getMixer().onSaveTrackPreset = [this](const juce::String& trackId, const juce::String& name) {
            coordinator->saveTrackPreset(trackId, name);
        };
        layout->getMixer().onLoadTrackPreset = [this](const juce::String& trackId, const juce::String& name) {
            coordinator->loadTrackPreset(trackId, name);
        };
        layout->getMixer().onListTrackPresets = [this]() {
            return coordinator->listTrackPresets();
        };

        ipcServer = std::make_unique<IPCServer>(*luaEngine);
        ipcServer->start();

        // Restore session — deferred so the window paints first
        juce::MessageManager::callAsync([this] {
            coordinator->restoreSession();
        });
    }

    void shutdown() override {
        juce::MenuBarModel::setMacMainMenu(nullptr);
        menuBar.reset();
        ipcServer.reset();
        luaEngine.reset();
        mainWindow.reset();
        coordinator.reset();
    }

    void systemRequestedQuit() override {
        if (coordinator)
            coordinator->save();
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PerformanceCoordinator> coordinator;
    std::unique_ptr<LuaEngine> luaEngine;
    std::unique_ptr<IPCServer> ipcServer;
    std::unique_ptr<AppMenuBar> menuBar;
};

START_JUCE_APPLICATION(PerformanceApp)
