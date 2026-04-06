#include <juce_gui_basics/juce_gui_basics.h>
#include "api/PerformanceAPI.h"
#include "gui/MainLayout.h"
#include "scripting/LuaEngine.h"
#include "ipc/IPCServer.h"
#include "engine/Log.h"
#import <AppKit/AppKit.h>

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(PerformanceAPI& api)
        : DocumentWindow("Performance",
                         juce::Colour(0xff121212),
                         DocumentWindow::allButtons),
          mainLayout(new MainLayout(api)) {
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
    AppMenuBar(PerformanceAPI& api, LuaEngine& lua, MainLayout& layout)
        : api(api), lua(lua), layout(layout) {}

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
            auto trackNames = api.listTrackNames();
            auto name = "Track " + juce::String((int)trackNames.size() + 1);
            api.createTrack(name);
            perfLog("[Menu] Created track: %s\n", name.toRawUTF8());
        }
        else if (menuItemID == CommandIDs::newEffectsBus) {
            auto busNames = api.listBusNames();
            auto name = "Bus " + juce::String((int)busNames.size() + 1);
            api.createBus(name);
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

        mainWindow = std::make_unique<MainWindow>(*api);

        luaEngine = std::make_unique<LuaEngine>(*api);

        // Menu bar (needs references to api, lua, and layout)
        auto* layout = dynamic_cast<MainLayout*>(mainWindow->getContentComponent());
        menuBar = std::make_unique<AppMenuBar>(*api, *luaEngine, *layout);
        juce::MenuBarModel::setMacMainMenu(menuBar.get());

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
        api.reset();
    }

    void systemRequestedQuit() override {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PerformanceAPI> api;
    std::unique_ptr<LuaEngine> luaEngine;
    std::unique_ptr<IPCServer> ipcServer;
    std::unique_ptr<AppMenuBar> menuBar;
};

START_JUCE_APPLICATION(PerformanceApp)
