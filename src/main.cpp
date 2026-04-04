#include <juce_gui_basics/juce_gui_basics.h>
#include "api/PerformanceAPI.h"
#include "engine/Log.h"

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

    void initialise(const juce::String&) override {
        setvbuf(stderr, nullptr, _IONBF, 0);
        initLog();
        perfLog("[App] Starting Performance\n");
        mainWindow = std::make_unique<MainWindow>();

        api = std::make_unique<PerformanceAPI>();
        api->initialise();

        // Test song: two instruments + reverb bus, built entirely through the API
        juce::Timer::callAfterDelay(100, [this] {
            api->createBus("Reverb");
            api->addBusEffect("Reverb", "Hall", "Raum");

            api->createTrack("Keys");
            api->addInstrument("Keys", "Keyscape");
            api->addSend("Keys", "Reverb", 0.3f);

            api->createTrack("Synth");
            api->addInstrument("Synth", "Massive X");
            api->setTrackMidiEnabled("Synth", false);
            api->addSend("Synth", "Reverb", 0.5f);

            // Pad 1 (note 36, ch10) -> activate Keys
            api->bind("note", 10, 36, [this](float value) {
                if (value == 0.0f) return;
                api->setTrackMidiEnabled("Keys", true);
                api->setTrackMidiEnabled("Synth", false);
                api->log("Switched to Keys");
            }, "Pad 1 -> Keys");

            // Pad 2 (note 37, ch10) -> activate Synth
            api->bind("note", 10, 37, [this](float value) {
                if (value == 0.0f) return;
                api->setTrackMidiEnabled("Keys", false);
                api->setTrackMidiEnabled("Synth", true);
                api->log("Switched to Synth");
            }, "Pad 2 -> Synth");

            api->log("Song loaded: Two Instruments + Reverb Bus");
        });
    }

    void shutdown() override {
        api.reset();
        mainWindow.reset();
    }

    void systemRequestedQuit() override {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<PerformanceAPI> api;
};

START_JUCE_APPLICATION(PerformanceApp)
