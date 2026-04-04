#include <juce_gui_basics/juce_gui_basics.h>
#include "api/PerformanceAPI.h"
#include "gui/MixerView.h"
#include "scripting/LuaEngine.h"
#include "ipc/IPCServer.h"
#include "engine/Log.h"

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow(PerformanceAPI& api)
        : DocumentWindow("Performance",
                         juce::Colour(0xff121212),
                         DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new MixerView(api), false);

        // Size to fill the screen
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
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

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

        ipcServer = std::make_unique<IPCServer>(*luaEngine);
        ipcServer->start();

        // Load default song if it exists
        auto songsDir = LuaEngine::getSongsDirectory();
        auto defaultSong = songsDir + "/two_instruments.lua";
        juce::Timer::callAfterDelay(100, [this, defaultSong] {
            if (juce::File(defaultSong).existsAsFile()) {
                luaEngine->loadSong(defaultSong);
            } else {
                perfLog("[App] No default song found at %s\n", defaultSong.c_str());
                auto songs = luaEngine->listSongs();
                if (!songs.empty()) {
                    auto path = LuaEngine::getSongsDirectory() + "/" + songs[0] + ".lua";
                    luaEngine->loadSong(path);
                } else {
                    perfLog("[App] No songs found. Create .lua files in %s\n",
                            LuaEngine::getSongsDirectory().c_str());
                }
            }
        });
    }

    void shutdown() override {
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
};

START_JUCE_APPLICATION(PerformanceApp)
