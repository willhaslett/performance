#include <juce_gui_basics/juce_gui_basics.h>
#include "api/PerformanceCoordinator.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "gui/MainLayout.h"
#include "gui/KeyBindings.h"
#include "scripting/LuaEngine.h"
#include "ipc/IPCServer.h"
#include "gui/SettingsWindow.h"
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
                else if (event.keyCode == 123)
                    key = juce::KeyPress(juce::KeyPress::leftKey, juce::ModifierKeys::getCurrentModifiers(), 0);
                else if (event.keyCode == 124)
                    key = juce::KeyPress(juce::KeyPress::rightKey, juce::ModifierKeys::getCurrentModifiers(), 0);
                else if (event.keyCode == 125)
                    key = juce::KeyPress(juce::KeyPress::downKey, juce::ModifierKeys::getCurrentModifiers(), 0);
                else if (event.keyCode == 126)
                    key = juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::getCurrentModifiers(), 0);

                if (mainLayout->handleGlobalKey(key))
                    return nil;
                return event;
            }];

        keyUpMonitor = [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyUp
            handler:^NSEvent* (NSEvent* event) {
                juce::juce_wchar c = event.characters.length > 0
                    ? [event.characters characterAtIndex:0] : 0;
                auto key = juce::KeyPress(c, juce::ModifierKeys::getCurrentModifiers(), c);
                if (mainLayout->handleGlobalKeyUp(key))
                    return nil;
                return event;
            }];
    }

    ~MainWindow() override {
        if (keyMonitor)
            [NSEvent removeMonitor:keyMonitor];
        if (keyUpMonitor)
            [NSEvent removeMonitor:keyUpMonitor];
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    MainLayout* getMainLayout() { return mainLayout; }

private:
    MainLayout* mainLayout;
    id keyMonitor = nil;
    id keyUpMonitor = nil;
};

// --- Menu Bar ---

enum CommandIDs {
    // File
    newSong = 1, saveSong = 2, closeSong = 3,
    // Edit
    menuUndo = 10, menuRedo = 11, menuSplit = 12, menuDuplicate = 13,
    menuDelete = 14, menuSelectAll = 15, menuCycleFromSel = 16,
    // Track
    newInstrumentTrack = 20, newAudioTrack = 21, newEffectsBus = 22,
    // View
    viewSidebar = 30, viewMixer = 31, viewMusicalTyping = 32,
    viewZoomIn = 33, viewZoomOut = 34, viewZoomTaller = 35, viewZoomShorter = 36,
    // Transport
    transportPlayStop = 40, transportRecord = 41, transportRewind = 42,
    transportCycle = 43, transportMetronome = 44,
    transportStepFwd = 45, transportStepBack = 46,
    transportStepFwdBar = 47, transportStepBackBar = 48,
    // Other
    openSettings = 99,
};

class AppMenuBar : public juce::MenuBarModel {
public:
    AppMenuBar(PerformanceCoordinator& coord, LuaEngine& lua, MainLayout& layout)
        : coord(coord), lua(lua), layout(layout) {}

    juce::StringArray getMenuBarNames() override {
        return { "File", "Edit", "Track", "View", "Transport" };
    }

    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override {
        juce::PopupMenu menu;
        if (index == 0) {  // File
            menu.addItem(CommandIDs::newSong, "New Song");
            menu.addItem(CommandIDs::saveSong, juce::String("Save") + juce::String::fromUTF8("                          \xe2\x8c\x98S"));
            menu.addSeparator();
            auto& songs = coord.state().allSongs();
            for (int i = 0; i < (int)songs.size(); ++i) {
                auto isCurrent = songs[i].id == coord.state().getMasterOutputId();
                menu.addItem(100 + i, juce::String(songs[i].name), true, isCurrent);
            }
            if (!songs.empty()) menu.addSeparator();
            menu.addItem(CommandIDs::closeSong, "Close Song");
        }
        else if (index == 1) {  // Edit
            menu.addItem(CommandIDs::menuUndo, "Undo", coord.state().canUndo());
            menu.addItem(CommandIDs::menuRedo, "Redo", coord.state().canRedo());
            menu.addSeparator();
            menu.addItem(CommandIDs::menuSplit, "Split at Playhead");
            menu.addItem(CommandIDs::menuDuplicate, "Duplicate");
            menu.addItem(CommandIDs::menuDelete, "Delete");
            menu.addSeparator();
            menu.addItem(CommandIDs::menuCycleFromSel, "Set Cycle from Selection");
        }
        else if (index == 2) {  // Track
            menu.addItem(CommandIDs::newInstrumentTrack, "New Virtual Instrument Track");
            menu.addItem(CommandIDs::newAudioTrack, "New Audio Input Track");
            menu.addItem(CommandIDs::newEffectsBus, "New Effects Bus");
        }
        else if (index == 3) {  // View
            menu.addItem(CommandIDs::viewSidebar, "Sidebar");
            menu.addItem(CommandIDs::viewMixer, "Mixer");
            menu.addItem(CommandIDs::viewMusicalTyping, "Musical Typing");
            menu.addSeparator();
            menu.addSubMenu("Sidebar", layout.buildPaneMenu(PaneSlot::Sidebar));
            menu.addSubMenu("Left Pane", layout.buildPaneMenu(PaneSlot::Left));
            menu.addSubMenu("Right Pane", layout.buildPaneMenu(PaneSlot::Right));
            menu.addSubMenu("Bottom Pane", layout.buildPaneMenu(PaneSlot::Bottom));
            menu.addSeparator();
            menu.addItem(CommandIDs::viewZoomIn, "Zoom In");
            menu.addItem(CommandIDs::viewZoomOut, "Zoom Out");
            menu.addItem(CommandIDs::viewZoomTaller, "Zoom Tracks Taller");
            menu.addItem(CommandIDs::viewZoomShorter, "Zoom Tracks Shorter");
        }
        else if (index == 4) {  // Transport
            menu.addItem(CommandIDs::transportPlayStop, "Play/Stop");
            menu.addItem(CommandIDs::transportRecord, "Record");
            menu.addItem(CommandIDs::transportRewind, "Rewind");
            menu.addSeparator();
            menu.addItem(CommandIDs::transportCycle, "Cycle Mode");
            menu.addItem(CommandIDs::transportMetronome, "Metronome");
            menu.addSeparator();
            menu.addItem(CommandIDs::transportStepFwd, "Step Forward");
            menu.addItem(CommandIDs::transportStepBack, "Step Back");
            menu.addItem(CommandIDs::transportStepFwdBar, "Step Forward Bar");
            menu.addItem(CommandIDs::transportStepBackBar, "Step Back Bar");
        }
        return menu;
    }

    void menuItemSelected(int menuItemID, int) override {
        auto& state = coord.state();

        switch (menuItemID) {
        // File
        case CommandIDs::newSong: {
            auto name = "Untitled " + juce::String(juce::Time::currentTimeMillis() % 10000);
            coord.createSong(name);
            break;
        }
        case CommandIDs::saveSong: coord.save(); break;
        case CommandIDs::closeSong: coord.unloadSong(); break;

        // Edit
        case CommandIDs::menuUndo: layout.onUndo(); break;
        case CommandIDs::menuRedo: layout.onRedo(); break;
        case CommandIDs::menuSplit:
            layout.producePane.keyPressed(juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0));
            break;
        case CommandIDs::menuDuplicate:
            layout.producePane.keyPressed(juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0));
            break;
        case CommandIDs::menuDelete:
            layout.producePane.keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey));
            break;
        case CommandIDs::menuCycleFromSel:
            layout.producePane.keyPressed(juce::KeyPress('u', 0, 'u'));
            break;

        // Track
        case CommandIDs::newInstrumentTrack: {
            auto tracks = state.listTracks();
            state.createTrack(("Track " + juce::String((int)tracks.size() + 1)).toStdString());
            break;
        }
        case CommandIDs::newAudioTrack: {
            auto tracks = state.listTracks();
            state.createAudioInputTrack(("Audio " + juce::String((int)tracks.size() + 1)).toStdString(), -1, 0);
            break;
        }
        case CommandIDs::newEffectsBus: {
            auto busses = state.listBusses();
            state.createBus(("Bus " + juce::String((int)busses.size() + 1)).toStdString());
            break;
        }

        // View
        case CommandIDs::viewSidebar: layout.handleGlobalKey(KeyBindings::toggleSidebar); break;
        case CommandIDs::viewMixer: layout.handleGlobalKey(KeyBindings::toggleMixer); break;
        case CommandIDs::viewMusicalTyping: layout.handleGlobalKey(KeyBindings::musicalTyping); break;
        case CommandIDs::viewZoomIn:
            layout.producePane.keyPressed(juce::KeyPress('l', juce::ModifierKeys::commandModifier, 0));
            break;
        case CommandIDs::viewZoomOut:
            layout.producePane.keyPressed(juce::KeyPress('h', juce::ModifierKeys::commandModifier, 0));
            break;
        case CommandIDs::viewZoomTaller:
            layout.producePane.keyPressed(juce::KeyPress('j', juce::ModifierKeys::commandModifier, 0));
            break;
        case CommandIDs::viewZoomShorter:
            layout.producePane.keyPressed(juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0));
            break;

        // Transport
        case CommandIDs::transportPlayStop:
            layout.producePane.keyPressed(juce::KeyPress(juce::KeyPress::spaceKey));
            break;
        case CommandIDs::transportRecord:
            layout.producePane.keyPressed(juce::KeyPress('r', 0, 'r'));
            break;
        case CommandIDs::transportRewind:
            layout.producePane.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
            break;
        case CommandIDs::transportCycle:
            layout.producePane.keyPressed(juce::KeyPress('c', 0, 'c'));
            break;
        case CommandIDs::transportMetronome:
            layout.producePane.keyPressed(juce::KeyPress('m', 0, 'm'));
            break;
        case CommandIDs::transportStepFwd:
            layout.producePane.keyPressed(juce::KeyPress('l', 0, 'l'));
            break;
        case CommandIDs::transportStepBack:
            layout.producePane.keyPressed(juce::KeyPress('h', 0, 'h'));
            break;
        case CommandIDs::transportStepFwdBar:
            layout.producePane.keyPressed(juce::KeyPress('L', juce::ModifierKeys::shiftModifier, 0));
            break;
        case CommandIDs::transportStepBackBar:
            layout.producePane.keyPressed(juce::KeyPress('H', juce::ModifierKeys::shiftModifier, 0));
            break;

        // Settings
        case CommandIDs::openSettings: layout.handleGlobalKey(KeyBindings::settings); break;

        default:
            if (menuItemID >= 100) {
                auto& songs = state.allSongs();
                int idx = menuItemID - 100;
                if (idx < (int)songs.size())
                    coord.loadSong(songs[idx].id);
            }
            break;
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

        // Crash handler — log context before dying
        juce::SystemStats::setApplicationCrashHandler([](void*) {
            perfLog("[App] CRASH — writing crash context\n");
            // Try to save state if possible
            if (auto* app = dynamic_cast<PerformanceApp*>(juce::JUCEApplication::getInstance())) {
                if (app->coordinator) {
                    try {
                        app->coordinator->save();
                        perfLog("[App] Emergency save completed\n");
                    } catch (...) {
                        perfLog("[App] Emergency save failed\n");
                    }
                }
            }
        });

        perfLog("[App] Starting Performance\n");

        // New system: in-memory state + persistence
        coordinator = std::make_unique<PerformanceCoordinator>();
        coordinator->initialise();

        luaEngine = std::make_unique<LuaEngine>(
            coordinator->state(), coordinator->engine(), *coordinator);

        // Wire Lua executor for custom actions
        auto* lua = luaEngine.get();
        coordinator->luaExecutor = [lua](const std::string& code) {
            return lua->executeString(code);
        };

        mainWindow = std::make_unique<MainWindow>(
            coordinator->state(), coordinator->engine(), *luaEngine, *coordinator);

        // Menu bar
        auto* layout = mainWindow->getMainLayout();
        menuBar = std::make_unique<AppMenuBar>(*coordinator, *luaEngine, *layout);
        auto appMenu = std::make_unique<juce::PopupMenu>();
        appMenu->addItem(CommandIDs::openSettings, "Settings...");
        juce::MenuBarModel::setMacMainMenu(menuBar.get(), appMenu.get());
        appMenuItems = std::move(appMenu);

        // Wire settings
        layout->onOpenSettings = [this]() {
            if (!settingsWindow)
                settingsWindow = std::make_unique<SettingsWindow>(
                    coordinator->state(), coordinator->engine());
            settingsWindow->setVisible(true);
            settingsWindow->toFront(true);
        };

        // Wire save
        layout->onSave = [this, layout]() {
            layout->showOverlay("Saving...");
            juce::MessageManager::callAsync([this, layout]() {
                coordinator->save();
                layout->hideOverlay();
            });
        };

        // Wire undo/redo
        layout->onUndo = [this]() {
            if (coordinator->state().undo()) {
                coordinator->onUndoRedoRestore();
            }
        };
        layout->onRedo = [this]() {
            if (coordinator->state().redo()) {
                coordinator->onUndoRedoRestore();
            }
        };

        // Wire sidebar song loading
        layout->getSidebar().onLoadSong = [this, layout](const std::string& songId) {
            layout->showOverlay("Loading song...");
            juce::MessageManager::callAsync([this, layout, songId]() {
                coordinator->loadSong(songId);
                layout->hideOverlay();
            });
        };

        layout->getSidebar().onDeleteSong = [this, layout](const std::string& songId) {
            // Switch to Sandbox first if deleting the current song
            auto currentId = coordinator->state().getMasterOutputId();
            if (songId == currentId) {
                auto& songs = coordinator->state().allSongs();
                for (auto& s : songs) {
                    if (s.name == "Sandbox" && s.id != songId) {
                        coordinator->loadSong(s.id);
                        break;
                    }
                }
            }
            coordinator->state().deleteSong(songId);
            coordinator->save();
        };

        // Wire sidebar Maps device selection
        layout->getSidebar().onMapSelected = [layout](const std::string& deviceId, const std::string& portName) {
            layout->setPaneContent(PaneSlot::Left, PaneContent::Mappings);
            layout->mappingPane.setDevice(deviceId, portName);
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
            coordinator->save();  // persist device change immediately
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
            coordinator->save();  // persist device change immediately
        };

        // Sidebar pane selection is wired in MainLayout constructor

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
        juce::MessageManager::callAsync([this, layout] {
            layout->showOverlay("Loading session...");
            juce::MessageManager::callAsync([this, layout] {
                coordinator->restoreSession();
                layout->hideOverlay();
            });
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
    std::unique_ptr<juce::PopupMenu> appMenuItems;
    std::unique_ptr<SettingsWindow> settingsWindow;
};

START_JUCE_APPLICATION(PerformanceApp)
