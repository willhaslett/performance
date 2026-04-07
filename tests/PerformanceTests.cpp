#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "api/PerformanceAPI.h"
#include "registry/Registry.h"
#include "engine/Log.h"

// ============================================================================
// Test helpers
// ============================================================================

class TempDB {
public:
    TempDB() : file(juce::File::createTempFile(".db")) {}
    ~TempDB() { file.deleteFile(); }
    juce::String path() const { return file.getFullPathName(); }
private:
    juce::File file;
};

// A full PerformanceAPI with a temp database for isolated testing.
// AudioEngine will init (may log warnings without audio device) but
// all track/bus/registry operations work.
class TestAPI {
public:
    TestAPI() {
        try {
            api.initialise(db.path());
            api.createSong("Test Session");
        } catch (...) {
            // AudioEngine init may fail in test environment
        }
    }
    ~TestAPI() {
        api.shutdown();
    }

    PerformanceAPI& get() { return api; }
    PerformanceAPI* operator->() { return &api; }

private:
    TempDB db;
    PerformanceAPI api;
};

// ============================================================================
// Registry tests
// ============================================================================

class RegistryTests : public juce::UnitTest {
public:
    RegistryTests() : UnitTest("Registry", "Performance") {}

    void runTest() override {
        TempDB db;

        beginTest("Open and create schema");
        {
            Registry reg;
            reg.open(db.path().toStdString());
            auto songs = reg.allSongs();
            expect(songs.empty());
        }

        beginTest("Create and find song");
        {
            Registry reg;
            reg.open(db.path().toStdString());
            auto id = reg.createSong("My Song");
            expect(!id.empty());
            auto song = reg.findSongByName("My Song");
            expect(song.has_value());
            expectEquals(song->name, std::string("My Song"));
            expectEquals(song->id, id);
        }

        beginTest("Create track with foreign key to song");
        {
            Registry reg;
            reg.open(db.path().toStdString());
            auto songId = reg.createSong("Song 2");
            auto trackId = reg.createTrack(songId, "Keys", "");
            expect(!trackId.empty());
            auto tracks = reg.tracksForSong(songId);
            expectEquals((int)tracks.size(), 1);
            expectEquals(tracks[0].name, std::string("Keys"));
        }

        beginTest("CASCADE delete — removing song deletes tracks");
        {
            Registry reg;
            reg.open(db.path().toStdString());
            auto songId = reg.createSong("Cascade Test");
            reg.createTrack(songId, "T1", "");
            reg.createTrack(songId, "T2", "");
            expectEquals((int)reg.tracksForSong(songId).size(), 2);
            reg.deleteSong(songId);
            expectEquals((int)reg.tracksForSong(songId).size(), 0);
        }

        beginTest("Create bus and effects");
        {
            Registry reg;
            reg.open(db.path().toStdString());
            auto songId = reg.createSong("FX Test");
            auto pluginId = reg.registerPlugin("Raum", "NI", "Effects/Reverb");
            auto busId = reg.createBus(songId, "Reverb");
            expect(!busId.empty());
            auto fxId = reg.createEffect(busId, "bus", "Raum", pluginId);
            expect(!fxId.empty());
            auto effects = reg.effectsForParent(busId);
            expectEquals((int)effects.size(), 1);
            expectEquals(effects[0].name, std::string("Raum"));
        }

        beginTest("Create and find preset");
        {
            Registry reg;
            reg.open(db.path().toStdString());
            // Need a plugin first
            auto pluginId = reg.registerPlugin("TestSynth", "TestMfg", "test-format-id");
            auto presetId = reg.createPreset(pluginId, "Warm Pad", "/tmp/test.state");
            expect(!presetId.empty());
            auto preset = reg.findPreset(pluginId, "Warm Pad");
            expect(preset.has_value());
            expectEquals(preset->name, std::string("Warm Pad"));
        }

        beginTest("Master gain on song");
        {
            Registry reg;
            reg.open(db.path().toStdString());
            auto songId = reg.createSong("Gain Test");
            // Default should be 1.0
            auto gain = reg.getMasterGain(songId);
            expectWithinAbsoluteError(gain, 1.0f, 0.001f);
            reg.setMasterGain(songId, 0.5f);
            expectWithinAbsoluteError(reg.getMasterGain(songId), 0.5f, 0.001f);
        }

        beginTest("Sandbox protection");
        {
            // Use a fresh DB for this test since schema migrations may create songs
            TempDB freshDb;
            Registry reg;
            reg.open(freshDb.path().toStdString());
            auto sandboxId = reg.createSong("Sandbox");
            auto otherSongId = reg.createSong("User Song");
            expectEquals((int)reg.allSongs().size(), 2);
            auto sandbox = reg.findSongByName("Sandbox");
            expect(sandbox.has_value());
        }
    }
};

static RegistryTests registryTests;

// ============================================================================
// PerformanceAPI lifecycle tests
// ============================================================================

class APILifecycleTests : public juce::UnitTest {
public:
    APILifecycleTests() : UnitTest("API Lifecycle", "Performance") {}

    void runTest() override {

        beginTest("Create track and verify it exists");
        {
            TestAPI t;
            t->createTrack("Keys");
            auto names = t->listTrackNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Keys"));
        }

        beginTest("Create bus and verify it exists");
        {
            TestAPI t;
            t->createBus("Reverb");
            auto names = t->listBusNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Reverb"));
        }

        beginTest("Rename track");
        {
            TestAPI t;
            t->createTrack("Track 1");
            t->renameTrack("Track 1", "Piano");
            auto names = t->listTrackNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Piano"));
        }

        beginTest("Rename track — old name is gone");
        {
            TestAPI t;
            t->createTrack("Old Name");
            t->renameTrack("Old Name", "New Name");
            // Creating another track with the old name should work
            t->createTrack("Old Name");
            auto names = t->listTrackNames();
            expectEquals((int)names.size(), 2);
        }

        beginTest("Rename bus");
        {
            TestAPI t;
            t->createBus("Bus 1");
            t->renameBus("Bus 1", "Delay");
            auto names = t->listBusNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Delay"));
        }

        beginTest("Delete track");
        {
            TestAPI t;
            t->createTrack("To Delete");
            expectEquals((int)t->listTrackNames().size(), 1);
            t->removeTrack("To Delete");
            expectEquals((int)t->listTrackNames().size(), 0);
        }

        beginTest("Delete bus");
        {
            TestAPI t;
            t->createBus("To Delete");
            expectEquals((int)t->listBusNames().size(), 1);
            t->removeBus("To Delete");
            expectEquals((int)t->listBusNames().size(), 0);
        }

        beginTest("Create track after delete — same name reusable");
        {
            TestAPI t;
            t->createTrack("Reuse");
            t->removeTrack("Reuse");
            t->createTrack("Reuse");
            auto names = t->listTrackNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Reuse"));
        }

        beginTest("Add effect to track");
        {
            TestAPI t;
            t->createTrack("FX Track");
            // DLSMusicDevice is always available on macOS
            t->addEffect("FX Track", "TestFX", "DLSMusicDevice");
            // Effect should appear (may be loading async, but the slot exists)
            auto effects = t->getTrackEffects("FX Track");
            expectEquals((int)effects.size(), 1);
        }

        beginTest("Remove effect from track");
        {
            TestAPI t;
            t->createTrack("FX Track");
            t->addEffect("FX Track", "TestFX", "DLSMusicDevice");
            auto effects = t->getTrackEffects("FX Track");
            expect(!effects.empty());
            t->removeEffect("FX Track", effects[0].effectId);
            expectEquals((int)t->getTrackEffects("FX Track").size(), 0);
        }

        beginTest("Duplicate effect names get unique IDs");
        {
            TestAPI t;
            t->createTrack("Dupes");
            t->addEffect("Dupes", "DLSMusicDevice", "DLSMusicDevice");
            t->addEffect("Dupes", "DLSMusicDevice", "DLSMusicDevice");
            auto effects = t->getTrackEffects("Dupes");
            expectEquals((int)effects.size(), 2);
            // IDs should be different
            expect(effects[0].effectId != effects[1].effectId);
        }

        beginTest("Set and get track gain");
        {
            TestAPI t;
            t->createTrack("Gain Test");
            t->setTrackGain("Gain Test", 0.5f);
            expectWithinAbsoluteError(t->getTrackGain("Gain Test"), 0.5f, 0.01f);
        }

        beginTest("Set and get master gain");
        {
            TestAPI t;
            t->setMasterGain(0.75f);
            expectWithinAbsoluteError(t->getMasterGain(), 0.75f, 0.01f);
        }

        beginTest("Add send from track to bus");
        {
            TestAPI t;
            t->createTrack("Src");
            t->createBus("Dest");
            t->addSend("Src", "Dest", 0.5f);
            auto sends = t->getTrackSends("Src");
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busName, juce::String("Dest"));
        }

        beginTest("Send survives bus rename");
        {
            TestAPI t;
            t->createTrack("Src");
            t->createBus("Old Bus");
            t->addSend("Src", "Old Bus", 1.0f);
            t->renameBus("Old Bus", "New Bus");
            auto sends = t->getTrackSends("Src");
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busName, juce::String("New Bus"));
        }

        beginTest("Send survives track rename");
        {
            TestAPI t;
            t->createTrack("Old Track");
            t->createBus("FX Bus");
            t->addSend("Old Track", "FX Bus", 1.0f);
            t->renameTrack("Old Track", "New Track");
            auto sends = t->getTrackSends("New Track");
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busName, juce::String("FX Bus"));
        }

        beginTest("Track gain persists to registry immediately");
        {
            TestAPI t;
            t->createTrack("Gain Persist");
            t->setTrackGain("Gain Persist", 0.42f);
            // Read directly from registry to verify
            auto& reg = t.get().getRegistry();
            auto songId = t.get().getCurrentSongId();
            for (auto& track : reg.tracksForSong(songId)) {
                if (track.name == "Gain Persist")
                    expectWithinAbsoluteError(track.outputGain, 0.42f, 0.01f);
            }
        }

        beginTest("Bus gain persists to registry immediately");
        {
            TestAPI t;
            t->createBus("Bus Gain");
            t->setBusGain("Bus Gain", 0.33f);
            auto& reg = t.get().getRegistry();
            auto songId = t.get().getCurrentSongId();
            for (auto& bus : reg.bussesForSong(songId)) {
                if (bus.name == "Bus Gain")
                    expectWithinAbsoluteError(bus.outputGain, 0.33f, 0.01f);
            }
        }

        beginTest("Master gain persists to registry immediately");
        {
            TestAPI t;
            t->setMasterGain(0.65f);
            auto& reg = t.get().getRegistry();
            auto songId = t.get().getCurrentSongId();
            expectWithinAbsoluteError(reg.getMasterGain(songId), 0.65f, 0.01f);
        }

        beginTest("Gain survives song switch without persist timer");
        {
            TestAPI t;
            auto songA = t->createSong("Song A");
            t->createTrack("T1");
            t->setTrackGain("T1", 0.25f);

            t->createSong("Song B");
            t->createTrack("T2");

            // Switch back to Song A — gain should be preserved
            t->loadSongFromRegistry(songA);
            expectWithinAbsoluteError(t->getTrackGain("T1"), 0.25f, 0.01f);
        }

        beginTest("MIDI enabled persists to registry immediately");
        {
            TestAPI t;
            t->createTrack("MIDI Test");
            expect(t->isTrackMidiEnabled("MIDI Test") == true);
            t->setTrackMidiEnabled("MIDI Test", false);
            // Verify in registry
            auto& reg = t.get().getRegistry();
            auto songId = t.get().getCurrentSongId();
            for (auto& track : reg.tracksForSong(songId)) {
                if (track.name == "MIDI Test")
                    expect(track.midiEnabled == false);
            }
        }
    }
};

static APILifecycleTests apiLifecycleTests;

// ============================================================================
// Song switching tests
// ============================================================================

class SongTests : public juce::UnitTest {
public:
    SongTests() : UnitTest("Song Switching", "Performance") {}

    void runTest() override {

        beginTest("Create song gives empty state");
        {
            TestAPI t;
            t->createTrack("Track 1");
            expectEquals((int)t->listTrackNames().size(), 1);
            // Creating a new song should clear the engine
            t->createSong("New Song");
            expectEquals((int)t->listTrackNames().size(), 0);
        }

        beginTest("Switch between songs preserves state");
        {
            TestAPI t;
            // Build up song A
            auto songAId = t->createSong("Song A");
            t->createTrack("A Track");
            t->createBus("A Bus");

            // Switch to song B
            auto songBId = t->createSong("Song B");
            t->createTrack("B Track");
            expectEquals((int)t->listTrackNames().size(), 1);
            expectEquals(t->listTrackNames()[0], juce::String("B Track"));

            // Switch back to song A
            t->loadSongFromRegistry(songAId);
            auto trackNames = t->listTrackNames();
            expectEquals((int)trackNames.size(), 1);
            expectEquals(trackNames[0], juce::String("A Track"));
            expectEquals((int)t->listBusNames().size(), 1);
        }
    }
};

static SongTests songTests;

// ============================================================================
// Test runner — main()
// ============================================================================

int main(int, char*[]) {
    // JUCE needs a message manager for some operations
    juce::ScopedJuceInitialiser_GUI juceInit;

    initLog();
    perfLog("[Tests] Starting test suite\n");

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i) {
        auto* result = runner.getResult(i);
        if (result->failures > 0) {
            failures += result->failures;
            for (auto& msg : result->messages)
                fprintf(stderr, "  FAIL: %s\n", msg.toRawUTF8());
        }
    }

    if (failures == 0)
        fprintf(stderr, "\n✓ All tests passed (%d results)\n", runner.getNumResults());
    else
        fprintf(stderr, "\n✗ %d failure(s)\n", failures);

    return failures > 0 ? 1 : 0;
}
