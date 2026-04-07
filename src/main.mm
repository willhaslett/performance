#include <juce_gui_basics/juce_gui_basics.h>
#include "api/PerformanceAPI.h"
#include "api/PerformanceCoordinator.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "gui/MainLayout.h"
#include "scripting/LuaEngine.h"
#include "ipc/IPCServer.h"
#include "engine/Log.h"
#import <AppKit/AppKit.h>

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(StateAPI& state, EngineAPI& engine, LuaEngine& lua)
        : DocumentWindow("Performance",
                         juce::Colour(0xff121212),
                         DocumentWindow::allButtons),
          mainLayout(new MainLayout(state, engine, lua)) {
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
    openSong,
    saveSong,
    closeSong,
    newInstrumentTrack,
    newEffectsBus,
    toggleSidebar,
    toggleMixer,
};

class AppMenuBar : public juce::MenuBarModel {
public:
    AppMenuBar(PerformanceAPI& api, StateAPI& state, LuaEngine& lua, MainLayout& layout)
        : api(api), state(state), lua(lua), layout(layout) {}

    juce::StringArray getMenuBarNames() override {
        return { "File", "Track", "View" };
    }

    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override {
        juce::PopupMenu menu;
        if (index == 0) {  // File
            menu.addItem(CommandIDs::newSong, "New Song");
            menu.addItem(CommandIDs::saveSong, "Save Song", api.isSongLoaded());
            menu.addSeparator();

            // Song list
            auto songs = lua.listSongs();
            for (int i = 0; i < (int)songs.size(); ++i) {
                menu.addItem(100 + i, juce::String("Load: ") + juce::String(songs[i]));
            }

            if (!songs.empty())
                menu.addSeparator();
            menu.addItem(CommandIDs::closeSong, "Close Song", api.isSongLoaded());
        }
        else if (index == 1) {  // Track
            menu.addItem(CommandIDs::newInstrumentTrack, "New Instrument Track");
            menu.addItem(CommandIDs::newEffectsBus, "New Effects Bus");
        }
        else if (index == 2) {  // View
            menu.addItem(CommandIDs::toggleSidebar, "Toggle Sidebar");
            menu.addItem(CommandIDs::toggleMixer, "Toggle Mixer");
        }
        return menu;
    }

    void menuItemSelected(int menuItemID, int) override {
        if (menuItemID == CommandIDs::newSong) {
            // Create a new empty song
            auto name = "Untitled " + juce::String(juce::Time::currentTimeMillis() % 10000);
            api.createSong(name);
            perfLog("[Menu] Created new song: %s\n", name.toRawUTF8());
        }
        else if (menuItemID == CommandIDs::saveSong) {
            api.saveInitialState();
        }
        else if (menuItemID == CommandIDs::closeSong) {
            api.unloadSong();
        }
        else if (menuItemID == CommandIDs::newInstrumentTrack) {
            auto tracks = state.listTracks();
            auto name = "Track " + juce::String((int)tracks.size() + 1);
            state.createTrack(name.toStdString());
            perfLog("[Menu] Created track: %s\n", name.toRawUTF8());
        }
        else if (menuItemID == CommandIDs::newEffectsBus) {
            auto busses = state.listBusses();
            auto name = "Bus " + juce::String((int)busses.size() + 1);
            state.createBus(name.toStdString());
            perfLog("[Menu] Created bus: %s\n", name.toRawUTF8());
        }
        else if (menuItemID == CommandIDs::toggleSidebar) {
            layout.handleGlobalKey(juce::KeyPress('s', {}, 's'));
        }
        else if (menuItemID == CommandIDs::toggleMixer) {
            layout.handleGlobalKey(juce::KeyPress('x', {}, 'x'));
        }
        else if (menuItemID >= 100) {
            // Load song by index
            auto songs = lua.listSongs();
            int idx = menuItemID - 100;
            if (idx < (int)songs.size()) {
                auto path = LuaEngine::getSongsDirectory() + "/" + songs[idx] + ".lua";
                lua.loadSong(path);
            }
        }
    }

private:
    PerformanceAPI& api;
    StateAPI& state;
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

        api = std::make_unique<PerformanceAPI>();
        api->initialise();

        coordinator = std::make_unique<PerformanceCoordinator>();
        coordinator->initialise();

        luaEngine = std::make_unique<LuaEngine>(*api);

        mainWindow = std::make_unique<MainWindow>(coordinator->state(), coordinator->engine(), *luaEngine);

        // Menu bar (needs references to api, state, lua, and layout)
        auto* layout = mainWindow->getMainLayout();
        menuBar = std::make_unique<AppMenuBar>(*api, coordinator->state(), *luaEngine, *layout);
        juce::MenuBarModel::setMacMainMenu(menuBar.get());

        // Wire sidebar song loading through coordinator
        layout->getSidebar().onLoadSong = [this](const std::string& songId) {
            api->loadSongFromRegistry(songId);
        };

        ipcServer = std::make_unique<IPCServer>(*luaEngine);
        ipcServer->start();

        // Restore session from registry (creates default if first run)
        juce::Timer::callAfterDelay(100, [this] {
            api->restoreSession();
        });
    }

    void shutdown() override {
        juce::MenuBarModel::setMacMainMenu(nullptr);
        menuBar.reset();
        ipcServer.reset();
        luaEngine.reset();
        mainWindow.reset();
        coordinator.reset();
        api.reset();
    }

    void systemRequestedQuit() override {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PerformanceAPI> api;
    std::unique_ptr<PerformanceCoordinator> coordinator;
    std::unique_ptr<LuaEngine> luaEngine;
    std::unique_ptr<IPCServer> ipcServer;
    std::unique_ptr<AppMenuBar> menuBar;
};

START_JUCE_APPLICATION(PerformanceApp)
