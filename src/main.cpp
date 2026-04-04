#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/AudioEngine.h"
#include "engine/MIDIEngine.h"
#include "song/Song.h"
#include "song/SongRuntime.h"

class MainWindow : public juce::DocumentWindow {
public:
    MainWindow()
        : DocumentWindow("Performance",
                         juce::Colours::black,
                         DocumentWindow::allButtons) {
        setUsingNativeTitleBar(true);
        setContentOwned(new juce::Component(), false);
        getContentComponent()->setSize(400, 200);
        centreWithSize(400, 200);
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

    void initialise(const juce::String& commandLine) override {
        mainWindow = std::make_unique<MainWindow>();

        audioEngine = std::make_unique<AudioEngine>();
        audioEngine->initialise();

        songRuntime = std::make_unique<SongRuntime>(*audioEngine);

        midiEngine = std::make_unique<MIDIEngine>(
            audioEngine->getDeviceManager(), *audioEngine);
        midiEngine->setSongRuntime(songRuntime.get());
        midiEngine->setMonitorMode(true);
        midiEngine->initialise();

        DBG("");
        DBG("MIDI monitor active. Move controls to see CC numbers.");
        DBG("Pass a plugin name to load: ./Performance \"DLSMusicDevice\"");

        if (commandLine.isNotEmpty()) {
            pluginToLoad = commandLine;
            juce::Timer::callAfterDelay(100, [this] {
                audioEngine->loadInstrument(pluginToLoad);
            });
        }
    }

    void shutdown() override {
        songRuntime.reset();
        midiEngine.reset();
        audioEngine.reset();
        mainWindow.reset();
    }

    void systemRequestedQuit() override {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<AudioEngine> audioEngine;
    std::unique_ptr<MIDIEngine> midiEngine;
    std::unique_ptr<SongRuntime> songRuntime;
    juce::String pluginToLoad;
};

START_JUCE_APPLICATION(PerformanceApp)
