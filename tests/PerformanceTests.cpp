#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "persistence/PersistenceLayer.h"
#include "engine/AudioEngineInterface.h"
#include "engine/EngineSync.h"
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

// Full coordinator with a temp database for isolated testing.
class TestCoordinator {
public:
    TestCoordinator() {
        coord.initialise(db.path());
        coord.createSong("Test Session");
    }
    ~TestCoordinator() { coord.shutdown(); }

    PerformanceCoordinator& get() { return coord; }
    StateAPI& state() { return coord.state(); }
    EngineAPI& engine() { return coord.engine(); }
    PerformanceCoordinator* operator->() { return &coord; }

private:
    TempDB db;
    PerformanceCoordinator coord;
};

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
            s.addEffect(busId, "Delay", pluginId);
            expectEquals((int)s.getBusEffects(busId).size(), 1);
        }

        beginTest("Add master effect");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto pluginId = s.registerPlugin("Limiter", "Apple", "fx", false);
            s.addEffect(songId, "Limiter", pluginId);
            expectEquals((int)s.getMasterEffects().size(), 1);
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
            expectWithinAbsoluteError(s.getTrackSends(trackId)[0].gain, 0.3f, 0.001f);
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
            expectEquals(s.getTrackSends(trackId)[0].busName, std::string("New"));
        }

        beginTest("setSendGainByBus");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto trackId = s.createTrack("T");
            auto b1 = s.createBus("B1");
            auto b2 = s.createBus("B2");
            s.addSend(trackId, b1, 1.0f);
            s.addSend(trackId, b2, 1.0f);
            s.setSendGainByBus(trackId, b1, 0.3f);
            auto sends = s.getTrackSends(trackId);
            for (auto& send : sends) {
                if (send.busId == b1) expectWithinAbsoluteError(send.gain, 0.3f, 0.001f);
                if (send.busId == b2) expectWithinAbsoluteError(send.gain, 1.0f, 0.001f);
            }
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

            s.setCurrentSong(a);
            expectEquals((int)s.listTracks().size(), 1);
            expectEquals(s.listTracks()[0].name, std::string("A Track"));
            expectWithinAbsoluteError(s.getTrackGain(s.listTracks()[0].id), 0.25f, 0.01f);
        }

        beginTest("Plugin catalog");
        {
            StateAPI s;
            auto id1 = s.registerPlugin("Synth", "Mfg", "au", true);
            auto id2 = s.registerPlugin("Delay", "Mfg", "au2", false);
            auto id1dup = s.registerPlugin("Synth", "Mfg", "au", true);
            expectEquals(id1, id1dup);
            expectEquals((int)s.allPlugins().size(), 2);
            expect(s.findPluginByName("Synth")->isInstrument == true);
            expect(s.findPluginByName("Delay")->isInstrument == false);
        }

        beginTest("Preset catalog");
        {
            StateAPI s;
            auto pluginId = s.registerPlugin("Synth", "Mfg", "au", true);
            auto presetId = s.createPreset(pluginId, "Warm", "/tmp/warm.state");
            expect(!presetId.empty());
            expect(s.findPreset(pluginId, "Warm") != nullptr);
            expectEquals((int)s.presetsForPlugin(pluginId).size(), 1);
        }

        beginTest("Action catalog");
        {
            StateAPI s;
            auto id = s.registerAction("fadeOut", "Fade out", "[{\"name\":\"track\"}]");
            expect(!id.empty());
            expect(s.findActionByName("fadeOut") != nullptr);
            expectEquals((int)s.allActions().size(), 1);
        }

        beginTest("Song-scoped bindings");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto actionId = s.registerAction("test", "Test");
            auto bindId = s.addBinding(songId, "cc", 1, 42, actionId, "[\"arg\"]", "Test");
            expect(!bindId.empty());
            auto bindings = s.bindingsForSong(songId);
            expectEquals((int)bindings.size(), 1);
            expect(!bindings[0].songId.empty());
            s.removeBinding(bindId);
            expect(s.bindingsForSong(songId).empty());
        }

        beginTest("Global bindings");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto actionId = s.registerAction("masterVol", "Master Volume");
            auto bindId = s.addGlobalBinding("cc", 1, 7, actionId);
            expectEquals((int)s.globalBindings().size(), 1);
            expect(s.globalBindings()[0].songId.empty());
            s.deleteSong(songId);
            expectEquals((int)s.globalBindings().size(), 1);
            s.removeBinding(bindId);
            expect(s.globalBindings().empty());
        }

        beginTest("Effective bindings — song overrides global");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto a1 = s.registerAction("g", "Global");
            auto a2 = s.registerAction("s", "Song");
            s.addGlobalBinding("cc", 1, 7, a1);
            s.addBinding(songId, "cc", 1, 7, a2);
            s.addGlobalBinding("cc", 1, 10, a1);
            auto eff = s.effectiveBindings();
            expectEquals((int)eff.size(), 2);
            for (auto& b : eff) {
                if (b.number == 7) expectEquals(b.actionId, a2);
                if (b.number == 10) expectEquals(b.actionId, a1);
            }
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
            s.selectTrack(t2, true);
            expectEquals((int)s.selectedTrackIds().size(), 2);
            s.selectBus(b1);
            expect(s.selectedTrackIds().empty());
            expectEquals((int)s.selectedBusIds().size(), 1);
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
            expect(received.size() >= 5);
            bool foundTrackCreated = false, foundTrackDeleted = false;
            for (auto& e : received) {
                if (e.entity == StateEvent::Track && e.action == StateEvent::Created) foundTrackCreated = true;
                if (e.entity == StateEvent::Track && e.action == StateEvent::Deleted) foundTrackDeleted = true;
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
// PersistenceLayer round-trip tests
// ============================================================================

class PersistenceTests : public juce::UnitTest {
public:
    PersistenceTests() : UnitTest("Persistence", "Performance") {}

    void runTest() override {

        beginTest("Save and load round-trip");
        {
            TempDB db;

            StateAPI original;
            auto pluginId = original.registerPlugin("DLS", "Apple", "au-id", true);
            auto fxPluginId = original.registerPlugin("AUDelay", "Apple", "au-delay", false);
            original.createPreset(pluginId, "Warm", "/tmp/warm.state", PresetKind::Instrument);
            original.registerAction("fadeOut", "Fade out", "[{\"name\":\"track\"}]");

            auto songId = original.createSong("My Song");
            original.setCurrentSong(songId);
            original.setMasterGain(0.8f);

            auto t1 = original.createTrack("Keys");
            original.setTrackGain(t1, 0.6f);
            original.setTrackMidiEnabled(t1, false);
            original.setTrackPlugin(t1, pluginId);

            auto t2 = original.createTrack("Bass");
            original.setTrackGain(t2, 0.4f);

            auto busId = original.createBus("Reverb");
            original.setBusGain(busId, 0.7f);

            original.addEffect(t1, "Delay", fxPluginId);
            original.addEffect(busId, "Delay2", fxPluginId);
            original.addEffect(songId, "MasterFX", fxPluginId);

            original.addSend(t1, busId, 0.5f);

            auto actionId = original.findActionByName("fadeOut")->id;
            original.addBinding(songId, "cc", 1, 42, actionId, "[\"Keys\"]", "Fade keys");
            original.addGlobalBinding("cc", 1, 7, actionId, "[]", "Master vol");

            {
                PersistenceLayer p;
                p.open(db.path().toStdString());
                p.saveFrom(original);
            }

            StateAPI loaded;
            {
                PersistenceLayer p;
                p.open(db.path().toStdString());
                p.loadInto(loaded);
            }

            // Catalog
            expectEquals((int)loaded.allPlugins().size(), 2);
            expect(loaded.findPluginByName("DLS")->isInstrument == true);

            // Song
            expectEquals((int)loaded.allSongs().size(), 1);
            auto* song = loaded.currentSong();
            expect(song != nullptr);
            expectWithinAbsoluteError(song->masterGain, 0.8f, 0.001f);

            // Tracks
            auto tracks = loaded.listTracks();
            expectEquals((int)tracks.size(), 2);
            const TrackState* keys = nullptr;
            for (auto& ti : tracks) {
                auto* t = loaded.findTrack(ti.id);
                if (t->name == "Keys") keys = t;
            }
            expect(keys != nullptr);
            expectWithinAbsoluteError(keys->outputGain, 0.6f, 0.001f);
            expect(keys->midiEnabled == false);
            expect(!keys->pluginId.empty());

            // Bus
            expectEquals((int)loaded.listBusses().size(), 1);
            expectWithinAbsoluteError(loaded.getBusGain(loaded.listBusses()[0].id), 0.7f, 0.001f);

            // Effects
            expectEquals((int)loaded.getTrackEffects(keys->id).size(), 1);
            expectEquals((int)loaded.getBusEffects(loaded.listBusses()[0].id).size(), 1);
            expectEquals((int)loaded.getMasterEffects().size(), 1);

            // Sends
            auto sends = loaded.getTrackSends(keys->id);
            expectEquals((int)sends.size(), 1);
            expectWithinAbsoluteError(sends[0].gain, 0.5f, 0.001f);

            // Bindings
            expectEquals((int)loaded.bindingsForSong(song->id).size(), 1);
            expectEquals((int)loaded.globalBindings().size(), 1);

            expect(!loaded.isDirty());
        }

        beginTest("Multiple songs round-trip");
        {
            TempDB db;
            StateAPI original;
            original.registerPlugin("Synth", "Mfg", "fmt", true);
            auto s1 = original.createSong("Song A");
            original.setCurrentSong(s1);
            original.createTrack("A Track");
            auto s2 = original.createSong("Song B");
            original.setCurrentSong(s2);
            original.createTrack("B Track");
            original.setConfig("current_song_id", s2);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            expectEquals((int)loaded.allSongs().size(), 2);
            auto* songA = loaded.allSongs()[0].name == "Song A" ? &loaded.allSongs()[0] : &loaded.allSongs()[1];
            loaded.setCurrentSong(songA->id);
            expectEquals((int)loaded.listTracks().size(), 1);
            expectEquals(loaded.listTracks()[0].name, std::string("A Track"));
        }

        beginTest("Empty database creates clean state");
        {
            TempDB db;
            StateAPI state;
            PersistenceLayer p;
            p.open(db.path().toStdString());
            p.loadInto(state);
            expect(state.allSongs().empty());
            expect(!state.isDirty());
        }

        beginTest("Processor state round-trip");
        {
            TempDB db;

            StateAPI original;
            auto pluginId = original.registerPlugin("Synth", "Apple", "au", true);
            auto fxPluginId = original.registerPlugin("FX", "Apple", "fx", false);
            auto songId = original.createSong("S");
            original.setCurrentSong(songId);
            auto trackId = original.createTrack("T");
            original.setTrackPlugin(trackId, pluginId);
            auto fxId = original.addEffect(trackId, "FX", fxPluginId);

            // Simulate captured processor state
            auto* track = original.findTrack(trackId);
            track->processorState = "dGVzdCBibG9i";  // base64 of "test blob"
            track->processorStateHash = "abc123hash";

            auto* fx = const_cast<EffectState*>(original.findEffect(fxId));
            fx->processorState = "ZWZmZWN0IGJsb2I=";  // base64 of "effect blob"
            fx->processorStateHash = "def456hash";

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* loadedTrack = loaded.findTrack(loaded.listTracks()[0].id);
            expect(loadedTrack != nullptr);
            expectEquals(loadedTrack->processorState, std::string("dGVzdCBibG9i"));
            expectEquals(loadedTrack->processorStateHash, std::string("abc123hash"));

            auto loadedEffects = loaded.getTrackEffects(loadedTrack->id);
            expectEquals((int)loadedEffects.size(), 1);
            auto* loadedFx = loaded.findEffect(loadedEffects[0].effectId);
            expect(loadedFx != nullptr);
            expectEquals(loadedFx->processorState, std::string("ZWZmZWN0IGJsb2I="));
            expectEquals(loadedFx->processorStateHash, std::string("def456hash"));
        }

        beginTest("isInstrument and PresetKind round-trip");
        {
            TempDB db;

            StateAPI original;
            auto instId = original.registerPlugin("Synth", "Mfg", "au", true);
            auto fxId = original.registerPlugin("Delay", "Mfg", "fx", false);
            original.createPreset(instId, "Warm", "/tmp/warm.state", PresetKind::Instrument);
            original.createPreset(fxId, "Long", "/tmp/long.state", PresetKind::Effect);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            expect(loaded.findPluginByName("Synth")->isInstrument == true);
            expect(loaded.findPluginByName("Delay")->isInstrument == false);

            auto* warmPreset = loaded.findPreset(
                loaded.findPluginByName("Synth")->id, "Warm");
            expect(warmPreset != nullptr);
            expect(warmPreset->kind == PresetKind::Instrument);

            auto* longPreset = loaded.findPreset(
                loaded.findPluginByName("Delay")->id, "Long");
            expect(longPreset != nullptr);
            expect(longPreset->kind == PresetKind::Effect);
        }

        beginTest("Score steps round-trip");
        {
            TempDB db;

            StateAPI original;
            auto actionId = original.registerAction("fadeOut", "Fade out");
            auto songId = original.createSong("S");
            original.setCurrentSong(songId);
            auto* song = original.findSong(songId);
            song->score.push_back({ actionId, "[\"Keys\"]", "Fade keys" });
            song->score.push_back({ actionId, "[\"Bass\"]", "Fade bass" });

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* loadedSong = loaded.currentSong();
            expect(loadedSong != nullptr);
            expectEquals((int)loadedSong->score.size(), 2);
            expectEquals(loadedSong->score[0].description, std::string("Fade keys"));
            expectEquals(loadedSong->score[1].description, std::string("Fade bass"));
        }

        beginTest("Global bindings round-trip");
        {
            TempDB db;

            StateAPI original;
            auto actionId = original.registerAction("masterVol", "Master Volume");
            original.addGlobalBinding("cc", 1, 7, actionId, "[]", "Vol fader");
            original.createSong("S");  // need a song for save

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto globals = loaded.globalBindings();
            expectEquals((int)globals.size(), 1);
            expectEquals(globals[0].number, 7);
            expectEquals(globals[0].description, std::string("Vol fader"));
            expect(globals[0].songId.empty());
        }

        beginTest("Selection state does not persist");
        {
            TempDB db;

            StateAPI original;
            auto songId = original.createSong("S");
            original.setCurrentSong(songId);
            auto trackId = original.createTrack("T");
            original.selectTrack(trackId);
            expectEquals((int)original.selectedTrackIds().size(), 1);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            loaded.setCurrentSong(loaded.allSongs()[0].id);
            expect(loaded.selectedTrackIds().empty());
        }
    }
};

static PersistenceTests persistenceTests;

// ============================================================================
// Integration tests (full coordinator → state → engine path)
// ============================================================================

class IntegrationTests : public juce::UnitTest {
public:
    IntegrationTests() : UnitTest("Integration", "Performance") {}

    void runTest() override {

        beginTest("Create track and verify in state");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("Keys");
            expect(!trackId.empty());
            auto tracks = tc.state().listTracks();
            expectEquals((int)tracks.size(), 1);
            expectEquals(tracks[0].name, std::string("Keys"));
        }

        beginTest("Create bus and send");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            auto busId = tc.state().createBus("Reverb");
            tc.state().addSend(trackId, busId, 0.5f);
            auto sends = tc.state().getTrackSends(trackId);
            expectEquals((int)sends.size(), 1);
            expectEquals(sends[0].busName, std::string("Reverb"));
        }

        beginTest("Rename track");
        {
            TestCoordinator tc;
            auto id = tc.state().createTrack("Old");
            tc.state().renameTrack(id, "New");
            expectEquals(tc.state().listTracks()[0].name, std::string("New"));
        }

        beginTest("Set and get gains");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            auto busId = tc.state().createBus("B");
            tc.state().setTrackGain(trackId, 0.5f);
            tc.state().setBusGain(busId, 0.7f);
            tc.state().setMasterGain(0.8f);
            expectWithinAbsoluteError(tc.state().getTrackGain(trackId), 0.5f, 0.01f);
            expectWithinAbsoluteError(tc.state().getBusGain(busId), 0.7f, 0.01f);
            expectWithinAbsoluteError(tc.state().getMasterGain(), 0.8f, 0.01f);
        }

        beginTest("Add effect to track");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            // DLSMusicDevice is always available on macOS
            auto* plugin = tc.state().findPluginByName("DLSMusicDevice");
            expect(plugin != nullptr);
            auto fxId = tc.state().addEffect(trackId, "FX", plugin->id);
            auto effects = tc.state().getTrackEffects(trackId);
            expectEquals((int)effects.size(), 1);
        }

        beginTest("Remove effect by ID");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            auto* plugin = tc.state().findPluginByName("DLSMusicDevice");
            auto fx1 = tc.state().addEffect(trackId, "FX1", plugin->id);
            auto fx2 = tc.state().addEffect(trackId, "FX2", plugin->id);
            tc.state().removeEffect(fx1);
            auto remaining = tc.state().getTrackEffects(trackId);
            expectEquals((int)remaining.size(), 1);
            expectEquals(remaining[0].effectId, fx2);
        }

        beginTest("Song switching preserves state");
        {
            TestCoordinator tc;
            auto songA = tc->createSong("Song A");
            tc.state().createTrack("A Track");
            tc.state().setTrackGain(tc.state().listTracks()[0].id, 0.25f);

            tc->createSong("Song B");
            tc.state().createTrack("B Track");
            expectEquals((int)tc.state().listTracks().size(), 1);

            tc->loadSong(songA);
            expectEquals((int)tc.state().listTracks().size(), 1);
            expectEquals(tc.state().listTracks()[0].name, std::string("A Track"));
            expectWithinAbsoluteError(tc.state().getTrackGain(tc.state().listTracks()[0].id), 0.25f, 0.01f);
        }

        beginTest("MIDI enabled state");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            expect(tc.state().isTrackMidiEnabled(trackId) == true);
            tc.state().setTrackMidiEnabled(trackId, false);
            expect(tc.state().isTrackMidiEnabled(trackId) == false);
        }

        beginTest("Multiple tracks independent");
        {
            TestCoordinator tc;
            auto t1 = tc.state().createTrack("Keys");
            auto t2 = tc.state().createTrack("Bass");
            tc.state().setTrackGain(t1, 0.8f);
            tc.state().setTrackGain(t2, 0.3f);
            tc.state().setTrackMidiEnabled(t1, false);
            expectWithinAbsoluteError(tc.state().getTrackGain(t1), 0.8f, 0.01f);
            expectWithinAbsoluteError(tc.state().getTrackGain(t2), 0.3f, 0.01f);
            expect(tc.state().isTrackMidiEnabled(t1) == false);
            expect(tc.state().isTrackMidiEnabled(t2) == true);
        }

        beginTest("Persistence round-trip through coordinator");
        {
            TempDB db;
            // Create state via coordinator
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.createSong("Persist Test");
                coord.state().createTrack("T1");
                coord.state().setTrackGain(coord.state().listTracks()[0].id, 0.42f);
                coord.state().createBus("B1");
                coord.save();
                coord.shutdown();
            }
            // Restore in new coordinator
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.restoreSession();

                auto tracks = coord.state().listTracks();
                expectEquals((int)tracks.size(), 1);
                expectEquals(tracks[0].name, std::string("T1"));
                expectWithinAbsoluteError(coord.state().getTrackGain(tracks[0].id), 0.42f, 0.01f);

                auto busses = coord.state().listBusses();
                expectEquals((int)busses.size(), 1);
                expectEquals(busses[0].name, std::string("B1"));

                coord.shutdown();
            }
        }
    }
};

static IntegrationTests integrationTests;

// ============================================================================
// Mock AudioEngine for EngineSync tests
// ============================================================================

class MockAudioEngine : public AudioEngineInterface {
public:
    struct Call {
        std::string method;
        std::string arg1, arg2, arg3;
        float floatArg = 0;
        bool boolArg = false;
    };
    std::vector<Call> calls;
    std::map<juce::String, juce::String> trackPlugins;  // trackId → pluginName

    void clear() { calls.clear(); }

    void createTrackWithId(const juce::String& id, const juce::String& name) override {
        calls.push_back({"createTrackWithId", id.toStdString(), name.toStdString()});
    }
    void removeTrack(const juce::String& id) override {
        calls.push_back({"removeTrack", id.toStdString()});
    }
    bool addTrackInstrument(const juce::String& id, const juce::String& plugin, LoadCallback cb) override {
        calls.push_back({"addTrackInstrument", id.toStdString(), plugin.toStdString()});
        trackPlugins[id] = plugin;
        if (cb) cb();  // fire immediately for testing
        return true;
    }
    void removeTrackInstrument(const juce::String& id) override {
        calls.push_back({"removeTrackInstrument", id.toStdString()});
        trackPlugins.erase(id);
    }
    void setTrackGain(const juce::String& id, float gain) override {
        calls.push_back({"setTrackGain", id.toStdString(), "", "", gain});
    }
    void setTrackMidiEnabled(const juce::String& id, bool enabled) override {
        calls.push_back({"setTrackMidiEnabled", id.toStdString(), "", "", 0, enabled});
    }
    void renameTrack(const juce::String& id, const juce::String& name) override {
        calls.push_back({"renameTrack", id.toStdString(), name.toStdString()});
    }
    void clearAllTracks() override { calls.push_back({"clearAllTracks"}); }

    void createBusWithId(const juce::String& id, const juce::String& name) override {
        calls.push_back({"createBusWithId", id.toStdString(), name.toStdString()});
    }
    void removeBus(const juce::String& id) override {
        calls.push_back({"removeBus", id.toStdString()});
    }
    void setBusGain(const juce::String& id, float gain) override {
        calls.push_back({"setBusGain", id.toStdString(), "", "", gain});
    }
    void renameBus(const juce::String& id, const juce::String& name) override {
        calls.push_back({"renameBus", id.toStdString(), name.toStdString()});
    }
    void clearAllBusses() override { calls.push_back({"clearAllBusses"}); }

    bool addEffect(const juce::String& parent, const juce::String& fxId,
                   const juce::String& plugin, LoadCallback cb) override {
        calls.push_back({"addEffect", parent.toStdString(), fxId.toStdString(), plugin.toStdString()});
        if (cb) cb();
        return true;
    }
    void removeEffect(const juce::String& parent, const juce::String& fxId) override {
        calls.push_back({"removeEffect", parent.toStdString(), fxId.toStdString()});
    }

    void addSend(const juce::String& track, const juce::String& bus, float gain) override {
        calls.push_back({"addSend", track.toStdString(), bus.toStdString(), "", gain});
    }
    void setSendGain(const juce::String& track, const juce::String& bus, float gain) override {
        calls.push_back({"setSendGain", track.toStdString(), bus.toStdString(), "", gain});
    }

    void setMasterGain(float gain) override {
        calls.push_back({"setMasterGain", "", "", "", gain});
    }

    juce::String getTrackPluginName(const juce::String& id) const override {
        auto it = trackPlugins.find(id);
        return it != trackPlugins.end() ? it->second : juce::String();
    }
    juce::AudioProcessor* getTrackInstrumentProcessor(const juce::String&) const override { return nullptr; }
    juce::AudioProcessor* getEffectProcessor(const juce::String&, const juce::String&) const override { return nullptr; }

    bool hasCall(const std::string& method) const {
        for (auto& c : calls) if (c.method == method) return true;
        return false;
    }
    int countCalls(const std::string& method) const {
        int n = 0;
        for (auto& c : calls) if (c.method == method) n++;
        return n;
    }
    const Call* findCall(const std::string& method, const std::string& arg1 = "") const {
        for (auto& c : calls)
            if (c.method == method && (arg1.empty() || c.arg1 == arg1)) return &c;
        return nullptr;
    }
};

// ============================================================================
// EngineSync tests (state events → engine calls via mock)
// ============================================================================

class EngineSyncTests : public juce::UnitTest {
public:
    EngineSyncTests() : UnitTest("EngineSync", "Performance") {}

    void runTest() override {

        beginTest("Song load creates tracks and busses in engine");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto pluginId = state.registerPlugin("DLS", "Apple", "au", true);
            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto trackId = state.createTrack("Keys");
            state.setTrackPlugin(trackId, pluginId);
            state.setTrackGain(trackId, 0.5f);
            auto busId = state.createBus("Reverb");
            state.addSend(trackId, busId, 0.3f);

            // Reset current song so EngineSync can trigger loadSong
            state.setCurrentSong("");

            EngineSync sync(mock, state);
            state.setCurrentSong(songId);  // triggers loadSong

            expect(mock.hasCall("clearAllTracks"));
            expect(mock.hasCall("clearAllBusses"));
            expect(mock.hasCall("createBusWithId"));
            expect(mock.hasCall("createTrackWithId"));
            expect(mock.hasCall("addTrackInstrument"));
            expect(mock.hasCall("addSend"));

            auto* gainCall = mock.findCall("setTrackGain");
            expect(gainCall != nullptr);
            expectWithinAbsoluteError(gainCall->floatArg, 0.5f, 0.01f);
        }

        beginTest("Track rename event updates engine");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            state.createTrack("Old");

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);
            mock.clear();

            state.renameTrack(state.listTracks()[0].id, "New");

            auto* call = mock.findCall("renameTrack");
            expect(call != nullptr);
            expectEquals(call->arg2, std::string("New"));
        }

        beginTest("Effect creation event adds effect to engine");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto pluginId = state.registerPlugin("Delay", "NI", "fx", false);
            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            state.createTrack("T");

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);
            mock.clear();

            auto trackId = state.listTracks()[0].id;
            state.addEffect(trackId, "MyDelay", pluginId);

            expect(mock.hasCall("addEffect"));
            auto* call = mock.findCall("addEffect");
            expectEquals(call->arg3, std::string("Delay"));
        }

        beginTest("Track deletion event removes track from engine");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto trackId = state.createTrack("T");

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);
            mock.clear();

            state.removeTrack(trackId);

            expect(mock.hasCall("removeTrack"));
        }

        beginTest("Master gain update propagates to engine");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);
            mock.clear();

            state.setMasterGain(0.6f);

            auto* call = mock.findCall("setMasterGain");
            expect(call != nullptr);
            expectWithinAbsoluteError(call->floatArg, 0.6f, 0.01f);
        }

        beginTest("LoadStatus Pending blocks instrument re-trigger");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto pluginId = state.registerPlugin("Synth", "Mfg", "au", true);
            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto trackId = state.createTrack("T");
            state.setTrackPlugin(trackId, pluginId);

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);

            // At this point, addTrackInstrument was called once during loadSong.
            // The mock fires callback immediately, setting LoadStatus to Loaded.
            // Now simulate a Track Updated event (e.g., from gain change).
            mock.clear();
            state.setTrackGain(trackId, 0.9f);

            // Should NOT trigger another addTrackInstrument — plugin hasn't changed
            expectEquals(mock.countCalls("addTrackInstrument"), 0);
        }

        beginTest("Instrument change triggers load");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto plugin1 = state.registerPlugin("Synth1", "Mfg", "au1", true);
            auto plugin2 = state.registerPlugin("Synth2", "Mfg", "au2", true);
            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto trackId = state.createTrack("T");
            state.setTrackPlugin(trackId, plugin1);

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);
            mock.clear();

            // Change instrument
            state.setTrackPlugin(trackId, plugin2);

            expect(mock.hasCall("removeTrackInstrument"));
            auto* call = mock.findCall("addTrackInstrument");
            expect(call != nullptr);
            expectEquals(call->arg2, std::string("Synth2"));
        }

        beginTest("Song switch clears and rebuilds engine");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto s1 = state.createSong("A");
            state.setCurrentSong(s1);
            state.createTrack("A Track");

            auto s2 = state.createSong("B");
            state.setCurrentSong(s2);
            state.createTrack("B Track");

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(s1);
            mock.clear();

            // Switch to song B
            state.setCurrentSong(s2);

            expect(mock.hasCall("clearAllTracks"));
            expect(mock.hasCall("clearAllBusses"));
            auto* call = mock.findCall("createTrackWithId");
            expect(call != nullptr);
            expectEquals(call->arg2, std::string("B Track"));
        }
    }
};

static EngineSyncTests engineSyncTests;

// ============================================================================
// Test runner — main()
// ============================================================================

int main(int, char*[]) {
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
