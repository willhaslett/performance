#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "api/PerformanceAPI.h"
#include "api/StateAPI.h"
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

        beginTest("Remove one of two duplicate effects by ID");
        {
            TestAPI t;
            auto trackId = t->createTrack("Dupes2");
            t->addEffect(trackId, "DLSMusicDevice", "DLSMusicDevice");
            t->addEffect(trackId, "DLSMusicDevice", "DLSMusicDevice");
            auto effects = t->getTrackEffects(trackId);
            expectEquals((int)effects.size(), 2);

            // Remove the first one by ID
            t->removeEffect(trackId, effects[0].effectId);
            auto remaining = t->getTrackEffects(trackId);
            expectEquals((int)remaining.size(), 1);
            // The remaining one should be the second one
            expectEquals(remaining[0].effectId, effects[1].effectId);
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

        beginTest("Instrument assignment survives song switch");
        {
            TestAPI t;
            auto songA = t->createSong("Inst Song");
            auto trackId = t->createTrack("Keys");
            t->addInstrument(trackId, "DLSMusicDevice");

            // Verify instrument is assigned in registry
            auto& reg = t.get().getRegistry();
            auto tracks = reg.tracksForSong(t.get().getCurrentSongId());
            expect(!tracks.empty());
            expect(!tracks[0].pluginId.empty());

            // Verify registry has the plugin assignment (SSOT check)
            auto regTracks = reg.tracksForSong(t.get().getCurrentSongId());
            expect(!regTracks.empty());
            expect(!regTracks[0].pluginId.empty());

            // Switch to another song and back
            t->createSong("Other");
            t->loadSongFromRegistry(songA);

            // Verify registry still has the instrument after switch
            auto restoredTracks = t->listTracks();
            expectEquals((int)restoredTracks.size(), 1);
            auto restoredRegTracks = reg.tracksForSong(songA);
            expect(!restoredRegTracks.empty());
            expect(!restoredRegTracks[0].pluginId.empty());
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
// StateAPI tests (in-memory state store — no engine, no SQLite)
// ============================================================================

class StateAPITests : public juce::UnitTest {
public:
    StateAPITests() : UnitTest("StateAPI", "Performance") {}

    void runTest() override {

        beginTest("Create song and set current");
        {
            StateAPI s;
            auto id = s.createSong("Test");
            expect(!id.empty());
            s.setCurrentSong(id);
            auto* song = s.currentSong();
            expect(song != nullptr);
            expectEquals(song->name, std::string("Test"));
        }

        beginTest("Create and list tracks");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto t1 = s.createTrack("Keys");
            auto t2 = s.createTrack("Bass");
            auto tracks = s.listTracks();
            expectEquals((int)tracks.size(), 2);
            expectEquals(tracks[0].name, std::string("Keys"));
            expectEquals(tracks[1].name, std::string("Bass"));
            expect(t1 != t2);
        }

        beginTest("Rename track");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createTrack("Old");
            s.renameTrack(id, "New");
            expectEquals(s.listTracks()[0].name, std::string("New"));
        }

        beginTest("Remove track");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createTrack("Del");
            s.createTrack("Keep");
            s.removeTrack(id);
            auto tracks = s.listTracks();
            expectEquals((int)tracks.size(), 1);
            expectEquals(tracks[0].name, std::string("Keep"));
        }

        beginTest("Track gain");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createTrack("T");
            expectWithinAbsoluteError(s.getTrackGain(id), 1.0f, 0.001f);
            s.setTrackGain(id, 0.42f);
            expectWithinAbsoluteError(s.getTrackGain(id), 0.42f, 0.001f);
        }

        beginTest("Track MIDI enabled");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createTrack("T");
            expect(s.isTrackMidiEnabled(id) == true);
            s.setTrackMidiEnabled(id, false);
            expect(s.isTrackMidiEnabled(id) == false);
        }

        beginTest("Track plugin assignment");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto pluginId = s.registerPlugin("DLS", "Apple", "au", true);
            auto trackId = s.createTrack("T");
            s.setTrackPlugin(trackId, pluginId);
            expectEquals(s.getTrackPluginName(trackId), std::string("DLS"));
            s.clearTrackPlugin(trackId);
            expect(s.getTrackPluginName(trackId).empty());
        }

        beginTest("Track instrument load status");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createTrack("T");
            auto* track = s.findTrack(id);
            expect(track->instrumentLoadStatus == LoadStatus::None);
            s.setTrackInstrumentLoadStatus(id, LoadStatus::Pending);
            expect(track->instrumentLoadStatus == LoadStatus::Pending);
            s.setTrackInstrumentLoadStatus(id, LoadStatus::Loaded);
            expect(track->instrumentLoadStatus == LoadStatus::Loaded);
        }

        beginTest("Create and list busses");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto b1 = s.createBus("Reverb");
            auto b2 = s.createBus("Delay");
            auto busses = s.listBusses();
            expectEquals((int)busses.size(), 2);
            expect(b1 != b2);
        }

        beginTest("Bus gain");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createBus("B");
            s.setBusGain(id, 0.33f);
            expectWithinAbsoluteError(s.getBusGain(id), 0.33f, 0.001f);
        }

        beginTest("Rename and remove bus");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createBus("Old");
            s.renameBus(id, "New");
            expectEquals(s.listBusses()[0].name, std::string("New"));
            s.removeBus(id);
            expect(s.listBusses().empty());
        }

        beginTest("Master gain");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            expectWithinAbsoluteError(s.getMasterGain(), 1.0f, 0.001f);
            s.setMasterGain(0.75f);
            expectWithinAbsoluteError(s.getMasterGain(), 0.75f, 0.001f);
        }

        beginTest("Add effect to track");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto pluginId = s.registerPlugin("Raum", "NI", "fx", false);
            auto trackId = s.createTrack("T");
            auto fxId = s.addEffect(trackId, "Raum", pluginId);
            expect(!fxId.empty());
            auto effects = s.getTrackEffects(trackId);
            expectEquals((int)effects.size(), 1);
            expectEquals(effects[0].pluginName, std::string("Raum"));
        }

        beginTest("Add effect to bus");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto pluginId = s.registerPlugin("Delay", "NI", "fx", false);
            auto busId = s.createBus("FX");
            auto fxId = s.addEffect(busId, "Delay", pluginId);
            auto effects = s.getBusEffects(busId);
            expectEquals((int)effects.size(), 1);
        }

        beginTest("Add master effect");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto pluginId = s.registerPlugin("Limiter", "Apple", "fx", false);
            auto fxId = s.addEffect(songId, "Limiter", pluginId);
            auto effects = s.getMasterEffects();
            expectEquals((int)effects.size(), 1);
            expectEquals(effects[0].pluginName, std::string("Limiter"));
        }

        beginTest("Remove effect");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto pluginId = s.registerPlugin("FX", "M", "fx", false);
            auto trackId = s.createTrack("T");
            auto fx1 = s.addEffect(trackId, "FX", pluginId);
            auto fx2 = s.addEffect(trackId, "FX", pluginId);
            expectEquals((int)s.getTrackEffects(trackId).size(), 2);
            s.removeEffect(fx1);
            auto remaining = s.getTrackEffects(trackId);
            expectEquals((int)remaining.size(), 1);
            expectEquals(remaining[0].effectId, fx2);
        }

        beginTest("Effect load status");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto pluginId = s.registerPlugin("FX", "M", "fx", false);
            auto trackId = s.createTrack("T");
            auto fxId = s.addEffect(trackId, "FX", pluginId);
            auto* fx = s.findEffect(fxId);
            expect(fx->loadStatus == LoadStatus::None);
            s.setEffectLoadStatus(fxId, LoadStatus::Loaded);
            expect(fx->loadStatus == LoadStatus::Loaded);
        }

        beginTest("Add and query sends");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto trackId = s.createTrack("T");
            auto busId = s.createBus("Reverb");
            auto sendId = s.addSend(trackId, busId, 0.5f);
            expect(!sendId.empty());
            auto sends = s.getTrackSends(trackId);
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busName, std::string("Reverb"));
            expectWithinAbsoluteError(sends[0].gain, 0.5f, 0.001f);
        }

        beginTest("Send gain update");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto trackId = s.createTrack("T");
            auto busId = s.createBus("B");
            auto sendId = s.addSend(trackId, busId, 1.0f);
            s.setSendGain(sendId, 0.3f);
            auto sends = s.getTrackSends(trackId);
            expectWithinAbsoluteError(sends[0].gain, 0.3f, 0.001f);
        }

        beginTest("Remove send");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto trackId = s.createTrack("T");
            auto b1 = s.createBus("B1");
            auto b2 = s.createBus("B2");
            auto s1 = s.addSend(trackId, b1);
            auto s2 = s.addSend(trackId, b2);
            s.removeSend(s1);
            auto sends = s.getTrackSends(trackId);
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busId, b2);
        }

        beginTest("Send survives bus rename");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto trackId = s.createTrack("T");
            auto busId = s.createBus("Old");
            s.addSend(trackId, busId);
            s.renameBus(busId, "New");
            auto sends = s.getTrackSends(trackId);
            expectEquals(sends[0].busName, std::string("New"));
        }

        beginTest("Multiple tracks independent state");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto t1 = s.createTrack("Keys");
            auto t2 = s.createTrack("Bass");
            s.setTrackGain(t1, 0.8f);
            s.setTrackGain(t2, 0.3f);
            s.setTrackMidiEnabled(t1, false);

            expectWithinAbsoluteError(s.getTrackGain(t1), 0.8f, 0.01f);
            expectWithinAbsoluteError(s.getTrackGain(t2), 0.3f, 0.01f);
            expect(s.isTrackMidiEnabled(t1) == false);
            expect(s.isTrackMidiEnabled(t2) == true);
        }

        beginTest("Delete song cascades");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            s.createTrack("T");
            s.createBus("B");
            expectEquals((int)s.listTracks().size(), 1);
            s.deleteSong(songId);
            expect(s.currentSong() == nullptr);
            expect(s.listTracks().empty());
        }

        beginTest("Switch songs preserves state");
        {
            StateAPI s;
            auto a = s.createSong("A");
            s.setCurrentSong(a);
            s.createTrack("A Track");
            s.setTrackGain(s.listTracks()[0].id, 0.25f);

            auto b = s.createSong("B");
            s.setCurrentSong(b);
            s.createTrack("B Track");
            expectEquals((int)s.listTracks().size(), 1);
            expectEquals(s.listTracks()[0].name, std::string("B Track"));

            // Switch back
            s.setCurrentSong(a);
            expectEquals((int)s.listTracks().size(), 1);
            expectEquals(s.listTracks()[0].name, std::string("A Track"));
            expectWithinAbsoluteError(s.getTrackGain(s.listTracks()[0].id), 0.25f, 0.01f);
        }

        beginTest("Plugin catalog");
        {
            StateAPI s;
            auto id1 = s.registerPlugin("Synth", "Mfg", "au", true);
            auto id2 = s.registerPlugin("Delay", "Mfg", "au", false);
            // Duplicate registration returns same ID
            auto id1dup = s.registerPlugin("Synth", "Mfg", "au", true);
            expectEquals(id1, id1dup);
            expectEquals((int)s.allPlugins().size(), 2);
            expect(s.findPluginByName("Synth")->isInstrument == true);
            expect(s.findPluginByName("Delay")->isInstrument == false);
            expect(s.findPluginById(id1) != nullptr);
        }

        beginTest("Preset catalog");
        {
            StateAPI s;
            auto pluginId = s.registerPlugin("Synth", "Mfg", "au", true);
            auto presetId = s.createPreset(pluginId, "Warm", "/tmp/warm.state");
            expect(!presetId.empty());
            auto* preset = s.findPreset(pluginId, "Warm");
            expect(preset != nullptr);
            expectEquals(preset->name, std::string("Warm"));
            auto presets = s.presetsForPlugin(pluginId);
            expectEquals((int)presets.size(), 1);
        }

        beginTest("Action catalog");
        {
            StateAPI s;
            auto id = s.registerAction("fadeOut", "Fade out", "[{\"name\":\"track\"}]");
            expect(!id.empty());
            expect(s.findActionByName("fadeOut") != nullptr);
            expect(s.findActionById(id) != nullptr);
            expectEquals((int)s.allActions().size(), 1);
        }

        beginTest("Bindings");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto actionId = s.registerAction("test", "Test");
            auto bindId = s.addBinding("cc", 1, 42, actionId, "[\"arg\"]", "Test binding");
            expect(!bindId.empty());
            auto bindings = s.bindingsForCurrentSong();
            expectEquals((int)bindings.size(), 1);
            expectEquals(bindings[0].controlType, std::string("cc"));
            expectEquals(bindings[0].number, 42);
            s.removeBinding(bindId);
            expect(s.bindingsForCurrentSong().empty());
        }

        beginTest("Config key-value store");
        {
            StateAPI s;
            s.setConfig("key1", "value1");
            expectEquals(s.getConfig("key1"), std::string("value1"));
            expectEquals(s.getConfig("missing", "default"), std::string("default"));
        }

        beginTest("Name resolution");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto t1 = s.createTrack("Keys");
            auto b1 = s.createBus("Reverb");
            expectEquals(s.findTrackIdByName("Keys"), t1);
            expectEquals(s.findBusIdByName("Reverb"), b1);
            expect(s.findTrackIdByName("Missing").empty());
        }

        beginTest("Selection");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto t1 = s.createTrack("T1");
            auto t2 = s.createTrack("T2");
            auto b1 = s.createBus("B1");

            s.selectTrack(t1);
            expectEquals((int)s.selectedTrackIds().size(), 1);
            expectEquals(s.selectedTrackIds()[0], t1);

            // Add to selection
            s.selectTrack(t2, true);
            expectEquals((int)s.selectedTrackIds().size(), 2);

            // Select bus clears tracks (no addToSelection)
            s.selectBus(b1);
            expect(s.selectedTrackIds().empty());
            expectEquals((int)s.selectedBusIds().size(), 1);

            // Clear all
            s.clearSelection();
            expect(s.selectedTrackIds().empty());
            expect(s.selectedBusIds().empty());
        }

        beginTest("Dirty tracking");
        {
            StateAPI s;
            expect(!s.isDirty());
            s.createSong("S");
            expect(s.isDirty());
            s.clearDirty();
            expect(!s.isDirty());
            s.setCurrentSong(s.allSongs()[0].id);
            s.createTrack("T");
            expect(s.isDirty());
        }

        beginTest("Events fire on mutations");
        {
            StateAPI s;
            std::vector<StateEvent> received;
            s.events().subscribe([&](const StateEvent& e) { received.push_back(e); });

            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto trackId = s.createTrack("T");
            s.setTrackGain(trackId, 0.5f);
            s.removeTrack(trackId);

            // Expect: Song Created, Config Updated (setCurrentSong), Track Created, Track Updated, Track Deleted
            expect(received.size() >= 5);

            // Check first event is Song Created
            expect(received[0].action == StateEvent::Created);
            expect(received[0].entity == StateEvent::Song);

            // Find Track Created
            bool foundTrackCreated = false;
            bool foundTrackDeleted = false;
            for (auto& e : received) {
                if (e.entity == StateEvent::Track && e.action == StateEvent::Created)
                    foundTrackCreated = true;
                if (e.entity == StateEvent::Track && e.action == StateEvent::Deleted)
                    foundTrackDeleted = true;
            }
            expect(foundTrackCreated);
            expect(foundTrackDeleted);
        }

        beginTest("Master output ID is song ID");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            expectEquals(s.getMasterOutputId(), songId);
        }
    }
};

static StateAPITests stateAPITests;

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
