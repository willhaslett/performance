#include <juce_gui_basics/juce_gui_basics.h>
#include "engine/AudioEngine.h"
#include "engine/MIDIEngine.h"
#include "engine/Log.h"
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

    void initialise(const juce::String&) override {
        setvbuf(stderr, nullptr, _IONBF, 0);
        initLog();
        perfLog("[App] Starting Performance\n");
        mainWindow = std::make_unique<MainWindow>();

        audioEngine = std::make_unique<AudioEngine>();
        audioEngine->initialise();
        perfLog("[App] AudioEngine initialised\n");

        songRuntime = std::make_unique<SongRuntime>(*audioEngine);

        midiEngine = std::make_unique<MIDIEngine>(
            audioEngine->getDeviceManager(), *audioEngine);
        midiEngine->setSongRuntime(songRuntime.get());
        midiEngine->setMonitorMode(true);
        midiEngine->initialise();
        perfLog("[App] MIDIEngine initialised\n");

        // Two-instrument test: Keyscape + Massive X
        // Channel 10 note events switch which instrument receives MIDI
        SongDef song;
        song.name = "Two Instruments";
        song.addInstrument("Keys", "Keyscape");
        song.addInstrument("Synth", "Massive X");

        // Pad 1 (note 36, ch10) -> activate Keys
        song.bind({ MIDIControl::Note, 10, 36 }, [this](float value) {
            if (value == 0.0f) return;  // ignore note-off
            audioEngine->setChainMidiEnabled("Keys", true);
            audioEngine->setChainMidiEnabled("Synth", false);
            perfLog("[Song] Switched to Keys\n");
        }, "Pad 1 -> Keys");

        // Pad 2 (note 37, ch10) -> activate Synth
        song.bind({ MIDIControl::Note, 10, 37 }, [this](float value) {
            if (value == 0.0f) return;
            audioEngine->setChainMidiEnabled("Keys", false);
            audioEngine->setChainMidiEnabled("Synth", true);
            perfLog("[Song] Switched to Synth\n");
        }, "Pad 2 -> Synth");

        juce::Timer::callAfterDelay(100, [this, song] {
            perfLog("[App] Loading song: %s\n", song.name.toRawUTF8());
            songRuntime->load(song);
        });
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
};

START_JUCE_APPLICATION(PerformanceApp)
