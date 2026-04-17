#include <juce_gui_basics/juce_gui_basics.h>
#include "api/PerformanceCoordinator.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "gui/MainLayout.h"
#include "gui/KeyBindings.h"
#include "scripting/LuaEngine.h"
#include "ipc/IPCServer.h"
#include "engine/AudioEngine.h"
#include "gui/PerformanceLookAndFeel.h"
#include "gui/SettingsWindow.h"
#include "gui/KeyBindingManager.h"
#include "gui/KeyBindingEditor.h"
#include "engine/Log.h"
#include "telemetry/InstallId.h"
#include "BuildVersion.h"
#import <AppKit/AppKit.h>

static juce::String uniqueSongName(StateAPI& state) {
    const juce::String base = "New Song";
    auto taken = [&](const juce::String& name) {
        for (auto& s : state.allSongs())
            if (juce::String(s.name) == name) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2; i < 1000; ++i) {
        auto candidate = base + " " + juce::String::formatted("%02d", i);
        if (!taken(candidate)) return candidate;
    }
    return base;  // unreachable under normal use
}

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

// --- Menu styling: gray shortcuts in native macOS menus ---

static void styleMenuShortcuts(NSMenu* menu) {
    // Right-aligned tab stop for shortcut text
    NSMutableParagraphStyle* paraStyle = [[NSMutableParagraphStyle alloc] init];
    NSTextTab* rightTab = [[NSTextTab alloc] initWithTextAlignment:NSTextAlignmentRight
                                                          location:180
                                                           options:@{}];
    [paraStyle setTabStops:@[rightTab]];

    for (NSMenuItem* item in [menu itemArray]) {
        if ([item hasSubmenu]) {
            styleMenuShortcuts([item submenu]);
            continue;
        }
        NSString* title = [item title];
        NSRange tabRange = [title rangeOfString:@"\t"];
        if (tabRange.location != NSNotFound) {
            NSString* label = [title substringToIndex:tabRange.location];
            NSString* shortcut = [title substringFromIndex:tabRange.location + 1];
            NSString* full = [NSString stringWithFormat:@"%@\t%@", label, shortcut];

            NSMutableAttributedString* attr = [[NSMutableAttributedString alloc]
                initWithString:full attributes:@{
                    NSFontAttributeName: [NSFont menuFontOfSize:14],
                    NSForegroundColorAttributeName: [NSColor labelColor],
                    NSParagraphStyleAttributeName: paraStyle
                }];

            // Style the shortcut portion (after tab) in gray, slightly smaller
            NSRange shortcutRange = NSMakeRange(tabRange.location, [full length] - tabRange.location);
            [attr addAttribute:NSForegroundColorAttributeName
                         value:[NSColor tertiaryLabelColor]
                         range:shortcutRange];
            [attr addAttribute:NSFontAttributeName
                         value:[NSFont menuFontOfSize:12]
                         range:shortcutRange];

            [item setAttributedTitle:attr];
        }
    }
}

@interface MenuStyleObserver : NSObject
@end

@implementation MenuStyleObserver
- (void)menuOpened:(NSNotification*)notification {
    NSMenu* menu = [notification object];
    // Delay slightly to let JUCE finish populating the menu
    dispatch_async(dispatch_get_main_queue(), ^{
        styleMenuShortcuts(menu);
    });
}
@end

static MenuStyleObserver* menuStyleObserver = nil;

static void installMenuStyling() {
    if (!menuStyleObserver) {
        menuStyleObserver = [[MenuStyleObserver alloc] init];
        [[NSNotificationCenter defaultCenter] addObserver:menuStyleObserver
                                                 selector:@selector(menuOpened:)
                                                     name:NSMenuDidBeginTrackingNotification
                                                   object:nil];
    }
}

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
        : coord(coord), lua(lua), layout(layout) {
        // macOS caches the native menu bar's submenu contents. When the songs
        // list or the current song changes, we have to call menuItemsChanged()
        // to invalidate the cache so File → Open Song reflects the new state.
        stateSubId = coord.state().events().subscribe([this](const StateEvent& ev) {
            if (ev.entity == StateEvent::Song || ev.entity == StateEvent::Config) {
                juce::MessageManager::callAsync([this] { menuItemsChanged(); });
            }
        });
    }

    ~AppMenuBar() override {
        if (stateSubId >= 0) coord.state().events().unsubscribe(stateSubId);
    }

    void setKeyBindingManager(KeyBindingManager* mgr) { kb = mgr; }

    juce::StringArray getMenuBarNames() override {
        return { "File", "Edit", "Track", "View", "Transport" };
    }

    juce::PopupMenu getMenuForIndex(int index, const juce::String&) override {
        juce::PopupMenu menu;
        // Helper: menu item with shortcut from KeyBindingManager (or fallback string)
        auto shortcut = [this](int id, const juce::String& label, const std::string& cmdId, bool enabled = true) {
            juce::String key;
            if (kb) key = kb->getShortcutText(cmdId);
            juce::PopupMenu::Item item(key.isEmpty() ? label : label + "\t" + key);
            item.itemID = id;
            item.isEnabled = enabled;
            return item;
        };

        if (index == 0) {  // File
            menu.addItem(shortcut(CommandIDs::newSong, "New Song", "file.newSong"));

            // Open Song submenu — lists all songs, ticks the current one
            juce::PopupMenu openSubmenu;
            auto& songs = coord.state().allSongs();
            std::string currentSongId;
            if (auto* cur = coord.state().currentSong())
                currentSongId = cur->id;
            for (int i = 0; i < (int)songs.size(); ++i) {
                bool isCurrent = (songs[i].id == currentSongId);
                openSubmenu.addItem(100 + i, juce::String(songs[i].name), true, isCurrent);
            }
            menu.addSubMenu("Open Song", openSubmenu, !songs.empty());

            menu.addItem(shortcut(CommandIDs::saveSong, "Save", "file.save"));
            menu.addSeparator();
            menu.addItem(CommandIDs::closeSong, "Close Song");
        }
        else if (index == 1) {  // Edit
            menu.addItem(shortcut(CommandIDs::menuUndo, "Undo", "edit.undo", coord.state().canUndo()));
            menu.addItem(shortcut(CommandIDs::menuRedo, "Redo", "edit.redo", coord.state().canRedo()));
            menu.addSeparator();
            menu.addItem(shortcut(CommandIDs::menuSplit, "Split at Playhead", "edit.split"));
            menu.addItem(shortcut(CommandIDs::menuDuplicate, "Duplicate", "edit.duplicate"));
            menu.addItem(shortcut(CommandIDs::menuDelete, "Delete", "edit.delete"));
            menu.addSeparator();
            menu.addItem(shortcut(CommandIDs::menuCycleFromSel, "Set Cycle from Selection", "edit.cycleFromSel"));
        }
        else if (index == 2) {  // Track
            menu.addItem(CommandIDs::newInstrumentTrack, "New Virtual Instrument Track");
            menu.addItem(CommandIDs::newAudioTrack, "New Audio Input Track");
            menu.addItem(CommandIDs::newEffectsBus, "New Effects Bus");
        }
        else if (index == 3) {  // View
            menu.addItem(shortcut(CommandIDs::viewSidebar, "Sidebar", "view.sidebar"));
            menu.addItem(shortcut(50, "Produce", "view.produce"));
            menu.addItem(shortcut(51, "Mappings", "view.mappings"));
            menu.addItem(shortcut(CommandIDs::viewMixer, "Mixer", "view.mixer"));
            menu.addItem(shortcut(52, "Chat", "view.chat"));
            menu.addItem(shortcut(53, "Logs", "view.logs"));
            menu.addItem(shortcut(CommandIDs::viewMusicalTyping, "Musical Typing", "view.musicalTyping"));
            menu.addSeparator();
            menu.addItem(shortcut(CommandIDs::viewZoomIn, "Zoom In", "view.zoomIn"));
            menu.addItem(shortcut(CommandIDs::viewZoomOut, "Zoom Out", "view.zoomOut"));
            menu.addItem(shortcut(CommandIDs::viewZoomTaller, "Zoom Tracks Taller", "view.zoomTaller"));
            menu.addItem(shortcut(CommandIDs::viewZoomShorter, "Zoom Tracks Shorter", "view.zoomShorter"));
        }
        else if (index == 4) {  // Transport
            menu.addItem(shortcut(CommandIDs::transportPlayStop, "Play/Stop", "transport.playStop"));
            menu.addItem(shortcut(CommandIDs::transportRecord, "Record", "transport.record"));
            menu.addItem(shortcut(CommandIDs::transportRewind, "Rewind", "transport.rewind"));
            menu.addSeparator();
            menu.addItem(shortcut(CommandIDs::transportCycle, "Cycle Mode", "transport.cycle"));
            menu.addItem(shortcut(CommandIDs::transportMetronome, "Metronome", "transport.metronome"));
            menu.addSeparator();
            menu.addItem(shortcut(CommandIDs::transportStepFwd, "Step Forward", "transport.stepFwd"));
            menu.addItem(shortcut(CommandIDs::transportStepBack, "Step Back", "transport.stepBack"));
            menu.addItem(shortcut(CommandIDs::transportStepFwdBar, "Step Forward Bar", "transport.stepFwdBar"));
            menu.addItem(shortcut(CommandIDs::transportStepBackBar, "Step Back Bar", "transport.stepBackBar"));
        }
        return menu;
    }

    void menuItemSelected(int menuItemID, int) override {
        auto& state = coord.state();

        switch (menuItemID) {
        // File
        case CommandIDs::newSong: {
            coord.createDefaultSong(uniqueSongName(coord.state()).toStdString());
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
        case CommandIDs::viewSidebar:
            if (kb) layout.handleGlobalKey(kb->getKey("view.sidebar")); break;
        case CommandIDs::viewMixer:
            if (kb) layout.handleGlobalKey(kb->getKey("view.mixer")); break;
        case 50: if (kb) layout.handleGlobalKey(kb->getKey("view.produce")); break;
        case 51: if (kb) layout.handleGlobalKey(kb->getKey("view.mappings")); break;
        case 52: if (kb) layout.handleGlobalKey(kb->getKey("view.chat")); break;
        case 53: if (kb) layout.handleGlobalKey(kb->getKey("view.logs")); break;
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
        case 200:
            if (onShowKeyBindings) onShowKeyBindings();
            break;

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

    std::function<void()> onShowKeyBindings;

private:
    PerformanceCoordinator& coord;
    LuaEngine& lua;
    MainLayout& layout;
    KeyBindingManager* kb = nullptr;
    int stateSubId = -1;
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
        perfLog("[App] Build version=%s commit=%s tag=%s dirty=%s\n",
                BUILD_VERSION, BUILD_GIT_COMMIT,
                BUILD_GIT_TAG[0] ? BUILD_GIT_TAG : "(none)",
                BUILD_GIT_DIRTY ? "true" : "false");
        perfLog("[App] Install id=%s firstSeen=%s\n",
                InstallId::id().toRawUTF8(), InstallId::firstSeen().toRawUTF8());

        // Beta expiry — update the date each release cycle.
        // Prevents stale builds from circulating indefinitely.
        {
            auto expiry = juce::Time(2026, 9, 16, 0, 0);  // October 16, 2026 (month is 0-based)
            if (juce::Time::getCurrentTime() > expiry) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon,
                    "Beta Expired",
                    "This beta build has expired. Please contact Will for an updated version.",
                    "OK",
                    nullptr,
                    juce::ModalCallbackFunction::create([](int) {
                        juce::JUCEApplication::getInstance()->systemRequestedQuit();
                    }));
                return;
            }
        }

        // New system: in-memory state + persistence
        coordinator = std::make_unique<PerformanceCoordinator>();
        coordinator->initialise();

        // Set app-wide LookAndFeel AFTER theme loads (coordinator->initialise
        // loads the active theme) so popup menus, combo boxes, etc. match.
        static PerformanceLookAndFeel performanceLaf;
        juce::LookAndFeel::setDefaultLookAndFeel(&performanceLaf);

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
        appMenu->addItem(200, "Keyboard Shortcuts...");
        juce::MenuBarModel::setMacMainMenu(menuBar.get(), appMenu.get());
        menuBar->onShowKeyBindings = [this]() { showKeyBindingEditor(); };
        installMenuStyling();
        setupKeyBindings();
        layout->setKeyBindingManager(&keyBindings);
        menuBar->setKeyBindingManager(&keyBindings);
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
        layout->onNewSong = [this]() {
            coordinator->createDefaultSong(uniqueSongName(coordinator->state()).toStdString());
        };
        layout->getSidebar().onNewSong = layout->onNewSong;
        layout->onNewInstrumentTrack = [this]() {
            auto tracks = coordinator->state().listTracks();
            coordinator->state().createTrack(("Track " + juce::String((int)tracks.size() + 1)).toStdString());
        };
        layout->onNewAudioTrack = [this]() {
            auto tracks = coordinator->state().listTracks();
            coordinator->state().createAudioInputTrack(("Audio " + juce::String((int)tracks.size() + 1)).toStdString(), -1, 0);
        };
        layout->onNewBus = [this]() {
            auto busses = coordinator->state().listBusses();
            coordinator->state().createBus(("Bus " + juce::String((int)busses.size() + 1)).toStdString());
        };

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
            // If deleting the current song, switch to another song first.
            // If this is the last song, create a new default song.
            auto currentId = coordinator->state().getMasterOutputId();
            if (songId == currentId) {
                auto& songs = coordinator->state().allSongs();
                bool switched = false;
                for (auto& s : songs) {
                    if (s.id != songId) {
                        coordinator->loadSong(s.id);
                        switched = true;
                        break;
                    }
                }
                if (!switched) {
                    coordinator->createDefaultSong("Untitled");
                }
            }
            coordinator->state().deleteSong(songId);
            coordinator->save();
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

        // Deferred startup — ensures the window is visible before any blocking work.
        juce::MessageManager::callAsync([this, layout] {
            auto& audioEngine = coordinator->engine().getAudioEngine();

            // Plugin scan (only if no cache exists — first install or cache deleted).
            // Runs after the window is visible so the user sees "Scanning plugins..."
            // instead of a frozen/invisible app.
            if (audioEngine.needsPluginScan()) {
                layout->showOverlay("Scanning plugins...");
                layout->repaint();
                // Timer delay lets the overlay paint before the blocking scan starts.
                juce::Timer::callAfterDelay(100, [this, layout, &audioEngine] {
                    audioEngine.scanForPlugins();
                    coordinator->syncPluginCatalog();
                    layout->hideOverlay();
                    continueStartup(layout);
                });
            } else {
                layout->showOverlay("Loading...");
                juce::MessageManager::callAsync([this, layout] {
                    continueStartup(layout);
                });
            }
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
    KeyBindingManager keyBindings;

    void continueStartup(MainLayout* layout) {
        coordinator->restoreSession();
        layout->hideOverlay();
        if (coordinator->needsStartupSongChooser())
            showStartupSongChooser(layout);
    }

    void showStartupSongChooser(MainLayout* layout) {
        // Build song list
        std::vector<std::pair<std::string, std::string>> songList;
        for (auto& s : coordinator->state().allSongs())
            songList.push_back({ s.id, s.name });

        auto& chooser = layout->startupChooser;
        chooser.setSongs(songList);
        chooser.setBounds(layout->getLocalBounds());
        layout->addAndMakeVisible(chooser);
        chooser.toFront(false);

        chooser.onLoadSong = [this, layout](const std::string& songId) {
            layout->removeChildComponent(&layout->startupChooser);
            layout->showOverlay("Loading song...");
            juce::MessageManager::callAsync([this, layout, songId] {
                coordinator->loadSong(songId);
                layout->hideOverlay();
            });
        };
        chooser.onNewSong = [this, layout]() {
            layout->removeChildComponent(&layout->startupChooser);
            coordinator->createDefaultSong(uniqueSongName(coordinator->state()).toStdString());
        };
    }

    void setupKeyBindings() {
        namespace KB = KeyBindings;
        auto& k = keyBindings;
        auto noKey = juce::KeyPress();  // unbound by default

        // File
        k.registerCommand("file.newSong", "File", "New Song", noKey);
        k.registerCommand("file.save", "File", "Save", KB::save);
        k.registerCommand("file.closeSong", "File", "Close Song", noKey);
        k.registerCommand("file.settings", "File", "Settings", KB::settings);

        // Edit
        k.registerCommand("edit.undo", "Edit", "Undo", KB::undo);
        k.registerCommand("edit.redo", "Edit", "Redo", KB::redo);
        k.registerCommand("edit.split", "Edit", "Split at Playhead",
            juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("edit.duplicate", "Edit", "Duplicate",
            juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("edit.delete", "Edit", "Delete",
            juce::KeyPress(juce::KeyPress::backspaceKey));
        k.registerCommand("edit.cycleFromSel", "Edit", "Set Cycle from Selection",
            juce::KeyPress('u', 0, 'u'));

        // Transport
        k.registerCommand("transport.playStop", "Transport", "Play/Stop",
            juce::KeyPress(juce::KeyPress::spaceKey));
        k.registerCommand("transport.record", "Transport", "Record",
            juce::KeyPress('r', 0, 'r'));
        k.registerCommand("transport.rewind", "Transport", "Rewind",
            juce::KeyPress(juce::KeyPress::returnKey));
        k.registerCommand("transport.cycle", "Transport", "Cycle Mode",
            juce::KeyPress('c', 0, 'c'));
        k.registerCommand("transport.metronome", "Transport", "Metronome",
            juce::KeyPress('m', 0, 'm'));
        k.registerCommand("transport.stepFwd", "Transport", "Step Forward",
            juce::KeyPress('l', 0, 'l'));
        k.registerCommand("transport.stepBack", "Transport", "Step Back",
            juce::KeyPress('h', 0, 'h'));
        k.registerCommand("transport.stepFwdBar", "Transport", "Step Forward Bar",
            juce::KeyPress('L', juce::ModifierKeys::shiftModifier, 0));
        k.registerCommand("transport.stepBackBar", "Transport", "Step Back Bar",
            juce::KeyPress('H', juce::ModifierKeys::shiftModifier, 0));

        // View
        k.registerCommand("view.sidebar", "View", "Sidebar",
            juce::KeyPress('p', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.mixer", "View", "Mixer",
            juce::KeyPress('o', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.musicalTyping", "View", "Musical Typing", KB::musicalTyping);
        k.registerCommand("view.zoomIn", "View", "Zoom In",
            juce::KeyPress('l', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.zoomOut", "View", "Zoom Out",
            juce::KeyPress('h', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.zoomTaller", "View", "Zoom Tracks Taller",
            juce::KeyPress('j', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.zoomShorter", "View", "Zoom Tracks Shorter",
            juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.produce", "View", "Produce",
            juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.mappings", "View", "Mappings",
            juce::KeyPress('u', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.chat", "View", "Chat",
            juce::KeyPress('i', juce::ModifierKeys::commandModifier, 0));
        k.registerCommand("view.logs", "View", "Logs",
            juce::KeyPress('l', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0));
        k.registerCommand("view.closeEditor", "View", "Close Plugin Editor", KB::closeEditor);

        // Region
        k.registerCommand("region.loop", "Region", "Toggle Loop",
            juce::KeyPress('l', 0, 'l'));
        k.registerCommand("region.mute", "Region", "Mute/Unmute", noKey);

        // Track
        k.registerCommand("track.newInstrument", "Track", "New Instrument Track", noKey);
        k.registerCommand("track.newAudioInput", "Track", "New Audio Input Track", noKey);
        k.registerCommand("track.newBus", "Track", "New Effects Bus", noKey);

        k.loadOverrides(coordinator->state());
    }

    void showKeyBindingEditor() {
        // Muted window chrome — dark theme, no red close button
        struct DarkLookAndFeel : public juce::LookAndFeel_V4 {
            DarkLookAndFeel() {
                setColour(juce::DocumentWindow::textColourId, juce::Colour(0xffcccccc));
                setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(0xff2a2a2a));
            }
            juce::Button* createDocumentWindowButton(int buttonType) override {
                auto* btn = juce::LookAndFeel_V4::createDocumentWindowButton(buttonType);
                btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff3a3a3a));
                btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff4a4a4a));
                return btn;
            }
        };
        static DarkLookAndFeel darkLaf;

        struct KBWindow : public juce::DocumentWindow {
            std::function<void()> onClose;
            KBWindow() : DocumentWindow("Keyboard Shortcuts",
                juce::Colour(0xff2a2a2a), closeButton) {
                setTitleBarHeight(24);
                setLookAndFeel(&darkLaf);
            }
            ~KBWindow() override { setLookAndFeel(nullptr); }
            void closeButtonPressed() override { if (onClose) onClose(); }
        };
        auto* editor = new KeyBindingEditor(keyBindings);
        auto* window = new KBWindow();
        window->setContentOwned(editor, false);
        window->centreWithSize(500, 600);
        window->setUsingNativeTitleBar(false);
        window->setVisible(true);
        window->setAlwaysOnTop(true);

        auto closeAction = [this, window]() {
            keyBindings.saveOverrides(coordinator->state());
            coordinator->save();
            delete window;
        };
        editor->onClose = closeAction;
        window->onClose = closeAction;
    }
};

START_JUCE_APPLICATION(PerformanceApp)
