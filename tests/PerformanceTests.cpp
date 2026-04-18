#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "persistence/PersistenceLayer.h"
#include "engine/AudioEngineInterface.h"
#include "engine/EngineSync.h"
#include "engine/Log.h"
#include "state/UndoHistory.h"
#include "song/SongRuntime.h"

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

        beginTest("Device registration and controls");
        {
            StateAPI s;
            auto devId = s.registerDevice("KeyLab 88", "Arturia KeyLab 88 mkII");
            expect(!devId.empty());
            // Deduplicate by port name
            auto devId2 = s.registerDevice("KeyLab 88", "Arturia KeyLab 88 mkII");
            expectEquals(devId, devId2);
            expectEquals((int)s.allDevices().size(), 1);

            s.addDeviceControl(devId, "Fader 1", "cc", 1, 73, "Faders");
            s.addDeviceControl(devId, "Pad 1", "note", 10, 36, "Pads");

            auto* dev = s.findDevice(devId);
            expect(dev != nullptr);
            expectEquals((int)dev->controls.size(), 2);
            expectEquals(dev->controls[0].name, std::string("Fader 1"));
            expectEquals(dev->controls[1].group, std::string("Pads"));
        }

        beginTest("Device-aware bindings");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto actionId = s.registerAction("test", "Test");
            auto devId = s.registerDevice("KeyLab", "keylab-port");

            // Binding with device
            auto b1 = s.addBinding(songId, "cc", 1, 7, actionId, "[]", "KeyLab vol", devId);
            // Binding without device (any)
            auto b2 = s.addBinding(songId, "cc", 1, 10, actionId, "[]", "Any device");

            auto bindings = s.bindingsForSong(songId);
            expectEquals((int)bindings.size(), 2);

            bool foundDevice = false, foundAny = false;
            for (auto& b : bindings) {
                if (b.id == b1) { expectEquals(b.deviceId, devId); foundDevice = true; }
                if (b.id == b2) { expect(b.deviceId.empty()); foundAny = true; }
            }
            expect(foundDevice);
            expect(foundAny);
        }

        beginTest("Song-device association");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto dev1 = s.registerDevice("KeyLab", "keylab-port");
            auto dev2 = s.registerDevice("MPK", "mpk-port");

            s.addDeviceToSong(songId, dev1);
            s.addDeviceToSong(songId, dev2);
            auto devices = s.devicesForSong(songId);
            expectEquals((int)devices.size(), 2);

            s.removeDeviceFromSong(songId, dev1);
            devices = s.devicesForSong(songId);
            expectEquals((int)devices.size(), 1);
            expectEquals(devices[0], dev2);
        }

        beginTest("Device persistence round-trip");
        {
            TempDB db;
            StateAPI original;
            auto devId = original.registerDevice("KeyLab 88", "keylab-port");
            original.addDeviceControl(devId, "Fader 1", "cc", 1, 73, "Faders");
            auto songId = original.createSong("S");
            original.setCurrentSong(songId);
            original.addDeviceToSong(songId, devId);
            auto actionId = original.registerAction("test", "Test");
            original.addBinding(songId, "cc", 1, 73, actionId, "[]", "Vol", devId);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            expectEquals((int)loaded.allDevices().size(), 1);
            auto* dev = loaded.findDeviceByPortName("keylab-port");
            expect(dev != nullptr);
            expectEquals(dev->name, std::string("KeyLab 88"));
            expectEquals((int)dev->controls.size(), 1);

            loaded.setCurrentSong(loaded.allSongs()[0].id);
            auto devices = loaded.devicesForSong(loaded.allSongs()[0].id);
            expectEquals((int)devices.size(), 1);

            auto bindings = loaded.bindingsForSong(loaded.allSongs()[0].id);
            expectEquals((int)bindings.size(), 1);
            expect(!bindings[0].deviceId.empty());
        }

        beginTest("Create audio input track");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createAudioInputTrack("Mic", 0, 1);
            expect(!id.empty());
            auto* track = s.findTrack(id);
            expect(track != nullptr);
            expect(track->sourceType == TrackSourceType::AudioInput);
            expect(track->channelMode == ChannelMode::Mono);
            expectEquals(track->inputChannelStart, 0);
            expectEquals(track->inputChannelCount, 1);
            expect(track->midiEnabled == false);
        }

        beginTest("Audio input track stereo");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createAudioInputTrack("Line In", 0, 2);
            auto* track = s.findTrack(id);
            expect(track->channelMode == ChannelMode::Stereo);
            expectEquals(track->inputChannelCount, 2);
        }

        beginTest("Set track input channels");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto id = s.createAudioInputTrack("Mic", 0, 1);
            s.setTrackInputChannels(id, 1, 2);
            auto* track = s.findTrack(id);
            expectEquals(track->inputChannelStart, 1);
            expectEquals(track->inputChannelCount, 2);
            expect(track->channelMode == ChannelMode::Stereo);
        }

        beginTest("Audio input track persistence round-trip");
        {
            TempDB db;
            StateAPI original;
            auto songId = original.createSong("S");
            original.setCurrentSong(songId);
            original.createTrack("Synth");
            original.createAudioInputTrack("Mic", 0, 1);
            original.createAudioInputTrack("Line In", 0, 2);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            loaded.setCurrentSong(loaded.allSongs()[0].id);
            auto tracks = loaded.listTracks();
            expectEquals((int)tracks.size(), 3);

            for (auto& ti : tracks) {
                auto* t = loaded.findTrack(ti.id);
                if (t->name == "Synth")
                    expect(t->sourceType == TrackSourceType::Instrument);
                if (t->name == "Mic") {
                    expect(t->sourceType == TrackSourceType::AudioInput);
                    expect(t->channelMode == ChannelMode::Mono);
                    expectEquals(t->inputChannelCount, 1);
                }
                if (t->name == "Line In") {
                    expect(t->sourceType == TrackSourceType::AudioInput);
                    expect(t->channelMode == ChannelMode::Stereo);
                    expectEquals(t->inputChannelCount, 2);
                }
            }
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

    // --- New coverage: audioEnabled, custom actions, score steps, device groups ---

        beginTest("Track audioEnabled independent of midiEnabled");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto id = s.createTrack("T");
            expect(s.isTrackAudioEnabled(id) == true);
            expect(s.isTrackMidiEnabled(id) == true);

            s.setTrackAudioEnabled(id, false);
            expect(s.isTrackAudioEnabled(id) == false);
            expect(s.isTrackMidiEnabled(id) == true);  // independent

            s.setTrackMidiEnabled(id, false);
            expect(s.isTrackMidiEnabled(id) == false);
            expect(s.isTrackAudioEnabled(id) == false);  // still off

            s.setTrackAudioEnabled(id, true);
            expect(s.isTrackAudioEnabled(id) == true);
            expect(s.isTrackMidiEnabled(id) == false);  // still off
        }

        beginTest("Custom action create and remove");
        {
            StateAPI s;
            auto id = s.createCustomAction("myAction", "My Action",
                                            "log('hello')", "");
            expect(!id.empty());

            auto* action = s.findActionByName("myAction");
            expect(action != nullptr);
            expectEquals(action->label, std::string("My Action"));
            expectEquals(action->luaCode, std::string("log('hello')"));
            expect(action->songId.empty());  // global

            // Update existing by same name
            auto id2 = s.createCustomAction("myAction", "Updated", "log('bye')", "song1");
            expectEquals(id, id2);  // same ID
            action = s.findActionByName("myAction");
            expectEquals(action->label, std::string("Updated"));
            expectEquals(action->luaCode, std::string("log('bye')"));
            expectEquals(action->songId, std::string("song1"));

            // Remove
            s.removeAction(id);
            expect(s.findActionByName("myAction") == nullptr);
        }

        beginTest("Score steps: set, query, clear, ordering");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto t1 = s.createTrack("T1");
            auto t2 = s.createTrack("T2");
            auto actionId = s.registerAction("fadeOut", "Fade Out");

            auto b1 = s.addBinding(songId, "note", 10, 40, actionId, "[]", "Pad 1");
            auto b2 = s.addBinding(songId, "note", 10, 41, actionId, "[]", "Pad 2");
            auto b3 = s.addBinding(songId, "note", 10, 42, actionId, "[]", "Pad 3");

            s.setBindingAsScoreStep(b1, 1);
            s.setBindingAsScoreStep(b3, 2);

            auto steps = s.scoreSteps();
            expectEquals((int)steps.size(), 2);
            expectEquals(steps[0].id, b1);
            expectEquals(steps[0].scorePosition, 1);
            expectEquals(steps[1].id, b3);
            expectEquals(steps[1].scorePosition, 2);

            // Clear a step
            s.clearScoreStep(b1);
            steps = s.scoreSteps();
            expectEquals((int)steps.size(), 1);
            expectEquals(steps[0].id, b3);

            // b2 was never a score step
            auto* song = s.findSong(songId);
            for (auto& b : song->bindings)
                if (b.id == b2) expect(!b.isScoreStep);
        }

        beginTest("Device control group assignment");
        {
            StateAPI s;
            auto devId = s.registerDevice("MPK", "MPK mini 3");
            s.addDeviceControl(devId, "Pad 1", "note", 10, 40);
            s.addDeviceControl(devId, "Fader 1", "cc", 1, 73);

            auto* device = s.findDevice(devId);
            expect(device != nullptr);
            expect(device->controls[0].group.empty());

            s.setDeviceControlGroup(devId, 0, "Pads");
            s.setDeviceControlGroup(devId, 1, "Faders");

            device = s.findDevice(devId);
            expectEquals(device->controls[0].group, std::string("Pads"));
            expectEquals(device->controls[1].group, std::string("Faders"));

            // Change group
            s.setDeviceControlGroup(devId, 0, "");
            device = s.findDevice(devId);
            expect(device->controls[0].group.empty());
        }

        beginTest("Binding stores device ID");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto actionId = s.registerAction("test");

            auto bId = s.addBinding(songId, "note", 10, 40, actionId,
                                     "[]", "desc", "device123");

            auto* song = s.findSong(songId);
            expect(song != nullptr);
            for (auto& b : song->bindings) {
                if (b.id == bId) {
                    expectEquals(b.deviceId, std::string("device123"));
                }
            }
        }

        beginTest("Track reordering via moveTrack");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto t1 = s.createTrack("A");  // position 0
            auto t2 = s.createTrack("B");  // position 1
            auto t3 = s.createTrack("C");  // position 2

            auto tracks = s.listTracks();
            expectEquals(tracks[0].name, std::string("A"));
            expectEquals(tracks[1].name, std::string("B"));
            expectEquals(tracks[2].name, std::string("C"));

            // Move C to position 0 (before A)
            s.moveTrack(t3, 0);
            tracks = s.listTracks();
            expectEquals(tracks[0].name, std::string("C"));
            expectEquals(tracks[1].name, std::string("A"));
            expectEquals(tracks[2].name, std::string("B"));

            // Move C back to end (position 2)
            s.moveTrack(t3, 2);
            tracks = s.listTracks();
            expectEquals(tracks[0].name, std::string("A"));
            expectEquals(tracks[1].name, std::string("B"));
            expectEquals(tracks[2].name, std::string("C"));
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

        beginTest("Score steps as bindings round-trip");
        {
            TempDB db;

            StateAPI original;
            auto actionId = original.registerAction("fadeOut", "Fade out");
            auto songId = original.createSong("S");
            original.setCurrentSong(songId);

            // Create bindings, mark some as score steps
            auto b1 = original.addBinding(songId, "cc", 10, 1, actionId, "[\"Keys\"]", "Fade keys");
            auto b2 = original.addBinding(songId, "cc", 10, 2, actionId, "[\"Bass\"]", "Fade bass");
            auto b3 = original.addBinding(songId, "cc", 1, 5, actionId, "[]", "Utility toggle");
            original.setBindingAsScoreStep(b1, 0);
            original.setBindingAsScoreStep(b2, 1);
            // b3 is NOT a score step

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            // All 3 bindings should be there
            auto bindings = loaded.bindingsForSong(loaded.currentSong()->id);
            expectEquals((int)bindings.size(), 3);

            // Score should have 2 steps in order
            auto score = loaded.scoreSteps();
            expectEquals((int)score.size(), 2);
            expectEquals(score[0].description, std::string("Fade keys"));
            expectEquals(score[1].description, std::string("Fade bass"));
            expect(score[0].scorePosition == 0);
            expect(score[1].scorePosition == 1);
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

        beginTest("Custom actions persist with lua_code and song_id");
        {
            TempDB db;

            StateAPI original;
            original.createSong("S");
            original.registerAction("builtIn", "Built-in");  // no lua_code
            original.createCustomAction("custom1", "Custom One", "log('hi')", "");
            original.createCustomAction("custom2", "Custom Two", "fadeOut()", "song123");

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* builtIn = loaded.findActionByName("builtIn");
            expect(builtIn != nullptr);
            expect(builtIn->luaCode.empty());

            auto* c1 = loaded.findActionByName("custom1");
            expect(c1 != nullptr);
            expectEquals(c1->label, std::string("Custom One"));
            expectEquals(c1->luaCode, std::string("log('hi')"));
            expect(c1->songId.empty());

            auto* c2 = loaded.findActionByName("custom2");
            expect(c2 != nullptr);
            expectEquals(c2->luaCode, std::string("fadeOut()"));
            expectEquals(c2->songId, std::string("song123"));
        }

        beginTest("Audio device config round-trip with buffer and sample rate");
        {
            TempDB db;
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.state().setConfig("audio_output_device", "Scarlett 2i2");
                coord.state().setConfig("audio_input_device", "MacBook Pro Mic");
                coord.state().setConfig("audio_buffer_size", "256");
                coord.save();
                coord.shutdown();
            }
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                expectEquals(coord.state().getConfig("audio_output_device"),
                             std::string("Scarlett 2i2"));
                expectEquals(coord.state().getConfig("audio_input_device"),
                             std::string("MacBook Pro Mic"));
                expectEquals(coord.state().getConfig("audio_buffer_size"),
                             std::string("256"));
                coord.shutdown();
            }
        }

        beginTest("Device control groups persist");
        {
            TempDB db;

            StateAPI original;
            original.createSong("S");
            auto devId = original.registerDevice("MPK", "MPK mini 3");
            original.addDeviceControl(devId, "Pad 1", "note", 10, 40, "Pads");
            original.addDeviceControl(devId, "Fader 1", "cc", 1, 73, "Faders");

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* dev = loaded.findDeviceByPortName("MPK mini 3");
            expect(dev != nullptr);
            expectEquals((int)dev->controls.size(), 2);
            expectEquals(dev->controls[0].group, std::string("Pads"));
            expectEquals(dev->controls[1].group, std::string("Faders"));
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
            expectEquals((int)tracks.size(), 2);
            expectEquals(tracks[1].name, std::string("Keys"));
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
            expectEquals(tc.state().listTracks()[1].name, std::string("New"));
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
            tc.state().setTrackGain(tc.state().listTracks()[1].id, 0.25f);

            tc->createSong("Song B");
            tc.state().createTrack("B Track");
            expectEquals((int)tc.state().listTracks().size(), 2);

            tc->loadSong(songA);
            expectEquals((int)tc.state().listTracks().size(), 2);
            expectEquals(tc.state().listTracks()[1].name, std::string("A Track"));
            expectWithinAbsoluteError(tc.state().getTrackGain(tc.state().listTracks()[1].id), 0.25f, 0.01f);
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
                coord.state().setTrackGain(coord.state().listTracks()[1].id, 0.42f);
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
                expectEquals((int)tracks.size(), 2);
                expectEquals(tracks[1].name, std::string("T1"));
                expectWithinAbsoluteError(coord.state().getTrackGain(tracks[1].id), 0.42f, 0.01f);

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
    void createAudioInputTrackWithId(const juce::String& id, const juce::String& name,
                                      int start, int count) override {
        calls.push_back({"createAudioInputTrackWithId", id.toStdString(), name.toStdString(), "", 0, false});
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
    void setTrackAudioEnabled(const juce::String& id, bool enabled) override {
        calls.push_back({"setTrackAudioEnabled", id.toStdString(), "", "", 0, enabled});
    }
    void setTrackInputMonitoring(const juce::String& id, bool enabled) override {
        calls.push_back({"setTrackInputMonitoring", id.toStdString(), "", "", 0, enabled});
    }
    void setTrackMuted(const juce::String& id, bool muted) override {
        calls.push_back({"setTrackMuted", id.toStdString(), "", "", 0, muted});
    }
    void setTrackSoloed(const juce::String& id, bool soloed) override {
        calls.push_back({"setTrackSoloed", id.toStdString(), "", "", 0, soloed});
    }
    void setTrackInputChannels(const juce::String& id, int start, int count) override {
        calls.push_back({"setTrackInputChannels", id.toStdString()});
    }
    void renameTrack(const juce::String& id, const juce::String& name) override {
        calls.push_back({"renameTrack", id.toStdString(), name.toStdString()});
    }
    void setTrackOutputTarget(const juce::String& id, const juce::String& target) override {
        calls.push_back({"setTrackOutputTarget", id.toStdString(), target.toStdString()});
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
    void setBusAudioEnabled(const juce::String& id, bool enabled) override {
        calls.push_back({"setBusAudioEnabled", id.toStdString(), "", "", 0, enabled});
    }
    void renameBus(const juce::String& id, const juce::String& name) override {
        calls.push_back({"renameBus", id.toStdString(), name.toStdString()});
    }
    void setBusOutputTarget(const juce::String& id, const juce::String& target) override {
        calls.push_back({"setBusOutputTarget", id.toStdString(), target.toStdString()});
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
    void setMasterAudioEnabled(bool enabled) override {
        calls.push_back({"setMasterAudioEnabled", "", "", "", 0, enabled});
    }

    juce::String getTrackPluginName(const juce::String& id) const override {
        auto it = trackPlugins.find(id);
        return it != trackPlugins.end() ? it->second : juce::String();
    }
    std::vector<juce::String> getInputChannelNames() const override {
        return { "Input 1", "Input 2" };
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

        beginTest("Audio input track creates correct engine call");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            state.createAudioInputTrack("Mic", 0, 1);

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);

            expect(mock.hasCall("createAudioInputTrackWithId"));
            expectEquals(mock.countCalls("createTrackWithId"), 0);
        }

        beginTest("Disabled track propagates audioEnabled on creation");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto trackId = state.createTrack("T");
            state.setTrackAudioEnabled(trackId, false);  // disable before sync

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);  // triggers loadSong

            // The engine should get setTrackAudioEnabled(false)
            auto* call = mock.findCall("setTrackAudioEnabled");
            expect(call != nullptr);
            if (call) expect(call->boolArg == false);
        }

        beginTest("Binding changes trigger restoreBindings via state event");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto actionId = state.registerAction("test");

            state.setCurrentSong("");
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);

            // Adding a binding should fire a Binding event
            // (restoreBindings is handled by coordinator, not EngineSync,
            //  but the event should fire)
            bool bindingEventFired = false;
            auto subId = state.events().subscribe([&](const StateEvent& event) {
                if (event.entity == StateEvent::Binding)
                    bindingEventFired = true;
            });

            state.addBinding(songId, "note", 10, 40, actionId, "[]", "test");
            expect(bindingEventFired);

            state.events().unsubscribe(subId);
        }
    }
};

static EngineSyncTests engineSyncTests;

// ============================================================================
// Audio device config tests
// ============================================================================

class AudioDeviceConfigTests : public juce::UnitTest {
public:
    AudioDeviceConfigTests() : UnitTest("AudioDeviceConfig", "Performance") {}

    void runTest() override {

        beginTest("Audio device name persists via config key");
        {
            StateAPI state;
            state.setConfig("audio_device", "Scarlett 2i2 USB");
            expectEquals(state.getConfig("audio_device"), std::string("Scarlett 2i2 USB"));
        }

        beginTest("Audio device config round-trips through persistence");
        {
            TempDB db;
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.state().setConfig("audio_device", "Scarlett 2i2 USB");
                coord.save();
                coord.shutdown();
            }
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                // Config should be restored from DB
                expectEquals(coord.state().getConfig("audio_device"),
                             std::string("Scarlett 2i2 USB"));
                coord.shutdown();
            }
        }

        beginTest("EngineSync forwards audioEnabled to engine");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto trackId = state.createTrack("T1");
            state.setCurrentSong("");

            EngineSync sync(mock, state);
            state.setCurrentSong(songId);

            mock.clear();
            state.setTrackAudioEnabled(trackId, false);

            auto* call = mock.findCall("setTrackAudioEnabled");
            expect(call != nullptr);
            if (call) expect(call->boolArg == false);
        }

        beginTest("audioEnabled persists through save/load cycle");
        {
            TempDB db;
            std::string trackId;
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.createSong("S");
                trackId = coord.state().createTrack("T1");
                coord.state().setTrackAudioEnabled(trackId, false);
                expect(coord.state().isTrackAudioEnabled(trackId) == false);
                coord.save();
                coord.shutdown();
            }
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.restoreSession();
                expect(coord.state().isTrackAudioEnabled(trackId) == false);
                coord.shutdown();
            }
        }
    }
};

static AudioDeviceConfigTests audioDeviceConfigTests;

// ============================================================================
// InternalSequencer tests
// ============================================================================

#include "daw/InternalSequencer.h"

class SequencerTests : public juce::UnitTest {
public:
    SequencerTests() : UnitTest("Sequencer", "Performance") {}

    void runTest() override {

        beginTest("Transport play/stop");
        {
            InternalSequencer seq;
            expect(!seq.isPlaying());
            seq.play();
            expect(seq.isPlaying());
            seq.stop();
            expect(!seq.isPlaying());
            seq.togglePlayStop();
            expect(seq.isPlaying());
            seq.togglePlayStop();
            expect(!seq.isPlaying());
        }

        beginTest("Tempo get/set with clamping");
        {
            InternalSequencer seq;
            expectWithinAbsoluteError(seq.getTempo(), 120.0, 0.01);
            seq.setTempo(140.0);
            expectWithinAbsoluteError(seq.getTempo(), 140.0, 0.01);
            seq.setTempo(10.0);  // below minimum
            expectWithinAbsoluteError(seq.getTempo(), 20.0, 0.01);
            seq.setTempo(400.0);  // above maximum
            expectWithinAbsoluteError(seq.getTempo(), 300.0, 0.01);
        }

        beginTest("Beat position advances with tempo");
        {
            InternalSequencer seq;
            seq.setTempo(120.0);  // 2 beats per second
            seq.play();
            seq.advance(0.5);  // half second = 1 beat
            expectWithinAbsoluteError(seq.getBeatPosition(), 1.0, 0.01);
            seq.advance(0.5);
            expectWithinAbsoluteError(seq.getBeatPosition(), 2.0, 0.01);
        }

        beginTest("Position does not advance when stopped");
        {
            InternalSequencer seq;
            seq.setTempo(120.0);
            seq.advance(1.0);  // stopped — should not advance
            expectWithinAbsoluteError(seq.getBeatPosition(), 0.0, 0.01);
        }

        beginTest("Loop wraps position");
        {
            InternalSequencer seq;
            seq.setTempo(120.0);  // 2 bps
            seq.setLoopEnabled(true);
            seq.setLoopRange(0.0, 4.0);
            seq.play();
            seq.advance(2.5);  // 5 beats at 120bpm in 2.5s, loop at 4 → wraps to 1
            double pos = seq.getBeatPosition();
            expectWithinAbsoluteError(pos, 1.0, 0.01);
        }

        beginTest("Time signature");
        {
            InternalSequencer seq;
            expectEquals(seq.getTimeSignatureNumerator(), 4);
            expectEquals(seq.getTimeSignatureDenominator(), 4);
            seq.setTimeSignature(3, 4);
            expectEquals(seq.getTimeSignatureNumerator(), 3);
        }

        beginTest("Beat callback fires on each beat");
        {
            InternalSequencer seq;
            seq.setTempo(120.0);
            int callCount = 0;
            double lastBeat = -1;
            seq.setBeatCallback([&](double beat, double bpm) {
                callCount++;
                lastBeat = beat;
            });
            seq.play();
            // Advance 2.1 seconds = 4.2 beats → should fire on beats 0, 1, 2, 3, 4
            for (int i = 0; i < 21; ++i)
                seq.advance(0.1);
            expect(callCount >= 4);
            expect(lastBeat >= 4.0);
        }

        beginTest("Transport callback fires on play/stop");
        {
            InternalSequencer seq;
            bool lastState = false;
            int callCount = 0;
            seq.setTransportCallback([&](bool playing) {
                lastState = playing;
                callCount++;
            });
            seq.play();
            expect(lastState == true);
            seq.stop();
            expect(lastState == false);
            expectEquals(callCount, 2);
        }

        beginTest("Capabilities reflect internal sequencer");
        {
            InternalSequencer seq;
            auto caps = seq.getCapabilities();
            expect(caps.hasTransport);
            expect(caps.hasTempo);
            expect(caps.hasLoop);
            expect(caps.hasMetronome);
            expect(!caps.hasRecording);
            expect(!caps.hasClipTrigger);
            expect(!caps.hasExternalSync);
        }

        beginTest("Set beat position resets");
        {
            InternalSequencer seq;
            seq.setTempo(120.0);
            seq.play();
            seq.advance(1.0);  // 2 beats
            seq.setBeatPosition(0.0);
            expectWithinAbsoluteError(seq.getBeatPosition(), 0.0, 0.01);
        }
    }
};

static SequencerTests sequencerTests;

// ============================================================================
// Arrangement tests
// ============================================================================

#include "daw/Arrangement.h"

class ArrangementTests : public juce::UnitTest {
public:
    ArrangementTests() : UnitTest("Arrangement", "Performance") {}

    // Helper: create an arrangement backed by test tracks
    struct TestContext {
        std::vector<TrackState> tracks;
        Arrangement arr;
        TestContext(std::initializer_list<std::string> trackIds) {
            for (auto& id : trackIds) {
                TrackState t;
                t.id = id;
                t.name = id;
                tracks.push_back(std::move(t));
            }
            arr.setTracks(&tracks);
        }
    };

    void runTest() override {

        beginTest("Add and find MIDI region");
        {
            TestContext ctx({"track1"});
            auto* r = ctx.arr.addMidiRegion("track1", 0.0, 4.0);
            expect(r != nullptr);
            expectWithinAbsoluteError(r->startBeat, 0.0, 0.01);
            expectWithinAbsoluteError(r->lengthBeats, 4.0, 0.01);

            auto* found = ctx.arr.findRegion(r->id);
            expect(found == r);
        }

        beginTest("Regions for track filters correctly");
        {
            TestContext ctx({"t1", "t2"});
            ctx.arr.addMidiRegion("t1", 0.0, 4.0);
            ctx.arr.addMidiRegion("t2", 0.0, 4.0);
            ctx.arr.addMidiRegion("t1", 4.0, 4.0);

            auto t1Regions = ctx.arr.regionsForTrack("t1");
            expectEquals((int)t1Regions.size(), 2);
            auto t2Regions = ctx.arr.regionsForTrack("t2");
            expectEquals((int)t2Regions.size(), 1);
        }

        beginTest("Remove region");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion("t1", 0.0, 4.0);
            auto id = r->id;
            ctx.arr.removeRegion(id);
            expect(ctx.arr.findRegion(id) == nullptr);
        }

        beginTest("Scan MIDI events fires note on and off");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion("t1", 0.0, 4.0);
            auto* take = r->activeTake();
            take->events.push_back({ 1.0, 0x90, 1, 60, 100 });
            take->events.push_back({ 1.5, 0x80, 1, 60, 0 });

            std::vector<std::pair<int, int>> scanned;
            ctx.arr.scanMidiEvents(0.0, 1.6, [&](const std::string&, const MidiEventState& e, double) {
                scanned.push_back({ e.status, e.data1 });
            });

            expectEquals((int)scanned.size(), 2);
            expectEquals(scanned[0].first, 0x90);
            expectEquals(scanned[1].first, 0x80);
        }

        beginTest("Scan skips regions outside range");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion("t1", 8.0, 4.0);
            auto* take = r->activeTake();
            take->events.push_back({ 0.0, 0x90, 1, 60, 100 });

            int eventCount = 0;
            ctx.arr.scanMidiEvents(0.0, 4.0, [&](auto&, auto&, double) { eventCount++; });
            expectEquals(eventCount, 0);
        }

        beginTest("Recording creates region with take and captures events");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.startRecording("t1", 2.0);
            expect(ctx.arr.isRecording());
            expect(r != nullptr);
            expect(r->activeTake() != nullptr);

            ctx.arr.addRecordedEvent({ 0.0, 0x90, 1, 60, 100 });
            ctx.arr.addRecordedEvent({ 0.5, 0x80, 1, 60, 0 });
            ctx.arr.addRecordedEvent({ 0.5, 0x90, 1, 64, 90 });
            ctx.arr.stopRecording();

            expect(!ctx.arr.isRecording());
            auto* take = r->activeTake();
            expectEquals((int)take->events.size(), 3);
            expect(r->lengthBeats > 0.0);
            expectEquals(take->name, std::string("Take 1"));

            auto notes = Arrangement::buildNoteList(*r);
            expectEquals((int)notes.size(), 2);
            expect(std::abs(notes[0].durationBeats - 0.5) < 0.01);
        }
    }
};

static ArrangementTests arrangementTests;

// ============================================================================
// UndoHistory tests
// ============================================================================

class UndoHistoryTests : public juce::UnitTest {
public:
    UndoHistoryTests() : UnitTest("UndoHistory", "Performance") {}

    void runTest() override {

        beginTest("Empty history cannot undo or redo");
        {
            UndoHistory h;
            expect(!h.canUndo());
            expect(!h.canRedo());
        }

        beginTest("Push and undo restores previous state");
        {
            UndoHistory h;
            AppState s1; s1.currentSongId = "song1";
            AppState s2; s2.currentSongId = "song2";

            h.push(s1);
            expect(h.canUndo());
            auto restored = h.undo(s2);
            expectEquals(restored.currentSongId, std::string("song1"));
        }

        beginTest("Undo then redo restores forward state");
        {
            UndoHistory h;
            AppState s1; s1.currentSongId = "song1";
            AppState s2; s2.currentSongId = "song2";

            h.push(s1);
            auto afterUndo = h.undo(s2);
            expect(h.canRedo());
            auto afterRedo = h.redo(afterUndo);
            expectEquals(afterRedo.currentSongId, std::string("song2"));
        }

        beginTest("Push after undo clears redo stack");
        {
            UndoHistory h;
            AppState s1; s1.currentSongId = "s1";
            AppState s2; s2.currentSongId = "s2";
            AppState s3; s3.currentSongId = "s3";

            h.push(s1);
            h.push(s2);
            h.undo(s3);  // undo s2, can redo
            expect(h.canRedo());

            AppState s4; s4.currentSongId = "s4";
            h.push(s4);  // new branch — redo should be gone
            expect(!h.canRedo());
        }

        beginTest("Multiple undo steps");
        {
            UndoHistory h;
            AppState s1; s1.currentSongId = "s1";
            AppState s2; s2.currentSongId = "s2";
            AppState s3; s3.currentSongId = "s3";

            h.push(s1);
            h.push(s2);

            auto r1 = h.undo(s3);
            expectEquals(r1.currentSongId, std::string("s2"));
            auto r2 = h.undo(r1);
            expectEquals(r2.currentSongId, std::string("s1"));
            expect(!h.canUndo());
        }

        beginTest("Max steps trims oldest");
        {
            UndoHistory h;
            for (int i = 0; i < UndoHistory::maxSteps + 10; ++i) {
                AppState s;
                s.currentSongId = "s" + std::to_string(i);
                h.push(s);
            }
            // Should have exactly maxSteps entries
            int count = 0;
            AppState current; current.currentSongId = "current";
            while (h.canUndo()) {
                current = h.undo(current);
                count++;
            }
            expectEquals(count, UndoHistory::maxSteps);
            // Oldest surviving should be s10 (0-9 trimmed)
            expectEquals(current.currentSongId, std::string("s10"));
        }

        beginTest("Suspend prevents push");
        {
            UndoHistory h;
            h.suspend();
            expect(h.isSuspended());

            AppState s1; s1.currentSongId = "s1";
            h.push(s1);
            expect(!h.canUndo());

            h.resume();
            expect(!h.isSuspended());
            h.push(s1);
            expect(h.canUndo());
        }

        beginTest("Clear empties both stacks");
        {
            UndoHistory h;
            AppState s1; s1.currentSongId = "s1";
            AppState s2; s2.currentSongId = "s2";
            h.push(s1);
            h.undo(s2);  // creates redo entry

            h.clear();
            expect(!h.canUndo());
            expect(!h.canRedo());
        }

        beginTest("Undo on empty returns current state unchanged");
        {
            UndoHistory h;
            AppState current; current.currentSongId = "unchanged";
            auto result = h.undo(current);
            expectEquals(result.currentSongId, std::string("unchanged"));
        }

        beginTest("Redo on empty returns current state unchanged");
        {
            UndoHistory h;
            AppState current; current.currentSongId = "unchanged";
            auto result = h.redo(current);
            expectEquals(result.currentSongId, std::string("unchanged"));
        }
    }
};

static UndoHistoryTests undoHistoryTests;

// ============================================================================
// SongRuntime tests (MIDI dispatch with wildcard fallback)
// ============================================================================

class SongRuntimeTests : public juce::UnitTest {
public:
    SongRuntimeTests() : UnitTest("SongRuntime", "Performance") {}

    void runTest() override {

        beginTest("Exact match dispatches to handler");
        {
            SongRuntime rt;
            float received = -1.0f;
            rt.addBinding({ MIDIControl::CC, 1, 7, "dev1" },
                          [&](float v) { received = v; });
            rt.handleControl("dev1", 1, 7, 127);
            expectWithinAbsoluteError(received, 1.0f, 0.01f);
        }

        beginTest("No match does not dispatch");
        {
            SongRuntime rt;
            bool called = false;
            rt.addBinding({ MIDIControl::CC, 1, 7, "dev1" },
                          [&](float) { called = true; });
            rt.handleControl("dev1", 1, 8, 100);  // wrong CC number
            expect(!called);
        }

        beginTest("Wildcard any-device matches");
        {
            SongRuntime rt;
            float received = -1.0f;
            rt.addBinding({ MIDIControl::CC, 1, 7, "" },  // empty deviceId = any device
                          [&](float v) { received = v; });
            rt.handleControl("someDevice", 1, 7, 64);
            expectWithinAbsoluteError(received, 64.0f / 127.0f, 0.01f);
        }

        beginTest("Wildcard any-channel matches");
        {
            SongRuntime rt;
            float received = -1.0f;
            rt.addBinding({ MIDIControl::CC, 0, 7, "dev1" },  // channel 0 = any
                          [&](float v) { received = v; });
            rt.handleControl("dev1", 5, 7, 100);  // channel 5
            expectWithinAbsoluteError(received, 100.0f / 127.0f, 0.01f);
        }

        beginTest("Wildcard any-device + any-channel matches");
        {
            SongRuntime rt;
            float received = -1.0f;
            rt.addBinding({ MIDIControl::CC, 0, 7, "" },
                          [&](float v) { received = v; });
            rt.handleControl("anyDev", 3, 7, 50);
            expectWithinAbsoluteError(received, 50.0f / 127.0f, 0.01f);
        }

        beginTest("Exact match fires alongside wildcards");
        {
            SongRuntime rt;
            int exactCount = 0, wildcardCount = 0;
            rt.addBinding({ MIDIControl::CC, 1, 7, "dev1" },
                          [&](float) { exactCount++; });
            rt.addBinding({ MIDIControl::CC, 0, 7, "" },
                          [&](float) { wildcardCount++; });
            rt.handleControl("dev1", 1, 7, 100);
            expectEquals(exactCount, 1);
            expectEquals(wildcardCount, 1);
        }

        beginTest("Note on/off dispatch with velocity normalization");
        {
            SongRuntime rt;
            float onValue = -1.0f, offValue = -1.0f;
            rt.addBinding({ MIDIControl::Note, 1, 60, "dev1" },
                          [&](float v) {
                              if (v > 0) onValue = v; else offValue = v;
                          });
            rt.handleNoteOn("dev1", 1, 60, 100);
            expectWithinAbsoluteError(onValue, 100.0f / 127.0f, 0.01f);
            rt.handleNoteOff("dev1", 1, 60);
            expectWithinAbsoluteError(offValue, 0.0f, 0.01f);
        }

        beginTest("Pitch bend normalization");
        {
            SongRuntime rt;
            float received = -1.0f;
            rt.addBinding({ MIDIControl::PitchBend, 1, 0, "dev1" },
                          [&](float v) { received = v; });
            rt.handlePitchBend("dev1", 1, 8192);  // center
            expectWithinAbsoluteError(received, 8192.0f / 16383.0f, 0.01f);
        }

        beginTest("Remove binding stops dispatch");
        {
            SongRuntime rt;
            bool called = false;
            MIDIControl ctrl = { MIDIControl::CC, 1, 7, "dev1" };
            rt.addBinding(ctrl, [&](float) { called = true; });
            rt.removeBinding(ctrl);
            rt.handleControl("dev1", 1, 7, 100);
            expect(!called);
        }

        beginTest("Clear bindings removes all");
        {
            SongRuntime rt;
            int count = 0;
            rt.addBinding({ MIDIControl::CC, 1, 7, "dev1" }, [&](float) { count++; });
            rt.addBinding({ MIDIControl::CC, 2, 10, "dev2" }, [&](float) { count++; });
            rt.clearBindings();
            rt.handleControl("dev1", 1, 7, 100);
            rt.handleControl("dev2", 2, 10, 50);
            expectEquals(count, 0);
        }

        beginTest("Multiple handlers on same control all fire");
        {
            SongRuntime rt;
            int count = 0;
            MIDIControl ctrl = { MIDIControl::CC, 1, 7, "dev1" };
            rt.addBinding(ctrl, [&](float) { count++; });
            rt.addBinding(ctrl, [&](float) { count++; });
            rt.handleControl("dev1", 1, 7, 100);
            expectEquals(count, 2);
        }
    }
};

static SongRuntimeTests songRuntimeTests;

// ============================================================================
// Extended Arrangement tests (looping, quantize, split, move, duplicate)
// ============================================================================

class ArrangementExtTests : public juce::UnitTest {
public:
    ArrangementExtTests() : UnitTest("Arrangement / Extended", "Performance") {}

    struct TestContext {
        std::vector<TrackState> tracks;
        Arrangement arr;
        TestContext(std::initializer_list<std::string> trackIds) {
            for (auto& id : trackIds) {
                TrackState t;
                t.id = id;
                t.name = id;
                tracks.push_back(std::move(t));
            }
            arr.setTracks(&tracks);
        }
    };

    void runTest() override {

        beginTest("Move region changes start beat");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion("t1", 0.0, 4.0);
            expect(r != nullptr);
            auto id = r->id;
            ctx.arr.moveRegion(id, "t1", 8.0);
            auto* moved = ctx.arr.findRegion(id);
            expectWithinAbsoluteError(moved->startBeat, 8.0, 0.01);
        }

        beginTest("Move region to different track");
        {
            TestContext ctx({"t1", "t2"});
            auto* r = ctx.arr.addMidiRegion("t1", 0.0, 4.0);
            auto id = r->id;
            ctx.arr.moveRegion(id, "t2", 2.0);

            // Gone from t1
            auto t1regions = ctx.arr.regionsForTrack("t1");
            expectEquals((int)t1regions.size(), 0);
            // Present in t2
            auto t2regions = ctx.arr.regionsForTrack("t2");
            expectEquals((int)t2regions.size(), 1);
            expectWithinAbsoluteError(t2regions[0]->startBeat, 2.0, 0.01);
        }

        beginTest("Duplicate region creates copy at offset");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion("t1", 0.0, 4.0);
            auto origId = r->id;
            auto* dup = ctx.arr.duplicateRegion(origId, "t1", 4.0);
            expect(dup != nullptr);
            expect(dup->id != origId);
            expectWithinAbsoluteError(dup->startBeat, 4.0, 0.01);
            expectWithinAbsoluteError(dup->lengthBeats, 4.0, 0.01);
            auto allRegions = ctx.arr.regionsForTrack("t1");
            expectEquals((int)allRegions.size(), 2);
        }

        beginTest("Split region at beat creates two regions");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion("t1", 0.0, 8.0);

            // Add a note spanning the split point
            auto& take = r->takes.back();
            take.events.push_back({ 0.0, 0x90, 1, 60, 100 });  // noteOn at 0
            take.events.push_back({ 6.0, 0x80, 1, 60, 0 });    // noteOff at 6

            auto origId = r->id;
            auto* right = ctx.arr.splitRegion(origId, 4.0, true);
            expect(right != nullptr);

            auto regions = ctx.arr.regionsForTrack("t1");
            expectEquals((int)regions.size(), 2);

            // Left region: 0-4
            auto* left = ctx.arr.findRegion(origId);
            expectWithinAbsoluteError(left->startBeat, 0.0, 0.01);
            expectWithinAbsoluteError(left->lengthBeats, 4.0, 0.01);

            // Right region: 4-8
            expectWithinAbsoluteError(right->startBeat, 4.0, 0.01);
            expectWithinAbsoluteError(right->lengthBeats, 4.0, 0.01);
        }

        beginTest("Scan MIDI events with quantize applies snap");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion("t1", 0.0, 4.0);
            r->quantize = 1.0;  // snap to whole beats

            auto& take = r->takes.back();
            // noteOn slightly off-grid at 0.3, noteOff at 1.3
            take.events.push_back({ 0.3, 0x90, 1, 60, 100 });
            take.events.push_back({ 1.3, 0x80, 1, 60, 0 });

            std::vector<std::pair<double, uint8_t>> captured;
            ctx.arr.scanMidiEvents(0.0, 4.0,
                [&](const std::string&, const MidiEventState& ev, double beat) {
                    captured.push_back({ beat, ev.status });
                });

            expect(captured.size() >= 2);
            // noteOn should snap to 0.0
            expectWithinAbsoluteError(captured[0].first, 0.0, 0.01);
            // noteOff should shift by the same delta (-0.3), so 1.3 - 0.3 = 1.0
            expectWithinAbsoluteError(captured[1].first, 1.0, 0.01);
        }

        beginTest("Looped region repeats events");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion("t1", 0.0, 2.0);
            r->looped = true;
            r->loopEndBeat = 6.0;

            auto& take = r->takes.back();
            take.events.push_back({ 0.0, 0x90, 1, 60, 100 });
            take.events.push_back({ 1.0, 0x80, 1, 60, 0 });

            std::vector<double> noteOnBeats;
            ctx.arr.scanMidiEvents(0.0, 6.0,
                [&](const std::string&, const MidiEventState& ev, double beat) {
                    if ((ev.status & 0xF0) == 0x90) noteOnBeats.push_back(beat);
                });

            // Should have noteOns at 0, 2, 4 (three repetitions within 0-6)
            expectEquals((int)noteOnBeats.size(), 3);
            expectWithinAbsoluteError(noteOnBeats[0], 0.0, 0.01);
            expectWithinAbsoluteError(noteOnBeats[1], 2.0, 0.01);
            expectWithinAbsoluteError(noteOnBeats[2], 4.0, 0.01);
        }
    }
};

static ArrangementExtTests arrangementExtTests;

// ============================================================================
// Extended Persistence tests (stub bindings, new schema features)
// ============================================================================

class PersistenceExtTests : public juce::UnitTest {
public:
    PersistenceExtTests() : UnitTest("Persistence / Extended", "Performance") {}

    void runTest() override {

        beginTest("Stub binding (empty actionId) persists");
        {
            TempDB db;

            StateAPI original;
            auto songId = original.createSong("Test");
            auto devId = original.registerDevice("MPK", "MPK mini 3");
            original.addDeviceControl(devId, "Pad 1", "note", 10, 36);

            // Create a stub binding — control in the song but no action assigned
            auto bindId = original.addBinding(songId, "note", 10, 36, "", "[]", "Pad 1", devId);
            expect(!bindId.empty());

            auto* song = original.findSong(songId);
            expectEquals((int)song->bindings.size(), 1);
            expectEquals(song->bindings[0].actionId, std::string(""));

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* loadedSong = loaded.currentSong();
            expect(loadedSong != nullptr);
            expectEquals((int)loadedSong->bindings.size(), 1);
            expectEquals(loadedSong->bindings[0].actionId, std::string(""));
            expectEquals(loadedSong->bindings[0].deviceId, devId);
            expectEquals(loadedSong->bindings[0].controlType, std::string("note"));
            expectEquals(loadedSong->bindings[0].channel, 10);
            expectEquals(loadedSong->bindings[0].number, 36);
        }

        beginTest("Stub binding with score step persists");
        {
            TempDB db;

            StateAPI original;
            auto songId = original.createSong("Test");
            original.setCurrentSong(songId);
            auto devId = original.registerDevice("MPK", "MPK mini 3");
            original.addDeviceControl(devId, "Pad 1", "note", 10, 36);

            auto bindId = original.addBinding(songId, "note", 10, 36, "", "[]", "Pad 1", devId);
            original.setBindingAsScoreStep(bindId, 1);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* song = loaded.currentSong();
            expectEquals((int)song->bindings.size(), 1);
            expect(song->bindings[0].isScoreStep);
            expectEquals(song->bindings[0].scorePosition, 1);
            expectEquals(song->bindings[0].actionId, std::string(""));
        }

        beginTest("Duplicate device control rejected by StateAPI");
        {
            StateAPI s;
            auto devId = s.registerDevice("MPK", "MPK mini 3");
            s.addDeviceControl(devId, "Pad 1", "note", 10, 36);
            s.addDeviceControl(devId, "Pad 1 Again", "note", 10, 36);  // same type/ch/num

            auto* dev = s.findDevice(devId);
            expectEquals((int)dev->controls.size(), 1);  // should reject duplicate
        }

        beginTest("Region looped and quantize fields persist");
        {
            TempDB db;

            StateAPI original;
            auto songId = original.createSong("Test");
            original.setCurrentSong(songId);
            auto trackId = original.createTrack("Track 1");

            // Create region with looping and quantize
            auto* song = original.findSong(songId);
            auto* track = original.findTrack(trackId);
            RegionState region;
            region.id = StateAPI::generateId();
            region.startBeat = 0.0;
            region.lengthBeats = 4.0;
            region.looped = true;
            region.loopEndBeat = 12.0;
            region.quantize = 0.5;
            TakeState take;
            take.id = StateAPI::generateId();
            take.name = "Take 1";
            region.takes.push_back(take);
            region.activeTakeId = take.id;
            track->regions.push_back(region);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* loadedSong = loaded.currentSong();
            expect(loadedSong != nullptr);
            expect(!loadedSong->tracks.empty());
            auto& loadedTrack = loadedSong->tracks[0];
            expectEquals((int)loadedTrack.regions.size(), 1);
            expect(loadedTrack.regions[0].looped);
            expectWithinAbsoluteError(loadedTrack.regions[0].loopEndBeat, 12.0, 0.01);
            expectWithinAbsoluteError(loadedTrack.regions[0].quantize, 0.5, 0.01);
        }

        beginTest("Take with MIDI events round-trips through save/load");
        {
            TempDB db;

            StateAPI original;
            auto songId = original.createSong("Test");
            original.setCurrentSong(songId);
            auto trackId = original.createTrack("Track 1");

            auto* track = original.findTrack(trackId);
            RegionState region;
            region.id = StateAPI::generateId();
            region.startBeat = 0.0;
            region.lengthBeats = 4.0;
            TakeState take;
            take.id = StateAPI::generateId();
            take.name = "Take 1";
            // A small chord: note-on + note-on + note-off + note-off
            take.events.push_back({ 0.0,  0x90, 1, 60, 100 });
            take.events.push_back({ 0.0,  0x90, 1, 64, 100 });
            take.events.push_back({ 2.0,  0x80, 1, 60,   0 });
            take.events.push_back({ 2.0,  0x80, 1, 64,   0 });
            region.takes.push_back(take);
            region.activeTakeId = take.id;
            track->regions.push_back(region);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* loadedSong = loaded.currentSong();
            expect(loadedSong != nullptr);
            if (loadedSong && !loadedSong->tracks.empty()) {
                auto& loadedTrack = loadedSong->tracks[0];
                expectEquals((int)loadedTrack.regions.size(), 1);
                if (!loadedTrack.regions.empty()) {
                    auto& loadedRegion = loadedTrack.regions[0];
                    expectEquals((int)loadedRegion.takes.size(), 1);
                    if (!loadedRegion.takes.empty()) {
                        auto& loadedTake = loadedRegion.takes[0];
                        expectEquals((int)loadedTake.events.size(), 4);
                        if (loadedTake.events.size() >= 4) {
                            expectEquals(loadedTake.events[0].status, 0x90);
                            expectEquals(loadedTake.events[0].data1, 60);
                            expectEquals(loadedTake.events[0].data2, 100);
                            expectEquals(loadedTake.events[3].status, 0x80);
                            expectEquals(loadedTake.events[3].data1, 64);
                        }
                    }
                }
            }
        }

        beginTest("Backup file (state.bak.db) captures the last committed save");
        {
            TempDB db;

            StateAPI original;
            auto songId = original.createSong("Backup Me");
            original.setCurrentSong(songId);
            original.createTrack("Track 1");
            original.createTrack("Track 2");

            // The backup is taken BEFORE each save runs (as a safety net for
            // the about-to-run save failing). So after the first save, the
            // backup is still pre-save and empty. After the second save, the
            // backup captures what the first save committed.
            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }
            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            auto dbFile = juce::File(db.path());
            auto bakFile = dbFile.getSiblingFile(dbFile.getFileNameWithoutExtension() + ".bak.db");
            expect(bakFile.existsAsFile());

            StateAPI fromBackup;
            { PersistenceLayer p; p.open(bakFile.getFullPathName().toStdString()); p.loadInto(fromBackup); }

            auto* bakSong = fromBackup.currentSong();
            expect(bakSong != nullptr);
            if (bakSong) expectEquals((int)bakSong->tracks.size(), 2);

            bakFile.deleteFile();
        }

        beginTest("Multi-cycle save/reopen preserves state across many cycles");
        {
            TempDB db;

            StateAPI seed;
            auto songId = seed.createSong("MultiSave");
            seed.setCurrentSong(songId);
            seed.createTrack("Kit");
            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(seed); }

            for (int cycle = 0; cycle < 5; ++cycle) {
                StateAPI reloaded;
                { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(reloaded); }

                auto* song = reloaded.currentSong();
                expect(song != nullptr);
                if (song) {
                    expectEquals(song->name, std::string("MultiSave"));
                    expectEquals((int)song->tracks.size(), 1);
                }

                // Resave the reloaded state — simulates autosave + quit cycles.
                { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(reloaded); }
            }
        }

        beginTest("Track processorState blob round-trips through save/load");
        {
            TempDB db;

            StateAPI original;
            // Register a fake plugin in the catalog so the track's pluginId
            // resolves to something real.
            auto pluginId = original.registerPlugin("DLSMusicDevice", "Apple",
                                                    "AudioUnit", /*isInstrument*/true);

            auto songId = original.createSong("Plugin Test");
            original.setCurrentSong(songId);
            auto trackId = original.createTrack("Keys");
            auto* track = original.findTrack(trackId);
            if (track) {
                track->pluginId = pluginId;
                // Simulate a plugin state blob the size DLS produces (~916 bytes
                // per the real logs). Binary, stored base64-encoded by the
                // save path; use a mix of printable + high-bit chars so we'd
                // catch encoding bugs.
                std::string blob;
                blob.reserve(916);
                for (int i = 0; i < 916; ++i)
                    blob.push_back((char)(i & 0xFF));
                track->processorState = blob;
                track->processorStateHash = "test-hash";
            }

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* loadedSong = loaded.currentSong();
            expect(loadedSong != nullptr);
            if (loadedSong && !loadedSong->tracks.empty()) {
                auto& t = loadedSong->tracks[0];
                expectEquals(t.pluginId, pluginId);
                expectEquals((int)t.processorState.size(), 916);
                if (t.processorState.size() == 916) {
                    expect(t.processorState[0]     == (char)0);
                    expect(t.processorState[255]   == (char)255);
                    expect(t.processorState[916-1] == (char)((916-1) & 0xFF));
                }
            }
        }

        beginTest("Coordinator shutdown → relaunch preserves track with recorded region");
        {
            TempDB db;

            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                auto songId = coord.state().createSong("Session");
                coord.state().setCurrentSong(songId);
                auto trackId = coord.state().createTrack("Keys");

                auto* track = coord.state().findTrack(trackId);
                expect(track != nullptr);
                if (track) {
                    RegionState region;
                    region.id = StateAPI::generateId();
                    region.startBeat = 0.0;
                    region.lengthBeats = 4.0;
                    TakeState take;
                    take.id = StateAPI::generateId();
                    take.name = "Take 1";
                    take.events.push_back({ 0.0, 0x90, 1, 60, 100 });
                    take.events.push_back({ 1.0, 0x80, 1, 60,   0 });
                    region.takes.push_back(take);
                    region.activeTakeId = take.id;
                    track->regions.push_back(region);
                }

                coord.save();
                // coord dtor runs shutdown(): captureProcessorState +
                // saveFrom + persistence.reset() — same as real app quit.
            }

            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());

                auto* song = coord.state().currentSong();
                expect(song != nullptr);
                if (song) {
                    expectEquals(song->name, std::string("Session"));
                    expectEquals((int)song->tracks.size(), 1);
                    if (!song->tracks.empty()) {
                        auto& t = song->tracks[0];
                        expectEquals((int)t.regions.size(), 1);
                        if (!t.regions.empty()) {
                            auto& r = t.regions[0];
                            expectEquals((int)r.takes.size(), 1);
                            if (!r.takes.empty()) {
                                expectEquals((int)r.takes[0].events.size(), 2);
                            }
                        }
                    }
                }
            }
        }
    }
};

static PersistenceExtTests persistenceExtTests;

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
