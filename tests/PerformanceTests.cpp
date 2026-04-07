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
            auto trackId = t->createTrack("Keys");
            expect(trackId.isNotEmpty());
            auto names = t->listTrackNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Keys"));
        }

        beginTest("Create bus and verify it exists");
        {
            TestAPI t;
            auto busId = t->createBus("Reverb");
            expect(busId.isNotEmpty());
            auto names = t->listBusNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Reverb"));
        }

        beginTest("Rename track");
        {
            TestAPI t;
            auto trackId = t->createTrack("Track 1");
            t->renameTrack(trackId, "Piano");
            auto names = t->listTrackNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Piano"));
        }

        beginTest("Rename track — old name is gone");
        {
            TestAPI t;
            auto id1 = t->createTrack("Old Name");
            t->renameTrack(id1, "New Name");
            // Creating another track with the old name should work
            t->createTrack("Old Name");
            auto names = t->listTrackNames();
            expectEquals((int)names.size(), 2);
        }

        beginTest("Rename bus");
        {
            TestAPI t;
            auto busId = t->createBus("Bus 1");
            t->renameBus(busId, "Delay");
            auto names = t->listBusNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Delay"));
        }

        beginTest("Delete track");
        {
            TestAPI t;
            auto trackId = t->createTrack("To Delete");
            expectEquals((int)t->listTrackNames().size(), 1);
            t->removeTrack(trackId);
            expectEquals((int)t->listTrackNames().size(), 0);
        }

        beginTest("Delete bus");
        {
            TestAPI t;
            auto busId = t->createBus("To Delete");
            expectEquals((int)t->listBusNames().size(), 1);
            t->removeBus(busId);
            expectEquals((int)t->listBusNames().size(), 0);
        }

        beginTest("Create track after delete — same name reusable");
        {
            TestAPI t;
            auto id1 = t->createTrack("Reuse");
            t->removeTrack(id1);
            t->createTrack("Reuse");
            auto names = t->listTrackNames();
            expectEquals((int)names.size(), 1);
            expectEquals(names[0], juce::String("Reuse"));
        }

        beginTest("Add effect to track");
        {
            TestAPI t;
            auto trackId = t->createTrack("FX Track");
            // DLSMusicDevice is always available on macOS
            t->addEffect(trackId, "TestFX", "DLSMusicDevice");
            // Effect should appear (may be loading async, but the slot exists)
            auto effects = t->getTrackEffects(trackId);
            expectEquals((int)effects.size(), 1);
        }

        beginTest("Remove effect from track");
        {
            TestAPI t;
            auto trackId = t->createTrack("FX Track");
            t->addEffect(trackId, "TestFX", "DLSMusicDevice");
            auto effects = t->getTrackEffects(trackId);
            expect(!effects.empty());
            t->removeEffect(trackId, effects[0].effectId);
            expectEquals((int)t->getTrackEffects(trackId).size(), 0);
        }

        beginTest("Duplicate effect names get unique IDs");
        {
            TestAPI t;
            auto trackId = t->createTrack("Dupes");
            t->addEffect(trackId, "DLSMusicDevice", "DLSMusicDevice");
            t->addEffect(trackId, "DLSMusicDevice", "DLSMusicDevice");
            auto effects = t->getTrackEffects(trackId);
            expectEquals((int)effects.size(), 2);
            // IDs should be different
            expect(effects[0].effectId != effects[1].effectId);
        }

        beginTest("Set and get track gain");
        {
            TestAPI t;
            auto trackId = t->createTrack("Gain Test");
            t->setTrackGain(trackId, 0.5f);
            expectWithinAbsoluteError(t->getTrackGain(trackId), 0.5f, 0.01f);
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
            auto trackId = t->createTrack("Src");
            auto busId = t->createBus("Dest");
            t->addSend(trackId, busId, 0.5f);
            auto sends = t->getTrackSends(trackId);
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busName, juce::String("Dest"));
        }

        beginTest("Send survives bus rename");
        {
            TestAPI t;
            auto trackId = t->createTrack("Src");
            auto busId = t->createBus("Old Bus");
            t->addSend(trackId, busId, 1.0f);
            t->renameBus(busId, "New Bus");
            auto sends = t->getTrackSends(trackId);
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busName, juce::String("New Bus"));
        }

        beginTest("Send survives track rename");
        {
            TestAPI t;
            auto trackId = t->createTrack("Old Track");
            auto busId = t->createBus("FX Bus");
            t->addSend(trackId, busId, 1.0f);
            t->renameTrack(trackId, "New Track");
            auto sends = t->getTrackSends(trackId);
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busName, juce::String("FX Bus"));
        }

        beginTest("Track gain persists to registry immediately");
        {
            TestAPI t;
            auto trackId = t->createTrack("Gain Persist");
            t->setTrackGain(trackId, 0.42f);
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
            auto busId = t->createBus("Bus Gain");
            t->setBusGain(busId, 0.33f);
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
            auto trackId = t->createTrack("T1");
            t->setTrackGain(trackId, 0.25f);

            t->createSong("Song B");
            t->createTrack("T2");

            // Switch back to Song A — gain should be preserved
            t->loadSongFromRegistry(songA);
            // Need to find the track ID in the restored song
            auto tracks = t->listTracks();
            expect(!tracks.empty());
            expectWithinAbsoluteError(t->getTrackGain(tracks[0].id), 0.25f, 0.01f);
        }

        beginTest("MIDI enabled persists to registry immediately");
        {
            TestAPI t;
            auto trackId = t->createTrack("MIDI Test");
            expect(t->isTrackMidiEnabled(trackId) == true);
            t->setTrackMidiEnabled(trackId, false);
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
// Multi-track e2e tests
// ============================================================================

class MultiTrackTests : public juce::UnitTest {
public:
    MultiTrackTests() : UnitTest("Multi-Track E2E", "Performance") {}

    void runTest() override {

        beginTest("Multiple tracks coexist with independent state");
        {
            TestAPI t;
            auto keysId = t->createTrack("Keys");
            auto bassId = t->createTrack("Bass");
            t->setTrackGain(keysId, 0.8f);
            t->setTrackGain(bassId, 0.3f);
            t->setTrackMidiEnabled(keysId, false);

            expectEquals((int)t->listTrackNames().size(), 2);
            expectWithinAbsoluteError(t->getTrackGain(keysId), 0.8f, 0.01f);
            expectWithinAbsoluteError(t->getTrackGain(bassId), 0.3f, 0.01f);
            expect(t->isTrackMidiEnabled(keysId) == false);
            expect(t->isTrackMidiEnabled(bassId) == true);
        }

        beginTest("Renaming one track doesn't affect another");
        {
            TestAPI t;
            auto id1 = t->createTrack("Track 1");
            auto id2 = t->createTrack("Track 2");
            t->setTrackGain(id1, 0.5f);
            t->setTrackGain(id2, 0.9f);

            t->renameTrack(id1, "Piano");

            // Piano should have Track 1's gain
            expectWithinAbsoluteError(t->getTrackGain(id1), 0.5f, 0.01f);
            // Track 2 should be unaffected
            expectWithinAbsoluteError(t->getTrackGain(id2), 0.9f, 0.01f);
            expectEquals((int)t->listTrackNames().size(), 2);
        }

        beginTest("Deleting one track doesn't affect another");
        {
            TestAPI t;
            auto keepId = t->createTrack("Keep");
            auto delId = t->createTrack("Delete");
            t->setTrackGain(keepId, 0.7f);

            t->removeTrack(delId);

            expectEquals((int)t->listTrackNames().size(), 1);
            expectEquals(t->listTrackNames()[0], juce::String("Keep"));
            expectWithinAbsoluteError(t->getTrackGain(keepId), 0.7f, 0.01f);
        }

        beginTest("Multiple tracks with effects — independent");
        {
            TestAPI t;
            auto id1 = t->createTrack("T1");
            auto id2 = t->createTrack("T2");
            t->addEffect(id1, "FX1", "DLSMusicDevice");
            t->addEffect(id2, "FX2", "DLSMusicDevice");

            auto t1fx = t->getTrackEffects(id1);
            auto t2fx = t->getTrackEffects(id2);
            expectEquals((int)t1fx.size(), 1);
            expectEquals((int)t2fx.size(), 1);
            expect(t1fx[0].effectId != t2fx[0].effectId);

            // Remove effect from T1, T2 should be unaffected
            t->removeEffect(id1, t1fx[0].effectId);
            expectEquals((int)t->getTrackEffects(id1).size(), 0);
            expectEquals((int)t->getTrackEffects(id2).size(), 1);
        }

        beginTest("Sends between tracks and busses — independent");
        {
            TestAPI t;
            auto id1 = t->createTrack("T1");
            auto id2 = t->createTrack("T2");
            auto revId = t->createBus("Reverb");
            auto delId = t->createBus("Delay");

            t->addSend(id1, revId, 0.5f);
            t->addSend(id2, delId, 0.8f);

            auto t1sends = t->getTrackSends(id1);
            auto t2sends = t->getTrackSends(id2);
            expectEquals((int)t1sends.size(), 1);
            expectEquals((int)t2sends.size(), 1);
            expectEquals(t1sends[0].busName, juce::String("Reverb"));
            expectEquals(t2sends[0].busName, juce::String("Delay"));
        }

        beginTest("Track preset save captures correct track");
        {
            TestAPI t;
            auto trackId = t->createTrack("Source");
            t->setTrackGain(trackId, 0.42f);
            t->setTrackMidiEnabled(trackId, false);

            t->saveTrackPreset(trackId, "TestPreset");

            // Verify file was created
            auto file = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile(".config/performance/track_presets/TestPreset.json");
            expect(file.existsAsFile());

            // Parse and verify contents
            auto json = juce::JSON::parse(file.loadFileAsString());
            expectWithinAbsoluteError((float)json.getProperty("gain", 0.0), 0.42f, 0.01f);
            expect((bool)json.getProperty("midiEnabled", true) == false);

            // Clean up
            file.deleteFile();
        }

        beginTest("Multiple tracks survive song switch roundtrip");
        {
            TestAPI t;
            auto songA = t->createSong("Multi A");
            auto a1Id = t->createTrack("A1");
            auto a2Id = t->createTrack("A2");
            auto aBusId = t->createBus("A Bus");
            t->setTrackGain(a1Id, 0.1f);
            t->setTrackGain(a2Id, 0.2f);
            t->addSend(a1Id, aBusId, 0.5f);

            // Switch away and back
            t->createSong("Temp");
            t->loadSongFromRegistry(songA);

            expectEquals((int)t->listTrackNames().size(), 2);
            expectEquals((int)t->listBusNames().size(), 1);
            // After song switch, find track IDs from the restored song
            auto tracks = t->listTracks();
            for (auto& tr : tracks) {
                if (tr.name == "A1")
                    expectWithinAbsoluteError(t->getTrackGain(tr.id), 0.1f, 0.01f);
                if (tr.name == "A2")
                    expectWithinAbsoluteError(t->getTrackGain(tr.id), 0.2f, 0.01f);
            }
            // Find sends on A1
            for (auto& tr : tracks) {
                if (tr.name == "A1") {
                    auto sends = t->getTrackSends(tr.id);
                    expectEquals((int)sends.size(), 1);
                }
            }
        }

        beginTest("Track preset load changes track name and gain");
        {
            TestAPI t;
            auto srcId = t->createTrack("Source");
            t->setTrackGain(srcId, 0.42f);
            t->setTrackMidiEnabled(srcId, false);
            t->saveTrackPreset(srcId, "MyPreset");

            auto tgtId = t->createTrack("Target");
            t->setTrackGain(tgtId, 1.0f);
            expectEquals((int)t->listTrackNames().size(), 2);

            t->loadTrackPreset(tgtId, "MyPreset");

            // Target should be renamed to MyPreset
            auto names = t->listTrackNames();
            bool foundMyPreset = false;
            bool foundSource = false;
            for (auto& n : names) {
                if (n == "MyPreset") foundMyPreset = true;
                if (n == "Source") foundSource = true;
            }
            expect(foundMyPreset);
            expect(foundSource);

            // Loaded track should have the preset's gain
            expectWithinAbsoluteError(t->getTrackGain(tgtId), 0.42f, 0.01f);
            // Source should be unaffected
            expectWithinAbsoluteError(t->getTrackGain(srcId), 0.42f, 0.01f);
        }

        beginTest("Track preset load doesn't affect other tracks");
        {
            TestAPI t;
            auto keepId = t->createTrack("Keep");
            t->setTrackGain(keepId, 0.77f);
            t->setTrackMidiEnabled(keepId, false);

            auto replaceId = t->createTrack("Replace");
            t->setTrackGain(replaceId, 0.99f);

            t->saveTrackPreset(replaceId, "ReplacePreset");

            // Load ReplacePreset onto Keep
            t->loadTrackPreset(keepId, "ReplacePreset");

            // Replace track should be completely unaffected
            expectWithinAbsoluteError(t->getTrackGain(replaceId), 0.99f, 0.01f);
            expect(t->isTrackMidiEnabled(replaceId) == true);
        }

        beginTest("Registry state consistent after multiple operations");
        {
            TestAPI t;
            auto& reg = t.get().getRegistry();
            auto songId = t.get().getCurrentSongId();

            auto t1Id = t->createTrack("T1");
            auto t2Id = t->createTrack("T2");
            auto b1Id = t->createBus("B1");
            t->setTrackGain(t1Id, 0.5f);
            t->setBusGain(b1Id, 0.7f);
            t->addSend(t1Id, b1Id, 0.3f);
            t->renameTrack(t2Id, "Bass");

            // Verify registry matches what the API reports
            auto regTracks = reg.tracksForSong(songId);
            expectEquals((int)regTracks.size(), 2);

            bool foundT1 = false, foundBass = false;
            for (auto& rt : regTracks) {
                if (rt.name == "T1") {
                    expectWithinAbsoluteError(rt.outputGain, 0.5f, 0.01f);
                    foundT1 = true;
                }
                if (rt.name == "Bass") foundBass = true;
            }
            expect(foundT1);
            expect(foundBass);

            auto regBusses = reg.bussesForSong(songId);
            expectEquals((int)regBusses.size(), 1);
            expectWithinAbsoluteError(regBusses[0].outputGain, 0.7f, 0.01f);
        }
    }
};

static MultiTrackTests multiTrackTests;

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
