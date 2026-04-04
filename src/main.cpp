#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/AudioEngine.h"
#include "engine/MIDIEngine.h"

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

        midiEngine = std::make_unique<MIDIEngine>(
            audioEngine->getDeviceManager(), *audioEngine);
        midiEngine->initialise();

        // List all discovered plugins
        audioEngine->listAvailablePlugins();

        // Load plugin after message loop starts (some plugins need the event loop running)
        if (commandLine.isNotEmpty()) {
            pluginToLoad = commandLine;
            juce::Timer::callAfterDelay(100, [this] {
                audioEngine->loadInstrument(pluginToLoad);
                DBG("");
                DBG("Performance is running.");
            });
        } else {
            DBG("");
            DBG("To load an instrument, pass its name as a command line argument:");
            DBG("  ./Performance \"plugin name\"");
            DBG("");
            DBG("Performance is running.");
        }
    }

    void shutdown() override {
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
    juce::String pluginToLoad;
};

START_JUCE_APPLICATION(PerformanceApp)
