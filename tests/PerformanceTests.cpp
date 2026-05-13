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
#include "install/BundledPluginInstaller.h"
#include "composer/V2NotationParser.h"
#include "composer/ABCParser.h"
#include "composer/ABCWriter.h"
#include "composer/RegionContent.h"
#include "composer/ComposerOutput.h"
#include "composer/ComposerWriter.h"
#include "daw/Arrangement.h"
#include "state/StateModel.h"

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
            auto fxId = s.addEffect(trackId.str(), "Raum", pluginId);
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
            s.addEffect(busId.str(), "Delay", pluginId);
            expectEquals((int)s.getBusEffects(busId).size(), 1);
        }

        beginTest("Add master effect");
        {
            StateAPI s;
            auto songId = s.createSong("S");
            s.setCurrentSong(songId);
            auto pluginId = s.registerPlugin("Limiter", "Apple", "fx", false);
            s.addEffect(songId.str(), "Limiter", pluginId);
            expectEquals((int)s.getMasterEffects().size(), 1);
        }

        beginTest("Remove effect");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto pluginId = s.registerPlugin("FX", "M", "fx", false);
            auto trackId = s.createTrack("T");
            auto fx1 = s.addEffect(trackId.str(), "FX", pluginId);
            auto fx2 = s.addEffect(trackId.str(), "FX", pluginId);
            expectEquals((int)s.getTrackEffects(trackId).size(), 2);
            s.removeEffect(fx1);
            auto remaining = s.getTrackEffects(trackId);
            expectEquals((int)remaining.size(), 1);
            expectEquals(remaining[0].effectId.str(), fx2.str());
        }

        beginTest("Effect load status");
        {
            StateAPI s;
            s.setCurrentSong(s.createSong("S"));
            auto pluginId = s.registerPlugin("FX", "M", "fx", false);
            auto trackId = s.createTrack("T");
            auto fxId = s.addEffect(trackId.str(), "FX", pluginId);
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
            expectEquals(sends[0].busId.str(), b2.str());
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
            expectWithinAbsoluteError(s.getTrackGain(t1), 0.8f, 0.01f);
            expectWithinAbsoluteError(s.getTrackGain(t2), 0.3f, 0.01f);
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
            expectEquals(id1.str(), id1dup.str());
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
            ParamSchema p; p.name = "track"; p.type = ParamType::ChannelRef; p.scope = { "track" };
            auto id = s.registerAction("fadeOut", "Fade out", { p });
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
                if (b.number == 7) expectEquals(b.actionId.str(), a2.str());
                if (b.number == 10) expectEquals(b.actionId.str(), a1.str());
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
            expectEquals(s.findTrackIdByName("Keys").str(), t1.str());
            expectEquals(s.findBusIdByName("Reverb").str(), b1.str());
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
            expectEquals(devId.str(), devId2.str());
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
                if (b.id == b1) { expectEquals(b.deviceId.str(), devId.str()); foundDevice = true; }
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
            expectEquals(devices[0].str(), dev2.str());
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
            expectEquals(s.getMasterOutputId(), songId.str());
        }

    // --- New coverage: audioEnabled, custom actions, score steps, device groups ---

        beginTest("Custom action create and remove");
        {
            StateAPI s;
            auto id = s.createCustomAction("myAction", "My Action",
                                            "log('hello')", {}, SongId{});
            expect(!id.empty());

            auto* action = s.findActionByName("myAction");
            expect(action != nullptr);
            expectEquals(action->label, std::string("My Action"));
            expectEquals(action->luaCode, std::string("log('hello')"));
            expect(action->songId.empty());  // global

            // Update existing by same name
            auto id2 = s.createCustomAction("myAction", "Updated", "log('bye')", {}, SongId{"song1"});
            expectEquals(id.str(), id2.str());  // same ID
            action = s.findActionByName("myAction");
            expectEquals(action->label, std::string("Updated"));
            expectEquals(action->luaCode, std::string("log('bye')"));
            expectEquals(action->songId.str(), std::string("song1"));

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
            expectEquals(steps[0].id.str(), b1.str());
            expectEquals(steps[0].scorePosition, 1);
            expectEquals(steps[1].id.str(), b3.str());
            expectEquals(steps[1].scorePosition, 2);

            // Clear a step
            s.clearScoreStep(b1);
            steps = s.scoreSteps();
            expectEquals((int)steps.size(), 1);
            expectEquals(steps[0].id.str(), b3.str());

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
                                     "[]", "desc", DeviceId{"device123"});

            auto* song = s.findSong(songId);
            expect(song != nullptr);
            for (auto& b : song->bindings) {
                if (b.id == bId) {
                    expectEquals(b.deviceId.str(), std::string("device123"));
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
            ParamSchema tp; tp.name = "track"; tp.type = ParamType::ChannelRef; tp.scope = { "track" };
            original.registerAction("fadeOut", "Fade out", { tp });

            auto songId = original.createSong("My Song");
            original.setCurrentSong(songId);
            original.setMasterGain(0.8f);

            auto t1 = original.createTrack("Keys");
            original.setTrackGain(t1, 0.6f);
            original.setTrackPlugin(t1, pluginId);

            auto t2 = original.createTrack("Bass");
            original.setTrackGain(t2, 0.4f);

            auto busId = original.createBus("Reverb");
            original.setBusGain(busId, 0.7f);

            original.addEffect(t1.str(), "Delay", fxPluginId);
            original.addEffect(busId.str(), "Delay2", fxPluginId);
            original.addEffect(songId.str(), "MasterFX", fxPluginId);

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
            // Note: setCurrentSong above already stamps currentSongId; we used
            // to also call setConfig("current_song_id", s2) here, but that put
            // the same key in the config map AND in the special-cased save
            // path — a UNIQUE constraint violation that was silently swallowed
            // before the save-side error checking landed.

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
            auto fxId = original.addEffect(trackId.str(), "FX", fxPluginId);

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
            original.createCustomAction("custom1", "Custom One", "log('hi')", {}, SongId{});
            original.createCustomAction("custom2", "Custom Two", "fadeOut()", {}, SongId{"song123"});

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
            expectEquals(c2->songId.str(), std::string("song123"));
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
            auto fxId = tc.state().addEffect(trackId.str(), "FX", plugin->id);
            auto effects = tc.state().getTrackEffects(trackId);
            expectEquals((int)effects.size(), 1);
        }

        beginTest("Remove effect by ID");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            auto* plugin = tc.state().findPluginByName("DLSMusicDevice");
            auto fx1 = tc.state().addEffect(trackId.str(), "FX1", plugin->id);
            auto fx2 = tc.state().addEffect(trackId.str(), "FX2", plugin->id);
            tc.state().removeEffect(fx1);
            auto remaining = tc.state().getTrackEffects(trackId);
            expectEquals((int)remaining.size(), 1);
            expectEquals(remaining[0].effectId.str(), fx2.str());
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

        beginTest("Multiple tracks independent");
        {
            TestCoordinator tc;
            auto t1 = tc.state().createTrack("Keys");
            auto t2 = tc.state().createTrack("Bass");
            tc.state().setTrackGain(t1, 0.8f);
            tc.state().setTrackGain(t2, 0.3f);
            expectWithinAbsoluteError(tc.state().getTrackGain(t1), 0.8f, 0.01f);
            expectWithinAbsoluteError(tc.state().getTrackGain(t2), 0.3f, 0.01f);
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
            state.setCurrentSong(SongId{});

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

        beginTest("Audio input track applies inputMonitoring on song load");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto trackId = state.createAudioInputTrack("Mic", 0, 1);
            state.setTrackInputMonitoring(trackId, false);

            // Reset + attach EngineSync so the next setCurrentSong triggers
            // a full re-creation via loadSong.
            state.setCurrentSong(SongId{});
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);

            // The engine's default inputMonitoring is true, so a persisted
            // `false` must be applied on (re)creation. Regression guard —
            // missing this call caused live input to leak even when the UI
            // showed monitoring off until the user toggled it manually.
            auto* call = mock.findCall("setTrackInputMonitoring");
            expect(call != nullptr);
            if (call) {
                expectEquals(call->arg1, trackId.str());
                expect(!call->boolArg);
            }
        }

        beginTest("Track rename event updates engine");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            state.createTrack("Old");

            state.setCurrentSong(SongId{});
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

            state.setCurrentSong(SongId{});
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);
            mock.clear();

            auto trackId = state.listTracks()[0].id;
            state.addEffect(trackId.str(), "MyDelay", pluginId);

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

            state.setCurrentSong(SongId{});
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
            state.setCurrentSong(SongId{});
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

            state.setCurrentSong(SongId{});
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

            state.setCurrentSong(SongId{});
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

            state.setCurrentSong(SongId{});
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

            state.setCurrentSong(SongId{});
            EngineSync sync(mock, state);
            state.setCurrentSong(songId);

            expect(mock.hasCall("createAudioInputTrackWithId"));
            expectEquals(mock.countCalls("createTrackWithId"), 0);
        }

        beginTest("Binding changes trigger restoreBindings via state event");
        {
            StateAPI state;
            MockAudioEngine mock;

            auto songId = state.createSong("S");
            state.setCurrentSong(songId);
            auto actionId = state.registerAction("test");

            state.setCurrentSong(SongId{});
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
                t.id = TrackId{id};
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
            auto* r = ctx.arr.addMidiRegion(TrackId{"track1"}, 0.0, 4.0);
            expect(r != nullptr);
            expectWithinAbsoluteError(r->startBeat, 0.0, 0.01);
            expectWithinAbsoluteError(r->lengthBeats, 4.0, 0.01);

            auto* found = ctx.arr.findRegion(r->id);
            expect(found == r);
        }

        beginTest("Regions for track filters correctly");
        {
            TestContext ctx({"t1", "t2"});
            ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 4.0);
            ctx.arr.addMidiRegion(TrackId{"t2"}, 0.0, 4.0);
            ctx.arr.addMidiRegion(TrackId{"t1"}, 4.0, 4.0);

            auto t1Regions = ctx.arr.regionsForTrack(TrackId{"t1"});
            expectEquals((int)t1Regions.size(), 2);
            auto t2Regions = ctx.arr.regionsForTrack(TrackId{"t2"});
            expectEquals((int)t2Regions.size(), 1);
        }

        beginTest("Remove region");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 4.0);
            auto id = r->id;
            ctx.arr.removeRegion(id);
            expect(ctx.arr.findRegion(id) == nullptr);
        }

        beginTest("Scan MIDI events fires note on and off");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 4.0);
            auto* take = r->activeTake();
            take->events.push_back({ 1.0, 0x90, 1, 60, 100 });
            take->events.push_back({ 1.5, 0x80, 1, 60, 0 });

            std::vector<std::pair<int, int>> scanned;
            ctx.arr.scanMidiEvents(0.0, 1.6, [&](const TrackId&, const MidiEventState& e, double) {
                scanned.push_back({ e.status, e.data1 });
            });

            expectEquals((int)scanned.size(), 2);
            expectEquals(scanned[0].first, 0x90);
            expectEquals(scanned[1].first, 0x80);
        }

        beginTest("Scan skips regions outside range");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 8.0, 4.0);
            auto* take = r->activeTake();
            take->events.push_back({ 0.0, 0x90, 1, 60, 100 });

            int eventCount = 0;
            ctx.arr.scanMidiEvents(0.0, 4.0, [&](auto&, auto&, double) { eventCount++; });
            expectEquals(eventCount, 0);
        }

        beginTest("Recording creates region with take and captures events");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.startRecording(TrackId{"t1"}, 2.0);
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
            AppState s1; s1.currentSongId = SongId{"song1"};
            AppState s2; s2.currentSongId = SongId{"song2"};

            h.push(s1);
            expect(h.canUndo());
            auto restored = h.undo(s2);
            expectEquals(restored.currentSongId.str(), std::string("song1"));
        }

        beginTest("Undo then redo restores forward state");
        {
            UndoHistory h;
            AppState s1; s1.currentSongId = SongId{"song1"};
            AppState s2; s2.currentSongId = SongId{"song2"};

            h.push(s1);
            auto afterUndo = h.undo(s2);
            expect(h.canRedo());
            auto afterRedo = h.redo(afterUndo);
            expectEquals(afterRedo.currentSongId.str(), std::string("song2"));
        }

        beginTest("Push after undo clears redo stack");
        {
            UndoHistory h;
            AppState s1; s1.currentSongId = SongId{"s1"};
            AppState s2; s2.currentSongId = SongId{"s2"};
            AppState s3; s3.currentSongId = SongId{"s3"};

            h.push(s1);
            h.push(s2);
            h.undo(s3);  // undo s2, can redo
            expect(h.canRedo());

            AppState s4; s4.currentSongId = SongId{"s4"};
            h.push(s4);  // new branch — redo should be gone
            expect(!h.canRedo());
        }

        beginTest("Multiple undo steps");
        {
            UndoHistory h;
            AppState s1; s1.currentSongId = SongId{"s1"};
            AppState s2; s2.currentSongId = SongId{"s2"};
            AppState s3; s3.currentSongId = SongId{"s3"};

            h.push(s1);
            h.push(s2);

            auto r1 = h.undo(s3);
            expectEquals(r1.currentSongId.str(), std::string("s2"));
            auto r2 = h.undo(r1);
            expectEquals(r2.currentSongId.str(), std::string("s1"));
            expect(!h.canUndo());
        }

        beginTest("Max steps trims oldest");
        {
            UndoHistory h;
            for (int i = 0; i < UndoHistory::maxSteps + 10; ++i) {
                AppState s;
                s.currentSongId = SongId{"s" + std::to_string(i)};
                h.push(s);
            }
            // Should have exactly maxSteps entries
            int count = 0;
            AppState current; current.currentSongId = SongId{"current"};
            while (h.canUndo()) {
                current = h.undo(current);
                count++;
            }
            expectEquals(count, UndoHistory::maxSteps);
            // Oldest surviving should be s10 (0-9 trimmed)
            expectEquals(current.currentSongId.str(), std::string("s10"));
        }

        beginTest("Suspend prevents push");
        {
            UndoHistory h;
            h.suspend();
            expect(h.isSuspended());

            AppState s1; s1.currentSongId = SongId{"s1"};
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
            AppState s1; s1.currentSongId = SongId{"s1"};
            AppState s2; s2.currentSongId = SongId{"s2"};
            h.push(s1);
            h.undo(s2);  // creates redo entry

            h.clear();
            expect(!h.canUndo());
            expect(!h.canRedo());
        }

        beginTest("Undo on empty returns current state unchanged");
        {
            UndoHistory h;
            AppState current; current.currentSongId = SongId{"unchanged"};
            auto result = h.undo(current);
            expectEquals(result.currentSongId.str(), std::string("unchanged"));
        }

        beginTest("Redo on empty returns current state unchanged");
        {
            UndoHistory h;
            AppState current; current.currentSongId = SongId{"unchanged"};
            auto result = h.redo(current);
            expectEquals(result.currentSongId.str(), std::string("unchanged"));
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
                t.id = TrackId{id};
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
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 4.0);
            expect(r != nullptr);
            auto id = r->id;
            ctx.arr.moveRegion(id, TrackId{"t1"}, 8.0);
            auto* moved = ctx.arr.findRegion(id);
            expectWithinAbsoluteError(moved->startBeat, 8.0, 0.01);
        }

        beginTest("Move region to different track");
        {
            TestContext ctx({"t1", "t2"});
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 4.0);
            auto id = r->id;
            ctx.arr.moveRegion(id, TrackId{"t2"}, 2.0);

            // Gone from t1
            auto t1regions = ctx.arr.regionsForTrack(TrackId{"t1"});
            expectEquals((int)t1regions.size(), 0);
            // Present in t2
            auto t2regions = ctx.arr.regionsForTrack(TrackId{"t2"});
            expectEquals((int)t2regions.size(), 1);
            expectWithinAbsoluteError(t2regions[0]->startBeat, 2.0, 0.01);
        }

        beginTest("Duplicate region creates copy at offset");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 4.0);
            auto origId = r->id;
            auto* dup = ctx.arr.duplicateRegion(origId, TrackId{"t1"}, 4.0);
            expect(dup != nullptr);
            expect(dup->id != origId);
            expectWithinAbsoluteError(dup->startBeat, 4.0, 0.01);
            expectWithinAbsoluteError(dup->lengthBeats, 4.0, 0.01);
            auto allRegions = ctx.arr.regionsForTrack(TrackId{"t1"});
            expectEquals((int)allRegions.size(), 2);
        }

        beginTest("Split region at beat creates two regions");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 8.0);

            // Add a note spanning the split point
            auto& take = r->takes.back();
            take.events.push_back({ 0.0, 0x90, 1, 60, 100 });  // noteOn at 0
            take.events.push_back({ 6.0, 0x80, 1, 60, 0 });    // noteOff at 6

            auto origId = r->id;
            auto* right = ctx.arr.splitRegion(origId, 4.0, true);
            expect(right != nullptr);

            auto regions = ctx.arr.regionsForTrack(TrackId{"t1"});
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
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 4.0);
            r->quantize = 1.0;  // snap to whole beats

            auto& take = r->takes.back();
            // noteOn slightly off-grid at 0.3, noteOff at 1.3
            take.events.push_back({ 0.3, 0x90, 1, 60, 100 });
            take.events.push_back({ 1.3, 0x80, 1, 60, 0 });

            std::vector<std::pair<double, uint8_t>> captured;
            ctx.arr.scanMidiEvents(0.0, 4.0,
                [&](const TrackId&, const MidiEventState& ev, double beat) {
                    captured.push_back({ beat, ev.status });
                });

            expect(captured.size() >= 2);
            // noteOn should snap to 0.0
            expectWithinAbsoluteError(captured[0].first, 0.0, 0.01);
            // noteOff should shift by the same delta (-0.3), so 1.3 - 0.3 = 1.0
            expectWithinAbsoluteError(captured[1].first, 1.0, 0.01);
        }

        beginTest("Quantize snaps to GLOBAL grid, not region-local");
        {
            // The bug we're guarding against: a region whose startBeat
            // isn't on a grid boundary used to quantize events to a grid
            // offset by region.startBeat % grid, so notes landed slightly
            // off the timeline gridlines and out of sync with the
            // metronome (which clicks at integer beats globally).
            TestContext ctx({"t1"});
            // Region starts at 47.95 (just before bar-13 boundary at 48
            // in a 4/4 song). User played a note "on bar 13" but with
            // typical 0.15-beat play latency, so it landed at global beat
            // 48.10 → region-local beatOffset = 0.15.
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 47.95, 4.0);
            r->quantize = 1.0;  // snap to quarter notes

            auto& take = r->takes.back();
            take.events.push_back({ 0.15, 0x90, 1, 60, 100 });   // global 48.10
            take.events.push_back({ 1.15, 0x80, 1, 60, 0 });

            std::vector<double> beats;
            ctx.arr.scanMidiEvents(47.0, 50.0,
                [&](const TrackId&, const MidiEventState& ev, double beat) {
                    if ((ev.status & 0xF0) == 0x90) beats.push_back(beat);
                });

            expectEquals((int)beats.size(), 1);
            // Should snap to 48.0 (bar 13), NOT 47.95 (region origin).
            expectWithinAbsoluteError(beats[0], 48.0, 0.001);
        }

        beginTest("Looped region with no following region loops indefinitely");
        {
            // Regression: this used to cap at 8 reps when no next region
            // existed on the track, so a looped 1-beat region would stop
            // emitting events after beat 8. Should now emit indefinitely.
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 1.0);
            r->looped = true;
            r->loopEndBeat = 0.0;  // unset → use computeLoopEnd

            auto& take = r->takes.back();
            take.events.push_back({ 0.0, 0x90, 1, 60, 100 });

            // Scan well past the old 9-rep cap.
            std::vector<double> noteOnBeats;
            ctx.arr.scanMidiEvents(0.0, 50.0,
                [&](const TrackId&, const MidiEventState& ev, double beat) {
                    if ((ev.status & 0xF0) == 0x90) noteOnBeats.push_back(beat);
                });

            // Should have 50 noteOns (one per beat), not 9.
            expectEquals((int)noteOnBeats.size(), 50);
            expectWithinAbsoluteError(noteOnBeats[0], 0.0, 0.01);
            expectWithinAbsoluteError(noteOnBeats[49], 49.0, 0.01);
        }

        beginTest("Looped region repeats events");
        {
            TestContext ctx({"t1"});
            auto* r = ctx.arr.addMidiRegion(TrackId{"t1"}, 0.0, 2.0);
            r->looped = true;
            r->loopEndBeat = 6.0;

            auto& take = r->takes.back();
            take.events.push_back({ 0.0, 0x90, 1, 60, 100 });
            take.events.push_back({ 1.0, 0x80, 1, 60, 0 });

            std::vector<double> noteOnBeats;
            ctx.arr.scanMidiEvents(0.0, 6.0,
                [&](const TrackId&, const MidiEventState& ev, double beat) {
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
            auto bindId = original.addBinding(songId, "note", 10, 36, ActionId{}, "[]", "Pad 1", devId);
            expect(!bindId.empty());

            auto* song = original.findSong(songId);
            expectEquals((int)song->bindings.size(), 1);
            expectEquals(song->bindings[0].actionId.str(), std::string(""));

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* loadedSong = loaded.currentSong();
            expect(loadedSong != nullptr);
            expectEquals((int)loadedSong->bindings.size(), 1);
            expectEquals(loadedSong->bindings[0].actionId.str(), std::string(""));
            expectEquals(loadedSong->bindings[0].deviceId.str(), devId.str());
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

            auto bindId = original.addBinding(songId, "note", 10, 36, ActionId{}, "[]", "Pad 1", devId);
            original.setBindingAsScoreStep(bindId, 1);

            { PersistenceLayer p; p.open(db.path().toStdString()); p.saveFrom(original); }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* song = loaded.currentSong();
            expectEquals((int)song->bindings.size(), 1);
            expect(song->bindings[0].isScoreStep);
            expectEquals(song->bindings[0].scorePosition, 1);
            expectEquals(song->bindings[0].actionId.str(), std::string(""));
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
            region.id = RegionId{StateAPI::generateId()};
            region.startBeat = 0.0;
            region.lengthBeats = 4.0;
            region.looped = true;
            region.loopEndBeat = 12.0;
            region.quantize = 0.5;
            TakeState take;
            take.id = TakeId{StateAPI::generateId()};
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
            region.id = RegionId{StateAPI::generateId()};
            region.startBeat = 0.0;
            region.lengthBeats = 4.0;
            TakeState take;
            take.id = TakeId{StateAPI::generateId()};
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
                expectEquals(t.pluginId.str(), pluginId.str());
                expectEquals((int)t.processorState.size(), 916);
                if (t.processorState.size() == 916) {
                    expect(t.processorState[0]     == (char)0);
                    expect(t.processorState[255]   == (char)255);
                    expect(t.processorState[916-1] == (char)((916-1) & 0xFF));
                }
            }
        }

        beginTest("Multi-cycle save with plugin-referencing tracks (FK regression)");
        {
            TempDB db;

            StateAPI seed;
            auto pluginId = seed.registerPlugin("DLSMusicDevice", "Apple", "AudioUnit", true);
            auto songId = seed.createSong("Session");
            seed.setCurrentSong(songId);
            auto trackId = seed.createTrack("Keys");
            if (auto* t = seed.findTrack(trackId))
                t->pluginId = pluginId;

            // The bug: on the second save, INSERT OR REPLACE on plugins
            // triggers an implicit DELETE of the existing plugin row, which
            // fails the tracks.plugin_id foreign key. Before the clearAllData
            // reorder the whole transaction rolled back silently.
            for (int cycle = 0; cycle < 3; ++cycle) {
                PersistenceLayer p;
                p.open(db.path().toStdString());
                expect(p.saveFrom(seed));
            }

            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }
            expect(loaded.currentSong() != nullptr);
            if (auto* s = loaded.currentSong())
                expectEquals((int)s->tracks.size(), 1);
        }

        beginTest("createDefaultSong → save → relaunch preserves tracks (FK regression)");
        {
            TempDB db;

            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                // DLS must be in the catalog for createDefaultSong to set up
                // the Electric Piano track. In production the audio engine
                // registers it during scan; here we do it by hand.
                coord.state().registerPlugin("DLSMusicDevice", "Apple", "AudioUnit", true);
                auto songId = coord.createDefaultSong("Untitled");
                expect(!songId.empty());
                coord.save();
                // coord dtor runs shutdown() which saves again. Both paths
                // used to silently fail on FK because createDefaultSong was
                // passing the plugin NAME as the preset UUID, which
                // referenced no preset row → FK constraint violation.
            }

            // Reopen and verify the song + its tracks actually made it
            // through the round-trip.
            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            auto* song = loaded.currentSong();
            expect(song != nullptr);
            if (song) {
                expectEquals(song->name, std::string("Untitled"));
                // Action + Electric Piano + Audio In = at least 3 tracks
                expect((int)song->tracks.size() >= 2);
            }
        }

        beginTest("Save failure rolls back, returns false, preserves prior state");
        {
            TempDB db;

            // First, a clean save so the DB has a known-good prior state.
            {
                StateAPI s;
                auto songId = s.createSong("Good");
                s.setCurrentSong(songId);
                s.createTrack("Keys");
                PersistenceLayer p;
                p.open(db.path().toStdString());
                expect(p.saveFrom(s));
            }

            // Now construct a state that will fail the save: put
                // current_song_id in the config map AND also set the canonical
                // currentSongId. On save both will try to INSERT the same key
                // into the config table — UNIQUE constraint violation.
            {
                StateAPI s;
                auto songId = s.createSong("Broken");
                s.setCurrentSong(songId);
                s.createTrack("A");
                s.createTrack("B");
                s.setConfig("current_song_id", songId.str());  // the poison pill

                PersistenceLayer p;
                p.open(db.path().toStdString());
                expect(!p.saveFrom(s));  // returns false
            }

            // Reopen — should see the first save's state ("Good" with "Keys"),
                // not the failed save's state.
            StateAPI loaded;
            { PersistenceLayer p; p.open(db.path().toStdString()); p.loadInto(loaded); }

            expectEquals((int)loaded.allSongs().size(), 1);
            if (!loaded.allSongs().empty()) {
                expectEquals(loaded.allSongs()[0].name, std::string("Good"));
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
                    region.id = RegionId{StateAPI::generateId()};
                    region.startBeat = 0.0;
                    region.lengthBeats = 4.0;
                    TakeState take;
                    take.id = TakeId{StateAPI::generateId()};
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
// Action algebra tests
// ============================================================================

#include "state/ActionAlgebra.h"

class ActionAlgebraTests : public juce::UnitTest {
public:
    ActionAlgebraTests() : UnitTest("ActionAlgebra", "Performance") {}

    void runTest() override {
        using namespace ActionAlgebra;

        beginTest("Builders construct expected nodes");
        {
            auto n = set({ Target::Kind::MasterGain, {}, -1 }, num(0.5));
            expect(n.op == ActionNode::Op::Set);
            expect(n.target.kind == Target::Kind::MasterGain);
            expect(n.to == num(0.5));

            auto i = interpolate({ Target::Kind::TrackGain, "T", -1 },
                                 captureCurrent(), num(0), num(3.0), "easein");
            expect(i.op == ActionNode::Op::Interpolate);
            expect(i.from == captureCurrent());
            expect(i.easing == "easein");
        }

        beginTest("Round-trip — flat ops");
        auto roundTrip = [this](const ActionNode& n) {
            auto json = toJson(n);
            auto back = fromJson(json);
            expect(n == back);
        };
        {
            roundTrip(set({ Target::Kind::MasterGain, {}, -1 }, num(0.5)));
            roundTrip(interpolate({ Target::Kind::TrackGain, "T", -1 },
                                   captureCurrent(), num(0), num(3.0), "easein"));
            roundTrip(delay(num(2.5), set({ Target::Kind::TrackGain, "T", -1 }, num(0))));
            roundTrip(lua("setTrackGain(args[1], 0)"));
        }

        beginTest("Round-trip — nested composites");
        {
            auto fade = interpolate({ Target::Kind::TrackGain, "A", -1 },
                                     captureCurrent(), num(0), num(3.0), "easein");
            auto fadeUp = interpolate({ Target::Kind::TrackGain, "B", -1 },
                                       captureCurrent(), num(1), num(3.0), "easein");
            roundTrip(parallel({ fade, fadeUp }));
            roundTrip(sequence({ fade, delay(num(1.0), fadeUp) }));
        }

        beginTest("Round-trip — placeholders");
        {
            auto t = interpolate({ Target::Kind::TrackGain, "T", -1 },
                                 captureCurrent(), num(0),
                                 placeholder("duration"),
                                 "easein");
            roundTrip(t);
            auto json = toJson(t);
            // Placeholder must stringify as the {"$": name} object form, not a bare string.
            expect(json.find("\"$\": \"duration\"") != std::string::npos);
        }

        beginTest("Round-trip — invoke");
        {
            auto inv = invoke("fadeOut", { placeholder("track"), num(3.0), placeholder("easing") });
            roundTrip(inv);
        }

        beginTest("Substitution replaces placeholders in all slots");
        {
            auto tree = interpolate({ Target::Kind::TrackGain, "T", -1 },
                                    captureCurrent(), num(0),
                                    placeholder("dur"), "easein");
            std::unordered_map<std::string, Value> bindings {
                { "dur", num(5.0) }
            };
            auto result = substitute(tree, bindings);
            expect(result.duration == num(5.0));
            // Other fields untouched
            expect(result.from == captureCurrent());
        }

        beginTest("Substitution recurses into children");
        {
            auto child = interpolate({ Target::Kind::TrackGain, "T", -1 },
                                     captureCurrent(), num(0),
                                     placeholder("dur"), "easein");
            auto tree = delay(placeholder("wait"), child);
            std::unordered_map<std::string, Value> bindings {
                { "wait", num(2.0) },
                { "dur",  num(4.0) }
            };
            auto result = substitute(tree, bindings);
            expect(result.duration == num(2.0));
            expect(result.children.size() == 1);
            expect(result.children[0].duration == num(4.0));
        }

        beginTest("Substitution replaces invoke args");
        {
            auto tree = invoke("fadeOut", { placeholder("t"), num(1), placeholder("e") });
            std::unordered_map<std::string, Value> bindings {
                { "t", num(42.0) },
                { "e", num(7.0) }
            };
            auto result = substitute(tree, bindings);
            expect(result.invokeArgs.size() == 3);
            expect(result.invokeArgs[0] == num(42.0));
            expect(result.invokeArgs[1] == num(1));
            expect(result.invokeArgs[2] == num(7.0));
        }

        beginTest("Substitution replaces $name sigil in Target.entityId");
        {
            auto tree = interpolate({ Target::Kind::TrackGain, "$track", -1 },
                                     captureCurrent(), num(0), num(3.0), "linear");
            std::unordered_map<std::string, Value> bindings {
                { "track", text("uuid-abc") }
            };
            auto result = substitute(tree, bindings);
            expect(result.target.entityId == "uuid-abc");
        }

        beginTest("Text value round-trips through JSON as bare string");
        {
            auto v = text("some-uuid");
            auto tree = set({ Target::Kind::Selection, {}, -1 }, v);
            auto back = fromJson(toJson(tree));
            expect(tree == back);
            expect(back.to.kind == Value::Kind::Text);
            expect(back.to.text == "some-uuid");
        }

        beginTest("Unbound placeholder passes through unchanged");
        {
            auto tree = interpolate({ Target::Kind::TrackGain, "T", -1 },
                                    captureCurrent(), num(0),
                                    placeholder("dur"), "easein");
            std::unordered_map<std::string, Value> empty;
            auto result = substitute(tree, empty);
            expect(result.duration.kind == Value::Kind::Placeholder);
            expect(result.duration.placeholder == "dur");
        }
    }
};

static ActionAlgebraTests actionAlgebraTests;

// ============================================================================
// Action interpreter tests (MockScheduler + MockTargetIO)
// ============================================================================

#include "state/ActionInterpreter.h"

class ActionInterpreterTests : public juce::UnitTest {
public:
    ActionInterpreterTests() : UnitTest("ActionInterpreter", "Performance") {}

    // A scheduler that never ticks on its own — the test calls complete()
    // explicitly to advance. Records what was scheduled in order.
    struct MockScheduler : ActionAlgebra::ActionInterpreter::Scheduler {
        struct Call {
            enum class Kind { Interpolate, Delay };
            Kind kind;
            float from, to, duration;
            std::string easing;
            std::function<void(float)> onTick;
            std::function<void()> onComplete;
        };
        std::vector<Call> calls;

        void interpolate(float from, float to, float dur,
                         std::function<void(float)> onTick,
                         std::function<void()> onComplete,
                         const std::string& easing) override {
            calls.push_back({ Call::Kind::Interpolate, from, to, dur, easing,
                              std::move(onTick), std::move(onComplete) });
        }
        void delay(float dur, std::function<void()> onComplete) override {
            calls.push_back({ Call::Kind::Delay, 0, 0, dur, {}, {}, std::move(onComplete) });
        }

        void complete(size_t idx) {
            if (idx >= calls.size()) return;
            auto& c = calls[idx];
            if (c.kind == Call::Kind::Interpolate && c.onTick) c.onTick(c.to);
            if (c.onComplete) c.onComplete();
        }
    };

    struct MockIO : ActionAlgebra::ActionInterpreter::TargetIO {
        std::map<std::string, float> values;
        static std::string key(const ActionAlgebra::Target& t) {
            return std::to_string((int)t.kind) + ":" + t.entityId + ":"
                 + std::to_string(t.paramIndex);
        }
        float read(const ActionAlgebra::Target& t) override {
            auto it = values.find(key(t));
            return it != values.end() ? it->second : 0.0f;
        }
        void write(const ActionAlgebra::Target& t, float v) override {
            values[key(t)] = v;
        }
    };

    // Mock template resolver for testing Invoke/trigger.
    struct MockResolver : ActionAlgebra::ActionInterpreter::TemplateResolver {
        std::map<std::string, Template> templates;
        const Template* lookup(const std::string& name) override {
            auto it = templates.find(name);
            return it != templates.end() ? &it->second : nullptr;
        }
    };

    void runTest() override {
        using namespace ActionAlgebra;

        beginTest("Set writes immediately and fires onComplete synchronously");
        {
            MockScheduler s; MockIO io;
            ActionInterpreter interp(s, io);
            bool done = false;
            interp.run(set({ Target::Kind::MasterGain, {}, -1 }, num(0.5f)),
                       [&]() { done = true; });
            expect(done);
            expect(s.calls.empty());
            expectWithinAbsoluteError(
                io.values[MockIO::key({ Target::Kind::MasterGain, {}, -1 })], 0.5f, 0.0001f);
        }

        beginTest("Interpolate schedules with scheduler, onComplete fires on completion");
        {
            MockScheduler s; MockIO io;
            ActionInterpreter interp(s, io);
            // Pre-seed the current value so CaptureCurrent reads 0.8
            Target target { Target::Kind::TrackGain, "T", -1 };
            io.values[MockIO::key(target)] = 0.8f;

            bool done = false;
            interp.run(interpolate(target, captureCurrent(), num(0.0f), num(3.0f), "easein"),
                       [&]() { done = true; });
            expect((int)s.calls.size() == 1);
            expect(s.calls[0].kind == MockScheduler::Call::Kind::Interpolate);
            expectWithinAbsoluteError(s.calls[0].from, 0.8f, 0.0001f);
            expectWithinAbsoluteError(s.calls[0].to, 0.0f, 0.0001f);
            expectWithinAbsoluteError(s.calls[0].duration, 3.0f, 0.0001f);
            expect(s.calls[0].easing == "easein");
            expect(!done);
            s.complete(0);
            expect(done);
            // Last tick wrote 'to' to the target
            expectWithinAbsoluteError(io.values[MockIO::key(target)], 0.0f, 0.0001f);
        }

        beginTest("Delay schedules, completes after child completes");
        {
            MockScheduler s; MockIO io;
            ActionInterpreter interp(s, io);
            Target target { Target::Kind::MasterGain, {}, -1 };
            bool done = false;
            interp.run(delay(num(2.0f), set(target, num(1.0f))),
                       [&]() { done = true; });
            expect((int)s.calls.size() == 1);
            expect(s.calls[0].kind == MockScheduler::Call::Kind::Delay);
            expect(!done);
            // Firing the delay's completion runs the child (Set writes + completes).
            s.complete(0);
            expect(done);
            expectWithinAbsoluteError(io.values[MockIO::key(target)], 1.0f, 0.0001f);
        }

        beginTest("Parallel runs all children, completes when all done");
        {
            MockScheduler s; MockIO io;
            ActionInterpreter interp(s, io);
            Target a { Target::Kind::TrackGain, "A", -1 };
            Target b { Target::Kind::TrackGain, "B", -1 };
            bool done = false;
            interp.run(parallel({
                interpolate(a, num(0), num(1), num(3), "linear"),
                interpolate(b, num(1), num(0), num(3), "linear"),
            }), [&]() { done = true; });
            expect((int)s.calls.size() == 2);
            expect(!done);
            s.complete(0);
            expect(!done);  // still waiting on second
            s.complete(1);
            expect(done);
        }

        beginTest("Sequence runs children one after another");
        {
            MockScheduler s; MockIO io;
            ActionInterpreter interp(s, io);
            Target a { Target::Kind::TrackGain, "A", -1 };
            bool done = false;
            interp.run(sequence({
                interpolate(a, num(0), num(1), num(2), "linear"),
                interpolate(a, num(1), num(0), num(2), "linear"),
            }), [&]() { done = true; });
            // Only the first interpolate should be scheduled initially.
            expect((int)s.calls.size() == 1);
            s.complete(0);
            expect(!done);
            // Completing the first triggers the second.
            expect((int)s.calls.size() == 2);
            s.complete(1);
            expect(done);
        }

        beginTest("Empty Parallel + empty Sequence complete immediately");
        {
            MockScheduler s; MockIO io;
            ActionInterpreter interp(s, io);
            int completions = 0;
            interp.run(parallel({}), [&]() { ++completions; });
            interp.run(sequence({}), [&]() { ++completions; });
            expect(completions == 2);
            expect(s.calls.empty());
        }

        beginTest("trigger binds positional args and $value into template");
        {
            MockScheduler s; MockIO io;
            // A fadeOut-shaped template: interpolate(track.gain, current,
            // 0, duration, easing). Params named "track" and "duration".
            MockResolver r;
            r.templates["fadeOut"] = {
                interpolate({ Target::Kind::TrackGain, "PLACEHOLDER", -1 },
                            captureCurrent(), num(0),
                            placeholder("duration"), "easein"),
                { "track", "duration" }
            };
            // Note: the template's target.entityId would normally be a
            // placeholder too. For step 3 we only test Value-slot
            // substitution; target-entity substitution lands when we
            // move the real built-ins over in step 4. Here we verify the
            // duration placeholder gets bound.

            ActionInterpreter interp(s, io, &r);
            Target target { Target::Kind::TrackGain, "PLACEHOLDER", -1 };
            io.values[MockIO::key(target)] = 0.8f;

            interp.trigger("fadeOut",
                { num(0 /*ignored — no target-entity substitution yet*/),
                  num(5.0 /*duration*/) },
                1.0f);

            expect((int)s.calls.size() == 1);
            expect(s.calls[0].kind == MockScheduler::Call::Kind::Interpolate);
            expectWithinAbsoluteError(s.calls[0].duration, 5.0f, 0.0001f);
            expectWithinAbsoluteError(s.calls[0].from, 0.8f, 0.0001f);
            expectWithinAbsoluteError(s.calls[0].to, 0.0f, 0.0001f);
        }

        beginTest("Invoke inside tree expands recursively");
        {
            MockScheduler s; MockIO io;
            MockResolver r;
            // fadeOut template: one interpolate with duration placeholder
            r.templates["fadeOut"] = {
                interpolate({ Target::Kind::TrackGain, "X", -1 },
                            captureCurrent(), num(0),
                            placeholder("duration"), "linear"),
                { "duration" }
            };
            ActionInterpreter interp(s, io, &r);

            // Sequence containing one Invoke — should expand and schedule
            bool done = false;
            interp.run(sequence({ invoke("fadeOut", { num(2.0) }) }),
                       [&]() { done = true; });
            expect((int)s.calls.size() == 1);
            expectWithinAbsoluteError(s.calls[0].duration, 2.0f, 0.0001f);
            expect(!done);
            s.complete(0);
            expect(done);
        }

        beginTest("Unknown action invocation completes without crash");
        {
            MockScheduler s; MockIO io;
            MockResolver r;  // empty
            ActionInterpreter interp(s, io, &r);
            bool done = false;
            interp.trigger("nonexistent", {}, 1.0f, [&]() { done = true; });
            expect(done);
            expect(s.calls.empty());
        }

        beginTest("$value is bound as a placeholder during trigger");
        {
            MockScheduler s; MockIO io;
            // trackVolume-shaped template: Set(MasterGain, $value * ... )
            // Simplified: Set(MasterGain, $value). We should observe the
            // MIDI value written to MasterGain.
            MockResolver r;
            r.templates["trackVolume"] = {
                set({ Target::Kind::MasterGain, {}, -1 }, placeholder("value")),
                {}  // no schema args
            };
            ActionInterpreter interp(s, io, &r);
            interp.trigger("trackVolume", {}, 0.42f);
            expectWithinAbsoluteError(
                io.values[MockIO::key({ Target::Kind::MasterGain, {}, -1 })],
                0.42f, 0.0001f);
        }
    }
};

static ActionInterpreterTests actionInterpreterTests;

// ============================================================================
// BundledPluginInstaller tests
// ============================================================================
//
// The installer's network / zip / codesign paths need real integration
// (live S3 bucket, Apple notarization, real .component bundles on disk)
// and are covered by `scripts/bundled-plugins/*.sh` + manual install
// verification. These tests cover the pure-ish local-manifest logic:
// read / uninstallArchive / uninstallAll. The test hook
// `setInstallManifestFileForTests` redirects the manifest path to a
// temp file so we don't touch the real install state.

class BundledPluginInstallerTests : public juce::UnitTest {
public:
    BundledPluginInstallerTests() : juce::UnitTest("BundledPluginInstaller") {}

private:
    struct TempManifest {
        juce::File tempDir;
        juce::File manifestFile;

        TempManifest() {
            tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("perf-installer-test-" + juce::Uuid().toString());
            tempDir.createDirectory();
            manifestFile = tempDir.getChildFile("plugins-installed.json");
            BundledPluginInstaller::setInstallManifestFileForTests(manifestFile);
        }
        ~TempManifest() {
            BundledPluginInstaller::setInstallManifestFileForTests({});
            tempDir.deleteRecursively();
        }
    };

    // Create a dummy .component-style directory at `path` so uninstall
    // can actually delete something. Content doesn't matter — just need
    // the path to exist.
    static juce::File makeFakeBundle(const juce::File& parent, const juce::String& name) {
        auto bundle = parent.getChildFile(name);
        bundle.createDirectory();
        bundle.getChildFile("Contents").createDirectory();
        bundle.getChildFile("Contents/Info.plist").replaceWithText("<plist/>");
        return bundle;
    }

    // Write a manifest that claims the given bundles are installed.
    static void writeManifest(const juce::File& manifestFile,
                               const std::vector<std::pair<juce::String, juce::StringArray>>& archiveToBundles) {
        juce::DynamicObject::Ptr root(new juce::DynamicObject());
        root->setProperty("version", 1);
        root->setProperty("installedAt", juce::Time::getCurrentTime().toISO8601(true));

        juce::Array<juce::var> archiveArray;
        for (auto& [slug, paths] : archiveToBundles) {
            juce::DynamicObject::Ptr obj(new juce::DynamicObject());
            obj->setProperty("slug", slug);
            obj->setProperty("version", "test-1.0");
            juce::Array<juce::var> pathVars;
            for (auto& p : paths) pathVars.add(p);
            obj->setProperty("installedPaths", pathVars);
            archiveArray.add(juce::var(obj.get()));
        }
        root->setProperty("archives", archiveArray);
        manifestFile.replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
    }

public:
    void runTest() override {
        beginTest("readInstalledManifest returns empty when no file exists");
        {
            TempManifest fx;
            auto entries = BundledPluginInstaller::readInstalledManifest();
            expect(entries.empty());
            expect(!BundledPluginInstaller::isInstalled());
        }

        beginTest("readInstalledManifest round-trips archives and installed paths");
        {
            TempManifest fx;
            auto mda1 = makeFakeBundle(fx.tempDir, "mda ePiano.component");
            auto mda2 = makeFakeBundle(fx.tempDir, "mda JX10.component");
            auto dex  = makeFakeBundle(fx.tempDir, "Dexed.component");

            writeManifest(fx.manifestFile, {
                {"mda-suite", { mda1.getFullPathName(), mda2.getFullPathName() }},
                {"dexed",     { dex.getFullPathName() }},
            });

            auto entries = BundledPluginInstaller::readInstalledManifest();
            expectEquals((int) entries.size(), 2);
            expect(BundledPluginInstaller::isInstalled());

            // Order is insertion order per writeManifest.
            expectEquals(juce::String(entries[0].slug), juce::String("mda-suite"));
            expectEquals((int) entries[0].installedPaths.size(), 2);
            expectEquals(juce::String(entries[1].slug), juce::String("dexed"));
            expectEquals((int) entries[1].installedPaths.size(), 1);
        }

        beginTest("uninstallArchive removes bundles + entry, leaves others");
        {
            TempManifest fx;
            auto mda1 = makeFakeBundle(fx.tempDir, "mda ePiano.component");
            auto mda2 = makeFakeBundle(fx.tempDir, "mda JX10.component");
            auto dex  = makeFakeBundle(fx.tempDir, "Dexed.component");

            writeManifest(fx.manifestFile, {
                {"mda-suite", { mda1.getFullPathName(), mda2.getFullPathName() }},
                {"dexed",     { dex.getFullPathName() }},
            });

            int removed = BundledPluginInstaller::uninstallArchive("mda-suite");
            expectEquals(removed, 2);

            // mda bundles gone, dexed still there.
            expect(!mda1.exists());
            expect(!mda2.exists());
            expect(dex.exists());

            // Manifest now only has dexed.
            auto entries = BundledPluginInstaller::readInstalledManifest();
            expectEquals((int) entries.size(), 1);
            expectEquals(juce::String(entries[0].slug), juce::String("dexed"));
        }

        beginTest("uninstallArchive on last archive deletes the manifest file");
        {
            TempManifest fx;
            auto dex = makeFakeBundle(fx.tempDir, "Dexed.component");
            writeManifest(fx.manifestFile, { {"dexed", { dex.getFullPathName() }} });

            expect(BundledPluginInstaller::isInstalled());
            int removed = BundledPluginInstaller::uninstallArchive("dexed");
            expectEquals(removed, 1);
            expect(!dex.exists());
            expect(!BundledPluginInstaller::isInstalled());
            expect(!fx.manifestFile.existsAsFile());
        }

        beginTest("uninstallArchive with unknown slug is a no-op");
        {
            TempManifest fx;
            auto dex = makeFakeBundle(fx.tempDir, "Dexed.component");
            writeManifest(fx.manifestFile, { {"dexed", { dex.getFullPathName() }} });

            int removed = BundledPluginInstaller::uninstallArchive("nonexistent");
            expectEquals(removed, 0);
            expect(dex.exists());
            expect(fx.manifestFile.existsAsFile());
            auto entries = BundledPluginInstaller::readInstalledManifest();
            expectEquals((int) entries.size(), 1);
        }

        beginTest("uninstallAll removes every bundle and deletes the manifest");
        {
            TempManifest fx;
            auto a = makeFakeBundle(fx.tempDir, "a.component");
            auto b = makeFakeBundle(fx.tempDir, "b.component");
            auto c = makeFakeBundle(fx.tempDir, "c.component");

            writeManifest(fx.manifestFile, {
                {"first",  { a.getFullPathName() }},
                {"second", { b.getFullPathName(), c.getFullPathName() }},
            });

            int removed = BundledPluginInstaller::uninstallAll();
            expectEquals(removed, 3);
            expect(!a.exists());
            expect(!b.exists());
            expect(!c.exists());
            expect(!BundledPluginInstaller::isInstalled());
            expect(!fx.manifestFile.existsAsFile());
        }

        beginTest("uninstall tolerates missing on-disk paths");
        {
            TempManifest fx;
            // Manifest claims these bundles are installed, but they
            // don't actually exist on disk (user deleted them in Finder
            // between sessions). Uninstall should still clear the
            // manifest without error.
            writeManifest(fx.manifestFile, {
                {"ghost", { fx.tempDir.getChildFile("never-was.component").getFullPathName() }},
            });

            int removed = BundledPluginInstaller::uninstallArchive("ghost");
            expectEquals(removed, 0);  // nothing actually deleted
            expect(!BundledPluginInstaller::isInstalled());
        }
    }
};

static BundledPluginInstallerTests bundledPluginInstallerTests;

// ============================================================================
// V2NotationParser tests
// ============================================================================

class V2NotationParserTests : public juce::UnitTest {
public:
    V2NotationParserTests() : juce::UnitTest("V2NotationParser") {}

private:
    static ComposerOutput parseOrFail(juce::UnitTest& t, const juce::String& input) {
        V2NotationParser p;
        ComposerOutput out;
        std::string err;
        bool ok = p.parse(input, out, err);
        if (!ok) t.logMessage("parse failed: " + juce::String(err));
        t.expect(ok);
        return out;
    }

public:
    void runTest() override {
        beginTest("empty / invalid input returns error");
        {
            V2NotationParser p;
            ComposerOutput out;
            std::string err;
            expect(!p.parse("", out, err));
            expect(!err.empty());
        }

        beginTest("metadata parses (tempo, time signature)");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 92
time_signature: 3/4
key: D minor
feel: straight
tracks:
  Piano: 0
)"));
            expectEquals(out.tempo, 92.0);
            expectEquals((int) out.timeSignature.size(), 1);
            expectEquals(out.timeSignature[0].first, 3);
            expectEquals(out.timeSignature[0].second, 4);
        }

        beginTest("single note parses (pitch, beat, duration, velocity)");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 1 C4 q mf
)"));
            expectEquals((int) out.notes.size(), 1);
            auto& n = out.notes[0];
            expectEquals(juce::String(n.trackName), juce::String("Piano"));
            expectEquals(n.startBeat, 0.0);
            expectEquals(n.durationBeats, 1.0);
            expectEquals(n.pitch, 60);                 // C4 = MIDI 60
            expectWithinAbsoluteError(n.velocity, 80.0f / 127.0f, 0.001f);
        }

        beginTest("offbeat + position parses as half-beat offset");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 2+ E4 8th mf
)"));
            expectEquals((int) out.notes.size(), 1);
            expectEquals(out.notes[0].startBeat, 1.5);  // beat 2 + half → offset 1.5
            expectEquals(out.notes[0].durationBeats, 0.5);
            expectEquals(out.notes[0].pitch, 64);       // E4
        }

        beginTest("chord expands to multiple notes with the same timing");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 1 [C4 E4 G4] h f
)"));
            expectEquals((int) out.notes.size(), 3);
            // All same startBeat + duration + velocity; pitches differ.
            for (auto& n : out.notes) {
                expectEquals(n.startBeat, 0.0);
                expectEquals(n.durationBeats, 2.0);
                expectWithinAbsoluteError(n.velocity, 96.0f / 127.0f, 0.001f);
            }
            // Sorted by (trackName, startBeat, pitch) → ascending pitch.
            expectEquals(out.notes[0].pitch, 60);  // C4
            expectEquals(out.notes[1].pitch, 64);  // E4
            expectEquals(out.notes[2].pitch, 67);  // G4
        }

        beginTest("bar numbering offsets startBeat correctly");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 1 C4 q mf

bar 3 | G
  Piano: beat 1 D4 q mf
)"));
            expectEquals((int) out.notes.size(), 2);
            expectEquals(out.notes[0].startBeat, 0.0);   // bar 1 beat 1
            expectEquals(out.notes[1].startBeat, 8.0);   // bar 3 beat 1 (2 bars * 4 beats)
        }

        beginTest("multiple events on one voice line split on |");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Bass: 33

bar 1 | C
  Bass: beat 1 C2 q mf | beat 2 D2 q mf | beat 3 E2 q mf | beat 4 F2 q mf
)"));
            expectEquals((int) out.notes.size(), 4);
            expectEquals(out.notes[0].pitch, 36);        // C2
            expectEquals(out.notes[3].pitch, 41);        // F2
            expectEquals(out.notes[0].startBeat, 0.0);
            expectEquals(out.notes[1].startBeat, 1.0);
            expectEquals(out.notes[2].startBeat, 2.0);
            expectEquals(out.notes[3].startBeat, 3.0);
        }

        beginTest("drums track uses drum-name → pitch mapping");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Kit: drums

bar 1 | -
  Kit: beat 1 kick q mf | beat 2 snare q mf | beat 3 hhc q mf
)"));
            expectEquals((int) out.notes.size(), 3);
            expectEquals(out.notes[0].pitch, 36);  // kick
            expectEquals(out.notes[1].pitch, 38);  // snare
            expectEquals(out.notes[2].pitch, 42);  // hhc
        }

        beginTest("multiple tracks, bar-grouped");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0
  Bass: 33

bar 1 | C
  Piano: beat 1 C4 q mf
  Bass: beat 1 C2 q mf
)"));
            expectEquals((int) out.notes.size(), 2);
            // Sorted by trackName — Bass < Piano alphabetically.
            expectEquals(juce::String(out.notes[0].trackName), juce::String("Bass"));
            expectEquals(juce::String(out.notes[1].trackName), juce::String("Piano"));
        }

        beginTest("sharps and flats parse correctly");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 1 C#4 8th mf | beat 2 Bb3 8th mf
)"));
            expectEquals((int) out.notes.size(), 2);
            // sorted by pitch ascending within same startBeat → but here
            // startBeats differ, so order is by startBeat.
            expectEquals(out.notes[0].pitch, 61);  // C#4
            expectEquals(out.notes[1].pitch, 58);  // Bb3
        }

        beginTest("rest is skipped, not emitted");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 1 C4 q mf | beat 2 r q mf | beat 3 D4 q mf
)"));
            // Rest is not a note.
            expectEquals((int) out.notes.size(), 2);
            expectEquals(out.notes[0].pitch, 60);
            expectEquals(out.notes[1].pitch, 62);
        }

        beginTest("lengthBeats reflects bar count");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 1 C4 q mf

bar 2 | C
  Piano: beat 1 C4 q mf
)"));
            expectEquals(out.lengthBeats, 8.0);  // 2 bars × 4 beats/bar
        }

        beginTest("lengthBeats extends to cover notes whose tail runs past last bar");
        {
            // Half note on beat 4 of bar 2 = startBeat 7, duration 2 → ends at beat 9.
            // Region length should grow from 8 (2 bars * 4) to 9.
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 1 C4 q mf

bar 2 | C
  Piano: beat 4 C4 h mf
)"));
            expectEquals(out.lengthBeats, 9.0);
        }

        beginTest("swing shifts offbeat eighths to triplet grid");
        {
            auto out = parseOrFail(*this, juce::String(R"(tempo: 120
time_signature: 4/4
feel: swing
tracks:
  Piano: 0

bar 1 | C
  Piano: beat 1 C4 8th mf | beat 1+ D4 8th mf
)"));
            expectEquals((int) out.notes.size(), 2);
            expectEquals(out.notes[0].startBeat, 0.0);
            // 1+ is half-beat offset; swing pushes it to 2/3 of a beat.
            expectWithinAbsoluteError(out.notes[1].startBeat, 2.0 / 3.0, 0.001);
        }
    }
};

static V2NotationParserTests v2NotationParserTests;


// ============================================================================
// ABCParser tests
// ============================================================================

class ABCParserTests : public juce::UnitTest {
public:
    ABCParserTests() : juce::UnitTest("ABCParser") {}

private:
    static ComposerOutput parseOrFail(juce::UnitTest& t, const juce::String& input) {
        ABCParser p;
        ComposerOutput out;
        std::string err;
        bool ok = p.parse(input, out, err);
        if (!ok) t.logMessage("parse failed: " + juce::String(err));
        t.expect(ok);
        return out;
    }

public:
    void runTest() override {
        beginTest("missing K: header is rejected");
        {
            ABCParser p;
            ComposerOutput out;
            std::string err;
            expect(!p.parse("X:1\nL:1/8\nQ:1/4=120\nM:4/4\nC D E F\n", out, err));
            expect(err.find("K:") != std::string::npos);
        }

        beginTest("headers parse (tempo, meter, key)");
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
T:Test
L:1/8
Q:1/4=92
M:3/4
K:Dmin
)"));
            expectEquals(out.tempo, 92.0);
            expectEquals((int) out.timeSignature.size(), 1);
            expectEquals(out.timeSignature[0].first, 3);
            expectEquals(out.timeSignature[0].second, 4);
        }

        beginTest("single note parses (pitch, start, duration)");
        {
            // L:1/8, so bare 'C' = eighth note (0.5 beats), 'C2' = quarter (1.0 beats).
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/8
Q:1/4=120
M:4/4
K:none
C2
)"));
            expectEquals((int) out.notes.size(), 1);
            expectEquals(out.notes[0].startBeat, 0.0);
            expectEquals(out.notes[0].durationBeats, 1.0);
            expectEquals(out.notes[0].pitch, 60);   // middle C
        }

        beginTest("octave markers (lowercase, ', ,)");
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/4
Q:1/4=120
M:4/4
K:none
C c c' C,
)"));
            expectEquals((int) out.notes.size(), 4);
            expectEquals(out.notes[0].pitch, 60);  // C  = C4
            expectEquals(out.notes[1].pitch, 72);  // c  = C5
            expectEquals(out.notes[2].pitch, 84);  // c' = C6
            expectEquals(out.notes[3].pitch, 48);  // C, = C3
        }

        beginTest("accidentals (^ and _)");
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/4
Q:1/4=120
M:4/4
K:none
^C _D =E
)"));
            expectEquals((int) out.notes.size(), 3);
            expectEquals(out.notes[0].pitch, 61);  // C# = 61
            expectEquals(out.notes[1].pitch, 61);  // Db = 61
            expectEquals(out.notes[2].pitch, 64);  // E natural = 64
        }

        beginTest("chord [CEG] expands to multiple notes at same beat");
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/4
Q:1/4=120
M:4/4
K:none
[CEG]2
)"));
            expectEquals((int) out.notes.size(), 3);
            for (auto& n : out.notes) {
                expectEquals(n.startBeat, 0.0);
                expectEquals(n.durationBeats, 2.0);
            }
            // Pitches sort to C E G.
            expectEquals(out.notes[0].pitch, 60);
            expectEquals(out.notes[1].pitch, 64);
            expectEquals(out.notes[2].pitch, 67);
        }

        beginTest("rests advance cursor without emitting notes");
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/4
Q:1/4=120
M:4/4
K:none
z C z C
)"));
            expectEquals((int) out.notes.size(), 2);
            expectEquals(out.notes[0].startBeat, 1.0);
            expectEquals(out.notes[1].startBeat, 3.0);
        }

        beginTest("ties merge tied notes of same pitch");
        {
            // Two C2 notes tied = single quarter (in L:1/8, 2 = 2 eighths).
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/8
Q:1/4=120
M:4/4
K:none
C2-C2
)"));
            expectEquals((int) out.notes.size(), 1);
            expectEquals(out.notes[0].pitch, 60);
            expectEquals(out.notes[0].durationBeats, 2.0);
        }

        beginTest("multi-voice (V:Piano, V:Drums)");
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/4
Q:1/4=120
M:4/4
K:none
%%MIDI drummap B 36
%%MIDI drummap S 38
V:Piano
V:Drums
V:Piano
C D E F
V:Drums
B S B S
)"));
            // 4 piano + 4 drum
            expectEquals((int) out.notes.size(), 8);
            int pianoCount = 0, drumCount = 0;
            for (auto& n : out.notes) {
                if (n.trackName == "Piano") ++pianoCount;
                if (n.trackName == "Drums") ++drumCount;
            }
            expectEquals(pianoCount, 4);
            expectEquals(drumCount, 4);
        }

        beginTest("inline header [Q:...] is rejected");
        {
            ABCParser p;
            ComposerOutput out;
            std::string err;
            expect(!p.parse(juce::String(R"(X:1
L:1/8
Q:1/4=120
M:4/4
K:none
C2 [Q:1/4=140] D2
)"), out, err));
            expect(err.find("Q") != std::string::npos);
        }

        beginTest("comment lines and inline %") ;
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/4
Q:1/4=120
M:4/4
K:none
% leading comment
C D % trailing comment
E F
)"));
            expectEquals((int) out.notes.size(), 4);
        }

        beginTest("decoration tokens (!ff!, +p+) are skipped silently");
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/4
Q:1/4=120
M:4/4
K:none
"text" C "more text" D
)"));
            expectEquals((int) out.notes.size(), 2);
        }

        beginTest("dynamics (!ff!, !p!) set sticky velocity");
        {
            auto out = parseOrFail(*this, juce::String(R"(X:1
L:1/4
Q:1/4=120
M:4/4
K:none
C !ff! D !p! E F
)"));
            // C with default mf=80, D with ff=112, E and F with p=50.
            expectEquals((int) out.notes.size(), 4);
            // Sort guarantees C E F D (by pitch within same trackName).
            // Find each by pitch.
            auto findPitch = [&](int p) -> ComposerOutput::Note* {
                for (auto& n : out.notes) if (n.pitch == p) return &n;
                return nullptr;
            };
            auto* nC = findPitch(60);
            auto* nD = findPitch(62);
            auto* nE = findPitch(64);
            auto* nF = findPitch(65);
            expect(nC && nD && nE && nF);
            expectWithinAbsoluteError(nC->velocity, 80.0f / 127.0f, 0.001f);
            expectWithinAbsoluteError(nD->velocity, 112.0f / 127.0f, 0.001f);
            expectWithinAbsoluteError(nE->velocity, 50.0f / 127.0f, 0.001f);
            expectWithinAbsoluteError(nF->velocity, 50.0f / 127.0f, 0.001f);
        }
    }
};

static ABCParserTests abcParserTests;

// ============================================================================
// ABCWriter tests
// ============================================================================

class ABCWriterTests : public juce::UnitTest {
public:
    ABCWriterTests() : juce::UnitTest("ABCWriter") {}

    void runTest() override {
        beginTest("header order and required fields");
        {
            ABCWriteInput in;
            in.tempo = 120; in.timeSignatureNum = 4; in.timeSignatureDen = 4;
            ABCWriter w;
            auto abc = w.write(in);
            // Header order: X T L Q M K (T optional, no title here).
            auto x = abc.find("X:");
            auto l = abc.find("L:");
            auto q = abc.find("Q:");
            auto m = abc.find("M:");
            auto k = abc.find("K:");
            expect(x < l);
            expect(l < q);
            expect(q < m);
            expect(m < k);
            expect(abc.find("L:1/8") != std::string::npos);
            expect(abc.find("K:none") != std::string::npos);
        }

        beginTest("middle C single note round-trips through parser");
        {
            ABCWriteInput in;
            in.tempo = 120; in.timeSignatureNum = 4; in.timeSignatureDen = 4;
            ABCWriteInput::Voice v; v.name = "Piano";
            v.notes.push_back({0.0, 1.0, 60, 80});  // middle C, quarter
            in.voices.push_back(v);
            in.lengthBeats = 4.0;

            ABCWriter w;
            auto abc = w.write(in);

            ABCParser p;
            ComposerOutput out;
            std::string err;
            expect(p.parse(juce::String(abc), out, err));
            expectEquals((int) out.notes.size(), 1);
            expectEquals(out.notes[0].pitch, 60);
            expectEquals(out.notes[0].durationBeats, 1.0);
        }

        beginTest("chord round-trips");
        {
            ABCWriteInput in;
            in.timeSignatureNum = 4; in.timeSignatureDen = 4;
            ABCWriteInput::Voice v; v.name = "Piano";
            v.notes.push_back({0.0, 2.0, 60, 80});
            v.notes.push_back({0.0, 2.0, 64, 80});
            v.notes.push_back({0.0, 2.0, 67, 80});
            in.voices.push_back(v);
            in.lengthBeats = 4.0;

            ABCWriter w;
            auto abc = w.write(in);
            // Chord syntax should appear.
            expect(abc.find("[") != std::string::npos);

            ABCParser p; ComposerOutput out; std::string err;
            expect(p.parse(juce::String(abc), out, err));
            expectEquals((int) out.notes.size(), 3);
            for (auto& n : out.notes) {
                expectEquals(n.startBeat, 0.0);
                expectEquals(n.durationBeats, 2.0);
            }
        }

        beginTest("note crossing bar line is split with tie");
        {
            // 4/4, half note starting at beat 3 → spans into bar 2.
            ABCWriteInput in;
            in.timeSignatureNum = 4; in.timeSignatureDen = 4;
            ABCWriteInput::Voice v; v.name = "Piano";
            v.notes.push_back({3.0, 2.0, 60, 80});  // starts beat 3, lasts 2 beats
            in.voices.push_back(v);
            in.lengthBeats = 8.0;

            ABCWriter w;
            auto abc = w.write(in);
            expect(abc.find("-") != std::string::npos);   // tie present

            ABCParser p; ComposerOutput out; std::string err;
            expect(p.parse(juce::String(abc), out, err));
            // The tied pair should re-merge into one note (length 2.0).
            expectEquals((int) out.notes.size(), 1);
            expectEquals(out.notes[0].startBeat, 3.0);
            expectEquals(out.notes[0].durationBeats, 2.0);
        }

        beginTest("multi-voice round-trip preserves voices");
        {
            ABCWriteInput in;
            in.timeSignatureNum = 4; in.timeSignatureDen = 4;
            ABCWriteInput::Voice piano; piano.name = "Piano";
            piano.notes.push_back({0.0, 1.0, 60, 80});
            piano.notes.push_back({1.0, 1.0, 62, 80});
            ABCWriteInput::Voice drums; drums.name = "Drums"; drums.isDrums = true;
            drums.notes.push_back({0.0, 0.5, 36, 80});  // kick
            drums.notes.push_back({1.0, 0.5, 38, 80});  // snare
            in.voices.push_back(piano);
            in.voices.push_back(drums);
            in.lengthBeats = 4.0;

            ABCWriter w;
            auto abc = w.write(in);
            expect(abc.find("V:Piano") != std::string::npos);
            expect(abc.find("V:Drums") != std::string::npos);
            expect(abc.find("%%MIDI drummap") != std::string::npos);

            ABCParser p; ComposerOutput out; std::string err;
            bool ok = p.parse(juce::String(abc), out, err);
            if (!ok) logMessage("parse failed: " + juce::String(err));
            expect(ok);
            int pianoCount = 0, drumCount = 0;
            for (auto& n : out.notes) {
                if (n.trackName == "Piano") ++pianoCount;
                if (n.trackName == "Drums") ++drumCount;
            }
            expectEquals(pianoCount, 2);
            expectEquals(drumCount, 2);
        }

        beginTest("dynamics emit on velocity change and round-trip");
        {
            ABCWriteInput in;
            in.timeSignatureNum = 4; in.timeSignatureDen = 4;
            ABCWriteInput::Voice v; v.name = "Piano";
            v.notes.push_back({0.0, 1.0, 60, 80});   // mf
            v.notes.push_back({1.0, 1.0, 62, 112});  // ff
            v.notes.push_back({2.0, 1.0, 64, 50});   // p
            v.notes.push_back({3.0, 1.0, 65, 50});   // still p — no new mark expected
            in.voices.push_back(v);
            in.lengthBeats = 4.0;

            ABCWriter w;
            auto abc = w.write(in);
            // First note is mf (matches default), so no leading mark.
            // Then !ff! before D, !p! before E. F should not re-emit !p!.
            expect(abc.find("!ff!") != std::string::npos);
            expect(abc.find("!p!") != std::string::npos);
            // Crude check: only one !p! occurrence.
            auto firstP = abc.find("!p!");
            expect(firstP != std::string::npos);
            expect(abc.find("!p!", firstP + 1) == std::string::npos);

            ABCParser p; ComposerOutput out; std::string err;
            expect(p.parse(juce::String(abc), out, err));
            expectEquals((int) out.notes.size(), 4);
            // Velocities should round-trip (parser sets 80/112/50/50).
            int vel60 = 0, vel62 = 0, vel64 = 0, vel65 = 0;
            for (auto& n : out.notes) {
                int v127 = static_cast<int>(std::lround(n.velocity * 127.0f));
                if (n.pitch == 60) vel60 = v127;
                if (n.pitch == 62) vel62 = v127;
                if (n.pitch == 64) vel64 = v127;
                if (n.pitch == 65) vel65 = v127;
            }
            expectEquals(vel60, 80);
            expectEquals(vel62, 112);
            expectEquals(vel64, 50);
            expectEquals(vel65, 50);
        }

        beginTest("varying time signature (3/4) round-trips");
        {
            ABCWriteInput in;
            in.tempo = 100; in.timeSignatureNum = 3; in.timeSignatureDen = 4;
            ABCWriteInput::Voice v; v.name = "Piano";
            v.notes.push_back({0.0, 1.0, 60, 80});
            v.notes.push_back({1.0, 1.0, 62, 80});
            v.notes.push_back({2.0, 1.0, 64, 80});
            in.voices.push_back(v);
            in.lengthBeats = 3.0;

            ABCWriter w;
            auto abc = w.write(in);
            expect(abc.find("M:3/4") != std::string::npos);

            ABCParser p; ComposerOutput out; std::string err;
            expect(p.parse(juce::String(abc), out, err));
            expectEquals(out.timeSignature[0].first, 3);
            expectEquals(out.timeSignature[0].second, 4);
            expectEquals((int) out.notes.size(), 3);
        }
    }
};

static ABCWriterTests abcWriterTests;


// ============================================================================
// RegionContent tests (bridge between RegionState <-> ABC text)
// ============================================================================

class RegionContentTests : public juce::UnitTest {
public:
    RegionContentTests() : juce::UnitTest("RegionContent") {}

    void runTest() override {
        beginTest("regionToABC produces ABC with header from project metadata");
        {
            SongState song;
            song.tempoEvents.push_back({0.0, 96.0});
            song.timeSigEvents.push_back({0.0, 3, 4});

            TrackState track;
            track.id   = TrackId{juce::Uuid().toString().toStdString()};
            track.name = "Piano";

            RegionState region;
            region.id          = RegionId{juce::Uuid().toString().toStdString()};
            region.startBeat   = 0.0;
            region.lengthBeats = 6.0;
            TakeState take;
            take.id = TakeId{juce::Uuid().toString().toStdString()};
            // Quarter note C4 at beat 0.
            MidiEventState on;
            on.beatOffset = 0.0; on.status = 0x90; on.channel = 1; on.data1 = 60; on.data2 = 80;
            MidiEventState off;
            off.beatOffset = 1.0; off.status = 0x80; off.channel = 1; off.data1 = 60; off.data2 = 0;
            take.events = {on, off};
            region.takes.push_back(take);
            region.activeTakeId = take.id;

            auto abc = RegionContent::regionToABC(region, track, song);
            expect(abc.find("Q:1/4=96") != std::string::npos);
            expect(abc.find("M:3/4") != std::string::npos);
            expect(abc.find("K:none") != std::string::npos);
        }

        beginTest("regionToABC encodes a drum-named track with drummap");
        {
            SongState song;
            song.tempoEvents.push_back({0.0, 120.0});
            song.timeSigEvents.push_back({0.0, 4, 4});

            TrackState track;
            track.id   = TrackId{juce::Uuid().toString().toStdString()};
            track.name = "Drum Kit";

            RegionState region;
            region.id          = RegionId{juce::Uuid().toString().toStdString()};
            region.startBeat   = 0.0;
            region.lengthBeats = 4.0;
            TakeState take;
            take.id = TakeId{juce::Uuid().toString().toStdString()};
            // Kick at beat 0 (MIDI 36).
            MidiEventState on;
            on.beatOffset = 0.0; on.status = 0x90; on.channel = 10; on.data1 = 36; on.data2 = 100;
            MidiEventState off;
            off.beatOffset = 0.5; off.status = 0x80; off.channel = 10; off.data1 = 36; off.data2 = 0;
            take.events = {on, off};
            region.takes.push_back(take);
            region.activeTakeId = take.id;

            auto abc = RegionContent::regionToABC(region, track, song);
            expect(abc.find("%%MIDI drummap") != std::string::npos);
        }

        beginTest("abcToRegion replaces events and sets length");
        {
            RegionState region;
            region.id          = RegionId{juce::Uuid().toString().toStdString()};
            region.startBeat   = 0.0;
            region.lengthBeats = 4.0;
            TakeState take;
            take.id = TakeId{juce::Uuid().toString().toStdString()};
            // Pre-existing event that should be wiped.
            MidiEventState old;
            old.beatOffset = 0.0; old.status = 0x90; old.channel = 1; old.data1 = 50; old.data2 = 80;
            take.events = {old};
            region.takes.push_back(take);
            region.activeTakeId = take.id;

            std::string abc = R"(X:1
L:1/8
Q:1/4=120
M:4/4
K:none
C2 D2 E2 F2 |
)";
            std::string err;
            expect(RegionContent::abcToRegion(abc, region, err));
            // Active take's events: 4 noteOn + 4 noteOff.
            auto* t = region.activeTake();
            expect(t != nullptr);
            int onCount = 0;
            for (auto& e : t->events) if ((e.status & 0xF0) == 0x90) ++onCount;
            expectEquals(onCount, 4);
            // The pre-existing pitch 50 should not be present.
            for (auto& e : t->events) expect(e.data1 != 50);
            expectGreaterOrEqual(region.lengthBeats, 4.0);
        }

        beginTest("region round-trip preserves note pitches and timings");
        {
            SongState song;
            song.tempoEvents.push_back({0.0, 120.0});
            song.timeSigEvents.push_back({0.0, 4, 4});

            TrackState track;
            track.id   = TrackId{juce::Uuid().toString().toStdString()};
            track.name = "Piano";

            RegionState region;
            region.id          = RegionId{juce::Uuid().toString().toStdString()};
            region.startBeat   = 0.0;
            region.lengthBeats = 4.0;
            TakeState take;
            take.id = TakeId{juce::Uuid().toString().toStdString()};

            // Three notes: C E G at beats 0, 1, 2 (each a quarter).
            for (int i = 0; i < 3; ++i) {
                int pitch = (i == 0 ? 60 : i == 1 ? 64 : 67);
                MidiEventState on;
                on.beatOffset = i * 1.0; on.status = 0x90; on.channel = 1; on.data1 = pitch; on.data2 = 80;
                MidiEventState off;
                off.beatOffset = i * 1.0 + 1.0; off.status = 0x80; off.channel = 1; off.data1 = pitch; off.data2 = 0;
                take.events.push_back(on);
                take.events.push_back(off);
            }
            region.takes.push_back(take);
            region.activeTakeId = take.id;

            auto abc = RegionContent::regionToABC(region, track, song);

            RegionState target;
            target.id          = RegionId{juce::Uuid().toString().toStdString()};
            target.lengthBeats = 4.0;
            TakeState empty;
            empty.id = TakeId{juce::Uuid().toString().toStdString()};
            target.takes.push_back(empty);
            target.activeTakeId = empty.id;

            std::string err;
            expect(RegionContent::abcToRegion(abc, target, err));
            auto* tt = target.activeTake();
            expect(tt != nullptr);

            int found60 = 0, found64 = 0, found67 = 0;
            for (auto& e : tt->events) {
                if ((e.status & 0xF0) != 0x90) continue;
                if (e.data1 == 60) ++found60;
                if (e.data1 == 64) ++found64;
                if (e.data1 == 67) ++found67;
            }
            expectEquals(found60, 1);
            expectEquals(found64, 1);
            expectEquals(found67, 1);
        }
    }
};

static RegionContentTests regionContentTests;


// ============================================================================
// RegionContent track / project view tests (Phase 2b read-only surface)
// ============================================================================

class RegionContentViewTests : public juce::UnitTest {
public:
    RegionContentViewTests() : juce::UnitTest("RegionContentViews") {}

    static RegionState makeRegionWithNotes(double startBeat, double lengthBeats,
                                             const std::vector<int>& pitches) {
        RegionState r;
        r.id          = RegionId{juce::Uuid().toString().toStdString()};
        r.startBeat   = startBeat;
        r.lengthBeats = lengthBeats;
        TakeState take;
        take.id = TakeId{juce::Uuid().toString().toStdString()};
        double cursor = 0.0;
        for (int p : pitches) {
            MidiEventState on;
            on.beatOffset = cursor; on.status = 0x90; on.channel = 1; on.data1 = p; on.data2 = 80;
            MidiEventState off;
            off.beatOffset = cursor + 1.0; off.status = 0x80; off.channel = 1; off.data1 = p; off.data2 = 0;
            take.events.push_back(on);
            take.events.push_back(off);
            cursor += 1.0;
        }
        r.takes.push_back(take);
        r.activeTakeId = take.id;
        return r;
    }

    void runTest() override {
        beginTest("trackToABC emits one P:B<beat> per region in beat order");
        {
            SongState song;
            song.tempoEvents.push_back({0.0, 120.0});
            song.timeSigEvents.push_back({0.0, 4, 4});

            TrackState track;
            track.id   = TrackId{juce::Uuid().toString().toStdString()};
            track.name = "Piano";
            track.regions.push_back(makeRegionWithNotes(0.0, 4.0, {60, 62, 64, 65}));
            track.regions.push_back(makeRegionWithNotes(8.0, 4.0, {67, 69, 71, 72}));

            auto abc = RegionContent::trackToABC(track, song);
            expect(abc.find("P:B0") != std::string::npos);
            expect(abc.find("P:B8") != std::string::npos);
            expect(abc.find("P:B0") < abc.find("P:B8"));
        }

        beginTest("trackToABC parses back as 8 notes (4 per region)");
        {
            SongState song;
            song.tempoEvents.push_back({0.0, 120.0});
            song.timeSigEvents.push_back({0.0, 4, 4});

            TrackState track;
            track.id   = TrackId{juce::Uuid().toString().toStdString()};
            track.name = "Piano";
            track.regions.push_back(makeRegionWithNotes(0.0, 4.0, {60, 62, 64, 65}));
            track.regions.push_back(makeRegionWithNotes(8.0, 4.0, {67, 69, 71, 72}));

            auto abc = RegionContent::trackToABC(track, song);

            ABCParser p; ComposerOutput out; std::string err;
            bool ok = p.parse(juce::String(abc), out, err);
            if (!ok) logMessage("parse failed: " + juce::String(err));
            expect(ok);
            expectEquals((int) out.notes.size(), 8);
        }

        beginTest("projectToABC declares one V: per instrument track");
        {
            SongState song;
            song.tempoEvents.push_back({0.0, 120.0});
            song.timeSigEvents.push_back({0.0, 4, 4});

            TrackState piano;
            piano.id   = TrackId{juce::Uuid().toString().toStdString()};
            piano.name = "Piano";
            piano.sourceType = TrackSourceType::Instrument;
            piano.regions.push_back(makeRegionWithNotes(0.0, 4.0, {60, 62, 64, 65}));

            TrackState drums;
            drums.id   = TrackId{juce::Uuid().toString().toStdString()};
            drums.name = "Drums";
            drums.sourceType = TrackSourceType::Instrument;
            drums.regions.push_back(makeRegionWithNotes(0.0, 4.0, {36, 38, 36, 38}));

            song.tracks.push_back(piano);
            song.tracks.push_back(drums);

            auto abc = RegionContent::projectToABC(song);
            expect(abc.find("V:Piano") != std::string::npos);
            expect(abc.find("V:Drums") != std::string::npos);
            expect(abc.find("%%MIDI drummap") != std::string::npos);
        }

        beginTest("projectToABC excludes non-instrument tracks (audio input, action)");
        {
            SongState song;
            song.tempoEvents.push_back({0.0, 120.0});
            song.timeSigEvents.push_back({0.0, 4, 4});

            TrackState piano;
            piano.id   = TrackId{juce::Uuid().toString().toStdString()};
            piano.name = "Piano";
            piano.sourceType = TrackSourceType::Instrument;
            piano.regions.push_back(makeRegionWithNotes(0.0, 4.0, {60, 62, 64, 65}));

            TrackState audio;
            audio.id   = TrackId{juce::Uuid().toString().toStdString()};
            audio.name = "Mic";
            audio.sourceType = TrackSourceType::AudioInput;

            TrackState action;
            action.id   = TrackId{juce::Uuid().toString().toStdString()};
            action.name = "Triggers";
            action.sourceType = TrackSourceType::Action;

            song.tracks.push_back(piano);
            song.tracks.push_back(audio);
            song.tracks.push_back(action);

            auto abc = RegionContent::projectToABC(song);
            expect(abc.find("V:Piano")    != std::string::npos);
            expect(abc.find("V:Mic")      == std::string::npos);
            expect(abc.find("V:Triggers") == std::string::npos);
        }
    }
};

static RegionContentViewTests regionContentViewTests;


// ============================================================================
// ComposerWriter tests
// ============================================================================

class ComposerWriterTests : public juce::UnitTest {
public:
    ComposerWriterTests() : juce::UnitTest("ComposerWriter") {}

private:
    static ComposerOutput::Note makeNote(const std::string& trackName,
                                          double startBeat,
                                          double durationBeats,
                                          int pitch,
                                          float velocity = 0.63f) {
        ComposerOutput::Note n;
        n.trackName = trackName;
        n.startBeat = startBeat;
        n.durationBeats = durationBeats;
        n.pitch = pitch;
        n.velocity = velocity;
        return n;
    }

    // Find the region the writer created on the named track.
    static RegionState* regionOn(PerformanceCoordinator& coord,
                                  const juce::String& trackName) {
        auto tid = coord.state().findTrackIdByName(trackName.toStdString());
        if (tid.empty()) return nullptr;
        auto* song = coord.state().currentSong();
        if (!song) return nullptr;
        for (auto& t : song->tracks) {
            if (t.id == tid) {
                return t.regions.empty() ? nullptr : &t.regions.back();
            }
        }
        return nullptr;
    }

public:
    void runTest() override {
        beginTest("apply fails when output has no notes");
        {
            TestCoordinator tc;
            tc.state().createTrack("Piano");

            ComposerWriter w(tc.get());
            ComposerOutput out;
            out.lengthBeats = 4.0;
            std::string err;
            expect(!w.apply(out, 0.0, err));
            expect(!err.empty());
        }

        beginTest("apply fails on unknown track name");
        {
            TestCoordinator tc;
            tc.state().createTrack("Piano");

            ComposerOutput out;
            out.lengthBeats = 4.0;
            out.notes.push_back(makeNote("Keyboard", 0.0, 1.0, 60));

            ComposerWriter w(tc.get());
            std::string err;
            expect(!w.apply(out, 0.0, err));
            expect(err.find("Keyboard") != std::string::npos);
        }

        beginTest("single-track apply creates a region with noteOn+noteOff");
        {
            TestCoordinator tc;
            tc.state().createTrack("Piano");

            ComposerOutput out;
            out.lengthBeats = 4.0;
            out.notes.push_back(makeNote("Piano", 0.0, 1.0, 60, 80.0f / 127.0f));

            ComposerWriter w(tc.get());
            std::string err;
            expect(w.apply(out, 2.0, err));

            auto* region = regionOn(tc.get(), "Piano");
            expect(region != nullptr);
            expectEquals(region->startBeat, 2.0);
            expectEquals(region->lengthBeats, 4.0);

            auto* take = region->activeTake();
            expect(take != nullptr);
            expectEquals((int) take->events.size(), 2);

            // Events sorted by (beat, status) — noteOn (0x90) at beat 0,
            // noteOff (0x80) at beat 1.
            auto& a = take->events[0];
            auto& b = take->events[1];
            expectEquals(a.beatOffset, 0.0);
            expectEquals(a.status, 0x90);
            expectEquals(a.data1, 60);
            expect(a.data2 >= 79 && a.data2 <= 81);  // velocity 80 ± round

            expectEquals(b.beatOffset, 1.0);
            expectEquals(b.status, 0x80);
            expectEquals(b.data1, 60);
            expectEquals(b.data2, 0);
        }

        beginTest("multi-track apply creates one region per named track");
        {
            TestCoordinator tc;
            tc.state().createTrack("Piano");
            tc.state().createTrack("Bass");

            ComposerOutput out;
            out.lengthBeats = 4.0;
            out.notes.push_back(makeNote("Piano", 0.0, 1.0, 60));
            out.notes.push_back(makeNote("Piano", 1.0, 1.0, 64));
            out.notes.push_back(makeNote("Bass",  0.0, 2.0, 36));

            ComposerWriter w(tc.get());
            std::string err;
            expect(w.apply(out, 0.0, err));

            auto* piano = regionOn(tc.get(), "Piano");
            auto* bass  = regionOn(tc.get(), "Bass");
            expect(piano != nullptr && bass != nullptr);
            expect(piano->activeTake() != nullptr && bass->activeTake() != nullptr);

            expectEquals((int) piano->activeTake()->events.size(), 4);  // 2 notes × 2 events
            expectEquals((int) bass->activeTake()->events.size(), 2);
        }

        beginTest("apply fires Track::Updated event per affected track");
        {
            TestCoordinator tc;
            tc.state().createTrack("Piano");
            tc.state().createTrack("Bass");

            std::vector<std::pair<int, std::string>> seen;  // action, entityId
            int subId = tc.state().events().subscribe([&](const StateEvent& ev) {
                if (ev.entity == StateEvent::Track && ev.action == StateEvent::Updated) {
                    seen.emplace_back(ev.action, ev.entityId);
                }
            });

            ComposerOutput out;
            out.lengthBeats = 4.0;
            out.notes.push_back(makeNote("Piano", 0.0, 1.0, 60));
            out.notes.push_back(makeNote("Bass",  0.0, 1.0, 36));

            ComposerWriter w(tc.get());
            std::string err;
            expect(w.apply(out, 0.0, err));

            tc.state().events().unsubscribe(subId);

            // One Updated event per affected track.
            expectEquals((int) seen.size(), 2);
        }

        beginTest("velocity 0 floors to MIDI 1, 1.0 maps to 127");
        {
            TestCoordinator tc;
            tc.state().createTrack("T");

            ComposerOutput out;
            out.lengthBeats = 2.0;
            out.notes.push_back(makeNote("T", 0.0, 0.5, 60, 0.0f));
            out.notes.push_back(makeNote("T", 1.0, 0.5, 60, 1.0f));

            ComposerWriter w(tc.get());
            std::string err;
            expect(w.apply(out, 0.0, err));

            auto* region = regionOn(tc.get(), "T");
            auto* take = region->activeTake();

            // Find the two note-ons and inspect their velocities.
            int seenVels[2] = {-1, -1};
            int i = 0;
            for (auto& e : take->events) {
                if (e.status == 0x90 && i < 2) seenVels[i++] = e.data2;
            }
            expectEquals(seenVels[0], 1);
            expectEquals(seenVels[1], 127);
        }
    }
};

static ComposerWriterTests composerWriterTests;

// ============================================================================
// Live looper — Phase 1: state model + persistence
// ============================================================================
//
// Proves that the new looper-mode state (project.looperModeActive,
// track.loops, RegionState.pendingTakeId) persists correctly and is
// independent from the arrangement pool (track.regions). See
// docs/LIVE_LOOPING.md.

class LooperStateTests : public juce::UnitTest {
public:
    LooperStateTests() : juce::UnitTest("LooperState") {}

    void runTest() override {
        beginTest("mode defaults Arrangement, setMode(Looper) normalizes cycle markers");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            expect(s.getMode() == AppMode::Arrangement);
            s.setMode(AppMode::Looper);
            expect(s.getMode() == AppMode::Looper);

            // Boss-RC model: no auto-fill of cycleEnd. Until the first
            // commit, the looper has no cycle and cycleEnabled is false.
            auto* song = s.currentSong();
            expectEquals(song->cycleStart, 0.0);
            expectEquals(song->cycleEnd, 0.0);
            expect(! song->cycleEnabled);
        }

        beginTest("setCycleLength sets cycleEnd, no floor from regions");
        {
            TestCoordinator tc;
            tc.state().setMode(AppMode::Looper);
            tc.state().setCycleLength(32.0);  // first commit's elapsed beats
            auto* song = tc.state().currentSong();
            expectEquals(song->cycleEnd, 32.0);
            expectEquals(song->cycleStart, 0.0);
            expect(song->cycleEnabled);
            expectEquals(tc.state().getCycleLength(), 32.0);
        }

        beginTest("loops collection starts empty, independent of regions");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            auto* t = tc.state().findTrack(trackId);
            expectEquals((int) t->regions.size(), 0);
            expectEquals((int) t->loops.size(), 0);

            // Put a region in the arrangement pool directly; loops
            // collection must stay empty.
            RegionState r;
            r.id = RegionId{"r1"};
            r.lengthBeats = 4.0;
            t->regions.push_back(r);
            expectEquals((int) t->regions.size(), 1);
            expectEquals((int) t->loops.size(), 0);

            // And the reverse — a loop entry doesn't leak into regions.
            RegionState l;
            l.id = RegionId{"l1"};
            l.lengthBeats = 8.0;
            t->loops.push_back(l);
            expectEquals((int) t->regions.size(), 1);
            expectEquals((int) t->loops.size(), 1);
        }

        beginTest("persistence round-trips app mode + loops independently");
        {
            TempDB db;
            RegionId arrangementRegionId, loopRegionId;
            TrackId trackId;
            SongId songId;

            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.createSong("RoundTrip");
                coord.state().setMode(AppMode::Looper);
                coord.state().setCycleLength(64.0);
                trackId = coord.state().createTrack("T");
                songId = coord.state().currentSong()->id;
                auto* t = coord.state().findTrack(trackId);

                RegionState r;
                r.id = RegionId{"r_arr"};
                r.startBeat = 4.0;
                r.lengthBeats = 8.0;
                r.name = "arrangement region";
                arrangementRegionId = r.id;
                t->regions.push_back(r);

                RegionState l;
                l.id = RegionId{"r_loop"};
                l.startBeat = 0.0;
                l.lengthBeats = 4.0;
                l.name = "loop region";
                loopRegionId = l.id;
                t->loops.push_back(l);

                coord.save();
                coord.shutdown();
            }

            // Reopen with a fresh coordinator.
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                auto* song = coord.state().findSong(songId);
                expect(song != nullptr);
                expect(coord.state().getMode() == AppMode::Looper);
                expectEquals(song->cycleEnd, 64.0);

                auto* t = coord.state().findTrack(trackId);
                expect(t != nullptr);
                expectEquals((int) t->regions.size(), 1);
                expectEquals((int) t->loops.size(), 1);
                expect(t->regions[0].id == arrangementRegionId);
                expect(t->loops[0].id == loopRegionId);
                expectEquals(juce::String(t->regions[0].name), juce::String("arrangement region"));
                expectEquals(juce::String(t->loops[0].name), juce::String("loop region"));
                coord.shutdown();
            }
        }

        beginTest("pendingTakeId is runtime-only (not persisted)");
        {
            TempDB db;
            TrackId trackId;
            SongId songId;
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.createSong("PendingTest");
                trackId = coord.state().createTrack("T");
                songId = coord.state().currentSong()->id;
                auto* t = coord.state().findTrack(trackId);
                RegionState l;
                l.id = RegionId{"r_loop"};
                l.lengthBeats = 4.0;
                l.activeTakeId = TakeId{"take-a"};
                l.pendingTakeId = TakeId{"take-pending"};  // should NOT survive
                t->loops.push_back(l);
                coord.save();
                coord.shutdown();
            }
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                auto* t = coord.state().findTrack(trackId);
                expect(t != nullptr);
                expectEquals((int) t->loops.size(), 1);
                expect(t->loops[0].activeTakeId == TakeId{"take-a"});
                expect(t->loops[0].pendingTakeId.empty());
                coord.shutdown();
            }
        }
    }
};

static LooperStateTests looperStateTests;

// ============================================================================
// Live looper — Phase 2: playback engine (within-cycle region wrap)
// ============================================================================

class LooperPlaybackTests : public juce::UnitTest {
public:
    LooperPlaybackTests() : juce::UnitTest("LooperPlayback") {}

private:
    // Parallel fixture to ArrangementTests::TestContext but uses
    // track.loops instead of track.regions. Each test sets up a loop
    // with some events and scans across a beat range.
    struct LooperCtx {
        std::vector<TrackState> tracks;
        Arrangement arr;

        LooperCtx(std::initializer_list<std::string> trackIds, double cycleLengthBeats) {
            for (auto& id : trackIds) {
                TrackState t;
                t.id = TrackId{id};
                t.name = id;
                tracks.push_back(std::move(t));
            }
            arr.setTracks(&tracks);
            arr.updateLooperMode(true, cycleLengthBeats);
        }

        TrackState* track(const std::string& id) {
            for (auto& t : tracks)
                if (t.id == TrackId{id}) return &t;
            return nullptr;
        }

        // Add a loop with one take containing the given events.
        RegionState* addLoop(const std::string& trackId, double lengthBeats,
                              const std::vector<MidiEventState>& events) {
            auto* t = track(trackId);
            if (!t) return nullptr;
            RegionState r;
            r.id = RegionId{"loop-" + trackId};
            r.type = "midi";
            r.startBeat = 0.0;
            r.lengthBeats = lengthBeats;
            TakeState take;
            take.id = TakeId{"take-" + trackId};
            take.events = events;
            r.activeTakeId = take.id;
            r.takes.push_back(std::move(take));
            t->loops.push_back(std::move(r));
            return &t->loops.back();
        }
    };

    static MidiEventState noteOn(int pitch, double beat) {
        return { beat, 0x90, 1, pitch, 100 };
    }
    static MidiEventState noteOff(int pitch, double beat) {
        return { beat, 0x80, 1, pitch, 0 };
    }

public:
    void runTest() override {
        beginTest("4-bar loop in 16-bar cycle plays 4× per cycle pass");
        {
            LooperCtx ctx({"t1"}, 16.0);
            ctx.addLoop("t1", 4.0, {
                noteOn(60, 0.0), noteOff(60, 1.0),
            });

            std::vector<double> beats;
            ctx.arr.scanMidiEvents(0.0, 16.0, [&](const TrackId&, const MidiEventState& e, double b) {
                if ((e.status & 0xF0) == 0x90) beats.push_back(b);
            });

            expectEquals((int) beats.size(), 4);
            expectEquals(beats[0], 0.0);
            expectEquals(beats[1], 4.0);
            expectEquals(beats[2], 8.0);
            expectEquals(beats[3], 12.0);
        }

        beginTest("Loop equal to cycle plays once per cycle pass");
        {
            LooperCtx ctx({"t1"}, 8.0);
            ctx.addLoop("t1", 8.0, {
                noteOn(60, 0.0), noteOff(60, 1.0),
                noteOn(62, 4.0), noteOff(62, 5.0),
            });

            int noteOns = 0;
            ctx.arr.scanMidiEvents(0.0, 8.0, [&](const TrackId&, const MidiEventState& e, double) {
                if ((e.status & 0xF0) == 0x90) ++noteOns;
            });
            expectEquals(noteOns, 2);  // plays once
        }

        beginTest("Loop longer than cycle has its tail clipped");
        {
            LooperCtx ctx({"t1"}, 16.0);
            // 20-bar loop: events at beats 0, 8, 16, 18 within the loop
            ctx.addLoop("t1", 20.0, {
                noteOn(60, 0.0),  noteOff(60, 1.0),
                noteOn(62, 8.0),  noteOff(62, 9.0),
                noteOn(64, 16.0), noteOff(64, 17.0),  // at cycle boundary — clipped
                noteOn(65, 18.0), noteOff(65, 19.0),  // past cycle — clipped
            });

            std::vector<int> pitches;
            ctx.arr.scanMidiEvents(0.0, 16.0, [&](const TrackId&, const MidiEventState& e, double) {
                if ((e.status & 0xF0) == 0x90) pitches.push_back(e.data1);
            });
            // Only 60 and 62 play; 64 and 65 are in the preserved-but-silent tail.
            expectEquals((int) pitches.size(), 2);
            expectEquals(pitches[0], 60);
            expectEquals(pitches[1], 62);
        }

        beginTest("Looper mode off falls back to arrangement scan");
        {
            LooperCtx ctx({"t1"}, 16.0);
            ctx.addLoop("t1", 4.0, {
                noteOn(60, 0.0), noteOff(60, 1.0),
            });
            ctx.arr.updateLooperMode(false, 0.0);  // flip off

            int noteOns = 0;
            ctx.arr.scanMidiEvents(0.0, 16.0, [&](const TrackId&, const MidiEventState& e, double) {
                if ((e.status & 0xF0) == 0x90) ++noteOns;
            });
            // Loops are ignored in arrangement mode — no events.
            expectEquals(noteOns, 0);
        }

        beginTest("Multiple tracks loop independently");
        {
            LooperCtx ctx({"t1", "t2"}, 16.0);
            ctx.addLoop("t1", 4.0, { noteOn(60, 0.0), noteOff(60, 1.0) });
            ctx.addLoop("t2", 8.0, { noteOn(72, 0.0), noteOff(72, 1.0) });

            std::map<int, int> countByPitch;
            ctx.arr.scanMidiEvents(0.0, 16.0, [&](const TrackId&, const MidiEventState& e, double) {
                if ((e.status & 0xF0) == 0x90) countByPitch[e.data1]++;
            });
            expectEquals(countByPitch[60], 4);  // 4-bar loop plays 4× in 16
            expectEquals(countByPitch[72], 2);  // 8-bar loop plays 2× in 16
        }

        beginTest("Muted track's loop is silent");
        {
            LooperCtx ctx({"t1"}, 16.0);
            // Mute via the region-level muted flag (track-level mute happens
            // elsewhere in the engine; this is the region's own muted gate).
            auto* r = ctx.addLoop("t1", 4.0, { noteOn(60, 0.0), noteOff(60, 1.0) });
            r->muted = true;

            int events = 0;
            ctx.arr.scanMidiEvents(0.0, 16.0, [&](const TrackId&, const MidiEventState&, double) {
                ++events;
            });
            expectEquals(events, 0);
        }
    }
};

static LooperPlaybackTests looperPlaybackTests;

// ============================================================================
// Live looper — Phase 3a: take-swap at cycle wrap
// ============================================================================

class LooperTakeSwapTests : public juce::UnitTest {
public:
    LooperTakeSwapTests() : juce::UnitTest("LooperTakeSwap") {}

    void runTest() override {
        beginTest("setPendingTake on a loop region defers the swap");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            s.setMode(AppMode::Looper);
            s.setCycleLength(16.0);
            auto trackId = s.createTrack("T");
            auto* t = s.findTrack(trackId);

            // Seed the track with a loop region having two takes.
            RegionState r;
            r.id = RegionId{"loop1"};
            r.lengthBeats = 4.0;
            TakeState a, b;
            a.id = TakeId{"take-a"};
            b.id = TakeId{"take-b"};
            r.takes.push_back(a);
            r.takes.push_back(b);
            r.activeTakeId = a.id;
            t->loops.push_back(r);

            s.setPendingTake(RegionId{"loop1"}, TakeId{"take-b"});

            // active unchanged, pending set.
            auto* region = &s.findTrack(trackId)->loops[0];
            expect(region->activeTakeId == TakeId{"take-a"});
            expect(region->pendingTakeId == TakeId{"take-b"});

            // Promote.
            int swapped = s.commitPendingTakeSwaps();
            expectEquals(swapped, 1);
            expect(region->activeTakeId == TakeId{"take-b"});
            expect(region->pendingTakeId.empty());
        }

        beginTest("setPendingTake when looper mode off is immediate");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            // Looper mode stays OFF
            auto trackId = s.createTrack("T");
            auto* t = s.findTrack(trackId);

            RegionState r;
            r.id = RegionId{"loop1"};
            r.lengthBeats = 4.0;
            TakeState a, b;
            a.id = TakeId{"take-a"};
            b.id = TakeId{"take-b"};
            r.takes.push_back(a);
            r.takes.push_back(b);
            r.activeTakeId = a.id;
            t->loops.push_back(r);

            s.setPendingTake(RegionId{"loop1"}, TakeId{"take-b"});

            auto* region = &s.findTrack(trackId)->loops[0];
            expect(region->activeTakeId == TakeId{"take-b"});   // immediate
            expect(region->pendingTakeId.empty());
        }

        beginTest("setPendingTake on an arrangement region is always immediate");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            s.setMode(AppMode::Looper);  // even in looper mode
            auto trackId = s.createTrack("T");
            auto* t = s.findTrack(trackId);

            RegionState r;
            r.id = RegionId{"arr1"};
            r.startBeat = 4.0;
            r.lengthBeats = 4.0;
            TakeState a, b;
            a.id = TakeId{"a"};
            b.id = TakeId{"b"};
            r.takes.push_back(a);
            r.takes.push_back(b);
            r.activeTakeId = a.id;
            t->regions.push_back(r);  // arrangement pool

            s.setPendingTake(RegionId{"arr1"}, TakeId{"b"});

            // Arrangement regions don't defer — the swap is immediate
            // regardless of looper mode, because the cycle-wrap concept
            // doesn't apply to timeline regions.
            auto* region = &s.findTrack(trackId)->regions[0];
            expect(region->activeTakeId == TakeId{"b"});
            expect(region->pendingTakeId.empty());
        }

        beginTest("commitPendingTakeSwaps handles multiple tracks at once");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            s.setMode(AppMode::Looper);
            s.setCycleLength(16.0);
            auto t1 = s.createTrack("T1");
            auto t2 = s.createTrack("T2");

            auto seed = [&](const TrackId& tid, const std::string& loopId) {
                auto* t = s.findTrack(tid);
                RegionState r;
                r.id = RegionId{loopId};
                r.lengthBeats = 4.0;
                TakeState a, b;
                a.id = TakeId{loopId + "-a"};
                b.id = TakeId{loopId + "-b"};
                r.takes.push_back(a);
                r.takes.push_back(b);
                r.activeTakeId = a.id;
                t->loops.push_back(r);
            };
            seed(t1, "loopA");
            seed(t2, "loopB");

            s.setPendingTake(RegionId{"loopA"}, TakeId{"loopA-b"});
            s.setPendingTake(RegionId{"loopB"}, TakeId{"loopB-b"});

            int swapped = s.commitPendingTakeSwaps();
            expectEquals(swapped, 2);
            expect(s.findTrack(t1)->loops[0].activeTakeId == TakeId{"loopA-b"});
            expect(s.findTrack(t2)->loops[0].activeTakeId == TakeId{"loopB-b"});
        }

        beginTest("commitPendingTakeSwaps no-ops when nothing pending");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            s.setMode(AppMode::Looper);
            s.setCycleLength(16.0);
            auto t1 = s.createTrack("T1");
            auto* t = s.findTrack(t1);
            RegionState r;
            r.id = RegionId{"loop"};
            r.lengthBeats = 4.0;
            TakeState a;
            a.id = TakeId{"a"};
            r.takes.push_back(a);
            r.activeTakeId = a.id;
            t->loops.push_back(r);

            int swapped = s.commitPendingTakeSwaps();
            expectEquals(swapped, 0);
        }
    }
};

static LooperTakeSwapTests looperTakeSwapTests;

// ============================================================================
// Live looper — Phase 3b: loop recording (Arrangement helpers)
// ============================================================================

class LooperRecordTests : public juce::UnitTest {
public:
    LooperRecordTests() : juce::UnitTest("LooperRecord") {}

    void runTest() override {
        beginTest("startLoopRecording creates loop region on first call");
        {
            std::vector<TrackState> tracks;
            TrackState t;
            t.id = TrackId{"t1"};
            tracks.push_back(std::move(t));
            Arrangement arr;
            arr.setTracks(&tracks);

            expectEquals((int) tracks[0].loops.size(), 0);

            auto* region = arr.startLoopRecording(TrackId{"t1"});
            expect(region != nullptr);
            expectEquals((int) tracks[0].loops.size(), 1);
            expectEquals((int) region->takes.size(), 1);  // first take
            expect(arr.isRecording());
        }

        beginTest("captured events route to new take; stopLoopRecording sets length");
        {
            std::vector<TrackState> tracks;
            TrackState t; t.id = TrackId{"t1"};
            tracks.push_back(std::move(t));
            Arrangement arr;
            arr.setTracks(&tracks);

            auto* region = arr.startLoopRecording(TrackId{"t1"});
            arr.addRecordedEvent({ 0.0, 0x90, 1, 60, 100 });
            arr.addRecordedEvent({ 0.5, 0x80, 1, 60, 0 });
            arr.stopLoopRecording(TrackId{"t1"}, 4.0);

            expect(!arr.isRecording());
            expectEquals(region->lengthBeats, 4.0);

            auto* take = region->activeTake();
            expect(take != nullptr);
            expectEquals((int) take->events.size(), 2);
            expect(region->activeTakeId == take->id);
        }

        beginTest("second punch-in appends a take; previous takes persist");
        {
            std::vector<TrackState> tracks;
            TrackState t; t.id = TrackId{"t1"};
            tracks.push_back(std::move(t));
            Arrangement arr;
            arr.setTracks(&tracks);

            // Pass 1
            arr.startLoopRecording(TrackId{"t1"});
            arr.addRecordedEvent({ 0.0, 0x90, 1, 60, 100 });
            arr.addRecordedEvent({ 0.5, 0x80, 1, 60, 0 });
            arr.stopLoopRecording(TrackId{"t1"}, 4.0);

            auto take1Id = tracks[0].loops[0].activeTakeId;

            // Pass 2
            arr.startLoopRecording(TrackId{"t1"});
            arr.addRecordedEvent({ 0.0, 0x90, 1, 64, 100 });
            arr.addRecordedEvent({ 0.5, 0x80, 1, 64, 0 });
            arr.stopLoopRecording(TrackId{"t1"}, 4.0);

            auto& region = tracks[0].loops[0];
            expectEquals((int) region.takes.size(), 2);   // both persist
            expect(region.activeTakeId != take1Id);       // latest is active

            // Active take has the second pass's pitch.
            auto* active = region.activeTake();
            expect(active != nullptr);
            expectEquals((int) active->events.size(), 2);
            expectEquals(active->events[0].data1, 64);
        }

        beginTest("stopLoopRecording handles a track with no in-flight recording gracefully");
        {
            std::vector<TrackState> tracks;
            TrackState t; t.id = TrackId{"t1"};
            tracks.push_back(std::move(t));
            Arrangement arr;
            arr.setTracks(&tracks);

            // No startLoopRecording — just call stop. Should no-op.
            arr.stopLoopRecording(TrackId{"t1"}, 4.0);
            expectEquals((int) tracks[0].loops.size(), 0);
        }
    }
};

static LooperRecordTests looperRecordTests;

// ============================================================================
// Looper Boss-style — phase 6 (see docs/LIVE_INPUT_AND_FOCUS.md).
// Per-track queued gestures: replace / overdub / undo / redo / clear.
// Capture happens at cycle wrap; one cycle later it commits.
// ============================================================================
class LooperBossTests : public juce::UnitTest {
public:
    LooperBossTests() : juce::UnitTest("LooperBoss") {}

    void runTest() override {
        // Established-cycle gesture transitions are queue-based:
        // None ↔ Queued ↔ (other) Queued; Capturing is locked. The
        // bootstrap path (no cycle) uses startLoopCaptureNow to go
        // directly to Capturing. These tests cover StateAPI primitives;
        // coordinator-level wrap dispatch lives in
        // CoordinatorLooperGestureTests.

        beginTest("replaceLoop on empty track creates region + queues replace");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            expectEquals((int) tc.state().findTrack(trackId)->loops.size(), 0);
            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();
            auto* t = tc.state().findTrack(trackId);
            expectEquals((int) t->loops.size(), 1);
            expect(tc.state().getLoopAction(trackId) == LoopAction::ReplaceQueued);
        }

        beginTest("re-pressing same gesture cancels back to None");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();
            expect(tc.state().getLoopAction(trackId) == LoopAction::ReplaceQueued);
            tc.state().replaceLoop();
            expect(tc.state().getLoopAction(trackId) == LoopAction::None);

            tc.state().overdubLoop();
            expect(tc.state().getLoopAction(trackId) == LoopAction::OverdubQueued);
            tc.state().overdubLoop();
            expect(tc.state().getLoopAction(trackId) == LoopAction::None);
        }

        beginTest("pressing the other gesture switches the queued kind");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();
            tc.state().overdubLoop();
            expect(tc.state().getLoopAction(trackId) == LoopAction::OverdubQueued);
            tc.state().replaceLoop();
            expect(tc.state().getLoopAction(trackId) == LoopAction::ReplaceQueued);
        }

        beginTest("either gesture during Capturing is ignored");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();
            tc.state().beginLoopCapture(trackId);   // Queued → CapturingReplace
            expect(tc.state().getLoopAction(trackId) == LoopAction::CapturingReplace);
            tc.state().replaceLoop();   // ignored
            expect(tc.state().getLoopAction(trackId) == LoopAction::CapturingReplace);
            tc.state().overdubLoop();   // ignored
            expect(tc.state().getLoopAction(trackId) == LoopAction::CapturingReplace);
        }

        beginTest("beginLoopCapture flips Queued → Capturing for both kinds");
        {
            TestCoordinator tc;
            auto t1 = tc.state().createTrack("T1");
            tc.state().setFocusedTrackId(t1);
            tc.state().replaceLoop();
            tc.state().beginLoopCapture(t1);
            expect(tc.state().getLoopAction(t1) == LoopAction::CapturingReplace);

            auto t2 = tc.state().createTrack("T2");
            tc.state().setFocusedTrackId(t2);
            tc.state().overdubLoop();
            tc.state().beginLoopCapture(t2);
            expect(tc.state().getLoopAction(t2) == LoopAction::CapturingOverdub);
        }

        beginTest("beginLoopCapture from None / non-Queued is a no-op");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();
            tc.state().replaceLoop();   // cancel back to None
            tc.state().beginLoopCapture(trackId);
            expect(tc.state().getLoopAction(trackId) == LoopAction::None);
        }

        beginTest("startLoopCaptureNow drops directly into Capturing (bootstrap path)");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setFocusedTrackId(trackId);
            tc.state().startLoopCaptureNow(LoopAction::CapturingReplace);
            expect(tc.state().getLoopAction(trackId) == LoopAction::CapturingReplace);
        }

        beginTest("commit Replace: events become captured, state → None");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();
            tc.state().beginLoopCapture(trackId);

            std::vector<MidiEventState> captured = {
                { 0.0, 0x90, 1, 60, 100 }, { 1.0, 0x80, 1, 60, 0 }
            };
            tc.state().commitLoopAction(trackId, captured);

            auto* take = tc.state().findTrack(trackId)->loops[0].activeTake();
            expectEquals((int) take->events.size(), 2);
            expectEquals(take->events[0].data1, 60);
            expect(tc.state().getLoopAction(trackId) == LoopAction::None);
            expect(tc.state().canUndo());
        }

        beginTest("commit Overdub: existing events plus captured, sorted");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();
            tc.state().beginLoopCapture(trackId);
            tc.state().commitLoopAction(trackId, {
                { 0.0, 0x90, 1, 60, 100 }, { 2.0, 0x80, 1, 60, 0 }
            });

            tc.state().overdubLoop();
            tc.state().beginLoopCapture(trackId);
            tc.state().commitLoopAction(trackId, {
                { 1.0, 0x90, 1, 64, 100 }, { 3.0, 0x80, 1, 64, 0 }
            });

            auto* take = tc.state().findTrack(trackId)->loops[0].activeTake();
            expectEquals((int) take->events.size(), 4);
            expectEquals(take->events[0].beatOffset, 0.0);
            expectEquals(take->events[1].beatOffset, 1.0);
            expectEquals(take->events[2].beatOffset, 2.0);
            expectEquals(take->events[3].beatOffset, 3.0);
            expect(tc.state().canUndo());
        }

        beginTest("commit outside Capturing* state is a no-op");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();   // → ReplaceQueued (NOT capturing)
            tc.state().commitLoopAction(trackId, {
                { 0.0, 0x90, 1, 60, 100 }
            });
            // Region exists (replaceLoop created it) but no events
            // committed since state is Queued not Capturing.
            auto* t = tc.state().findTrack(trackId);
            expect(! t->loops.empty());
            auto* take = t->loops[0].activeTake();
            expectEquals((int) take->events.size(), 0);
        }

        beginTest("clearLoop pushes app-level undo, empties events; cycleEnd untouched");
        {
            TestCoordinator tc;
            auto trackId = tc.state().createTrack("T");
            tc.state().setMode(AppMode::Looper);
            tc.state().setCycleLength(16.0);

            tc.state().setFocusedTrackId(trackId);
            tc.state().replaceLoop();
            tc.state().beginLoopCapture(trackId);
            tc.state().commitLoopAction(trackId, { { 0.0, 0x90, 1, 60, 100 } });

            tc.state().clearLoop();
            expectEquals((int) tc.state().findTrack(trackId)->loops[0].activeTake()->events.size(), 0);
            expect(tc.state().canUndo());
            expectEquals(tc.state().currentSong()->cycleEnd, 16.0);

            tc.state().undo();
            expectEquals((int) tc.state().findTrack(trackId)->loops[0].activeTake()->events.size(), 1);
        }

        beginTest("clearAllLoops resets cycleEnd and is undoable");
        {
            TestCoordinator tc;
            auto t1 = tc.state().createTrack("T1");
            auto t2 = tc.state().createTrack("T2");
            tc.state().setMode(AppMode::Looper);
            tc.state().setCycleLength(16.0);

            for (auto& tid : { t1, t2 }) {
                tc.state().setFocusedTrackId(tid);
                tc.state().replaceLoop();
                tc.state().beginLoopCapture(tid);
                tc.state().commitLoopAction(tid, { { 0.0, 0x90, 1, 60, 100 } });
            }

            tc.state().clearAllLoops();

            for (auto& tid : { t1, t2 }) {
                auto* t = tc.state().findTrack(tid);
                expectEquals((int) t->loops[0].activeTake()->events.size(), 0);
            }
            expectEquals(tc.state().currentSong()->cycleEnd, 0.0);
            expect(! tc.state().currentSong()->cycleEnabled);

            tc.state().undo();
            expectEquals(tc.state().currentSong()->cycleEnd, 16.0);
        }

        beginTest("undo on a fresh bootstrap commit also undoes master cycle");
        {
            // Motivating bug: the very first (bootstrap) commit sets
            // master cycle from elapsed beats; a single undo must take
            // the looper all the way back to its initial no-cycle state.
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto trackId = s.createTrack("T");
            s.setMode(AppMode::Looper);
            expectEquals(s.currentSong()->cycleEnd, 0.0);

            s.setFocusedTrackId(trackId);
            coord.replaceLoopGesture();          // bootstrap start → CapturingReplace
            coord.sequencer()->setBeatPosition(8.0);
            coord.replaceLoopGesture();          // bootstrap stop → commit + cycle=8
            expectWithinAbsoluteError(s.currentSong()->cycleEnd, 8.0, 1e-6);
            expect(s.canUndo());

            s.undo();
            expectEquals(s.currentSong()->cycleEnd, 0.0);
            auto* t = s.findTrack(trackId);
            expect(! t || t->loops.empty()
                   || t->loops[0].activeTake()->events.empty());

            coord.resetLooperSession();
        }
    }
};

static LooperBossTests looperBossTests;

// ============================================================================
// Focus — singular per-song pointer at "the track I'm playing into."
// See docs/LIVE_INPUT_AND_FOCUS.md. Phase 1: state-layer accessors only,
// no engine behavior yet.
class TrackFocusTests : public juce::UnitTest {
public:
    TrackFocusTests() : juce::UnitTest("TrackFocus") {}

    void runTest() override {
        beginTest("focus defaults empty with no tracks");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            // No tracks created → focus stays at default.
            expect(s.getFocusedTrackId().empty());
        }

        beginTest("setFocusedTrackId emits on change + stores");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");   // auto-focuses to t1
            auto t2 = s.createTrack("T2");   // auto-focuses to t2

            int eventCount = 0;
            std::string lastEntityId;
            int subId = s.events().subscribe([&](const StateEvent& ev) {
                if (ev.entity == StateEvent::Focus) {
                    eventCount++;
                    lastEntityId = ev.entityId;
                }
            });

            // Now change focus back to t1 — fires one event.
            s.setFocusedTrackId(t1);
            expect(s.getFocusedTrackId() == t1);
            expectEquals(eventCount, 1);
            expectEquals(juce::String(lastEntityId), juce::String(t1.str()));

            s.events().unsubscribe(subId);
        }

        beginTest("setFocusedTrackId is idempotent — no event when unchanged");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            auto t2 = s.createTrack("T2");   // focus now on t2

            int eventCount = 0;
            int subId = s.events().subscribe([&](const StateEvent& ev) {
                if (ev.entity == StateEvent::Focus) eventCount++;
            });

            s.setFocusedTrackId(t2);   // same value — no-op
            expectEquals(eventCount, 0);

            s.setFocusedTrackId(t1);   // real change, emits
            expectEquals(eventCount, 1);

            s.events().unsubscribe(subId);
        }

        beginTest("creating a track moves focus to it");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto trackA = s.createTrack("A");
            expect(s.getFocusedTrackId() == trackA);
            auto trackB = s.createTrack("B");
            expect(s.getFocusedTrackId() == trackB);
        }

        beginTest("focusing an instrument snaps I on focused + off on other instruments");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto a = s.createTrack("A");   // focus → A, A.I = on
            auto b = s.createTrack("B");   // focus → B, A.I = off, B.I = on
            auto c = s.createTrack("C");   // focus → C, A.I = off, B.I = off, C.I = on

            expect(s.findTrack(a)->inputMonitoring == false);
            expect(s.findTrack(b)->inputMonitoring == false);
            expect(s.findTrack(c)->inputMonitoring == true);

            // Re-focusing an older instrument snaps again.
            s.setFocusedTrackId(a);
            expect(s.findTrack(a)->inputMonitoring == true);
            expect(s.findTrack(b)->inputMonitoring == false);
            expect(s.findTrack(c)->inputMonitoring == false);
        }

        beginTest("audio tracks default to I=off and focus doesn't snap them");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto instr = s.createTrack("Instrument");
            expect(s.findTrack(instr)->inputMonitoring == true);

            // Create audio track — focus moves to it, I=false by default,
            // no snap fires for audio tracks so the instrument's I stays on.
            auto aud = s.createAudioInputTrack("Mic", -1, 0);
            expect(s.getFocusedTrackId() == aud);
            expect(s.findTrack(aud)->inputMonitoring == false);   // safe default
            expect(s.findTrack(instr)->inputMonitoring == true);  // unchanged

            // Re-focus the audio track: still no snap on anyone.
            auto instr2 = s.createTrack("Instr2");
            // After creating instr2, focus moved, instrument snap fired —
            // instr.I went to false, instr2.I is true.
            expect(s.findTrack(instr)->inputMonitoring == false);
            expect(s.findTrack(instr2)->inputMonitoring == true);

            s.setFocusedTrackId(aud);
            // Audio focus: no instrument-I changes at all.
            expect(s.findTrack(instr)->inputMonitoring == false);
            expect(s.findTrack(instr2)->inputMonitoring == true);
            expect(s.findTrack(aud)->inputMonitoring == false);
        }

        beginTest("I pill toggle persists until next focus change");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto a = s.createTrack("A");
            auto b = s.createTrack("B");
            // B has focus, I=true on B and false on A.
            expect(s.findTrack(a)->inputMonitoring == false);

            // User toggles I on A for multi-listen.
            s.setTrackInputMonitoring(a, true);
            expect(s.findTrack(a)->inputMonitoring == true);
            expect(s.findTrack(b)->inputMonitoring == true);

            // Focus stays on B (focus unchanged), multi-listen persists.
            expect(s.getFocusedTrackId() == b);

            // Clicking B again = focus stays B, snap doesn't re-fire (idempotent).
            s.setFocusedTrackId(b);
            expect(s.findTrack(a)->inputMonitoring == true);
            expect(s.findTrack(b)->inputMonitoring == true);

            // Clicking A = snap fires, A=on, B=off.
            s.setFocusedTrackId(a);
            expect(s.findTrack(a)->inputMonitoring == true);
            expect(s.findTrack(b)->inputMonitoring == false);
        }

        beginTest("focus persists per-song across coordinator restart");
        {
            TempDB db;
            TrackId focusTrackId;
            SongId songId;
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                coord.createSong("Focus");
                focusTrackId = coord.state().createTrack("focused track");
                coord.state().createTrack("other track");
                songId = coord.state().currentSong()->id;
                coord.state().setFocusedTrackId(focusTrackId);
                coord.save();
                coord.shutdown();
            }
            {
                PerformanceCoordinator coord;
                coord.initialise(db.path());
                auto* song = coord.state().findSong(songId);
                expect(song != nullptr);
                expect(coord.state().getFocusedTrackId() == focusTrackId);
                coord.shutdown();
            }
        }
    }
};

static TrackFocusTests trackFocusTests;

// ============================================================================
// Focus navigation gestures — focusPrev/Next wrap-around with Action skip,
// plus toggleFocusedMute. Bound as Looper-pane buttons + bindable actions.
// ============================================================================
class FocusNavigationTests : public juce::UnitTest {
public:
    FocusNavigationTests() : juce::UnitTest("FocusNavigation") {}

    void runTest() override {
        beginTest("focusNextTrack wraps from last to first");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto a = s.createTrack("A");
            auto b = s.createTrack("B");
            auto c = s.createTrack("C");
            s.setFocusedTrackId(c);
            s.focusNextTrack();
            expect(s.getFocusedTrackId() == a);
        }

        beginTest("focusPrevTrack wraps from first to last");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto a = s.createTrack("A");
            auto b = s.createTrack("B");
            auto c = s.createTrack("C");
            s.setFocusedTrackId(a);
            s.focusPrevTrack();
            expect(s.getFocusedTrackId() == c);
        }

        beginTest("both skip Action tracks");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto a = s.createTrack("A");
            // Action track lives at the start of every song already (created
            // by TestCoordinator::createSong → createActionTrack); the
            // skip behaviour should keep focus on instrument tracks only.
            auto b = s.createTrack("B");
            s.setFocusedTrackId(a);
            s.focusNextTrack();
            expect(s.getFocusedTrackId() == b);
            s.focusNextTrack();
            expect(s.getFocusedTrackId() == a);  // wrapped past Action
        }

        beginTest("no-op with fewer than two non-action tracks");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto a = s.createTrack("Only");
            s.setFocusedTrackId(a);
            s.focusNextTrack();
            expect(s.getFocusedTrackId() == a);  // unchanged
            s.focusPrevTrack();
            expect(s.getFocusedTrackId() == a);
        }

        beginTest("toggleFocusedMute flips muted on focused track");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto a = s.createTrack("A");
            auto b = s.createTrack("B");
            s.setFocusedTrackId(b);
            expect(! s.isTrackMuted(b));
            s.toggleFocusedMute();
            expect(s.isTrackMuted(b));
            // The other track is untouched.
            expect(! s.isTrackMuted(a));
            s.toggleFocusedMute();
            expect(! s.isTrackMuted(b));
        }

        beginTest("toggleFocusedMute is a no-op when no focus");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto a = s.createTrack("A");
            // Explicitly clear focus (createTrack auto-focuses).
            s.setFocusedTrackId(TrackId{});
            s.toggleFocusedMute();
            expect(! s.isTrackMuted(a));  // didn't mute anything
        }
    }
};

static FocusNavigationTests focusNavigationTests;

// ============================================================================
// Action-fire listener fan-out — coordinator's multi-listener registration
// for action dispatch. Replaces the prior single-callback model so every
// BindableButton can subscribe to the same chokepoint without stomping.
// ============================================================================
class ActionFireListenerTests : public juce::UnitTest {
public:
    ActionFireListenerTests() : juce::UnitTest("ActionFireListener") {}

    void runTest() override {
        beginTest("registered listener fires on executeAction; ids are unique");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            std::vector<std::string> seen;
            int idA = coord.addActionFireListener(
                [&](const std::string& name) { seen.push_back("A:" + name); });
            int idB = coord.addActionFireListener(
                [&](const std::string& name) { seen.push_back("B:" + name); });
            expect(idA != idB);

            // togglePlay is a built-in action and doesn't depend on focus.
            coord.executeAction("togglePlay", juce::var(), 1.0f);

            expect(seen.size() == 2);
            expect(seen[0] == "A:togglePlay" || seen[1] == "A:togglePlay");
            expect(seen[0] == "B:togglePlay" || seen[1] == "B:togglePlay");

            // Cleanup; toggle once more to put transport back at rest.
            coord.removeActionFireListener(idA);
            coord.removeActionFireListener(idB);
            if (coord.sequencer() && coord.sequencer()->isPlaying())
                coord.executeAction("togglePlay", juce::var(), 1.0f);
        }

        beginTest("removed listener stops firing");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            int hits = 0;
            int id = coord.addActionFireListener(
                [&](const std::string&) { hits++; });
            coord.executeAction("togglePlay", juce::var(), 1.0f);
            expect(hits == 1);
            coord.removeActionFireListener(id);
            coord.executeAction("togglePlay", juce::var(), 1.0f);
            expect(hits == 1);  // unchanged after removal
            // Clean up transport state.
            if (coord.sequencer() && coord.sequencer()->isPlaying())
                coord.executeAction("togglePlay", juce::var(), 1.0f);
        }
    }
};

static ActionFireListenerTests actionFireListenerTests;

// ============================================================================
// Coordinator-level looper gestures — the bootstrap-vs-established cycle
// branching, focus / mode bail-outs, and resetLooperSession contract that
// sit above the StateAPI primitives covered in LooperBossTests.
// ============================================================================
class CoordinatorLooperGestureTests : public juce::UnitTest {
public:
    CoordinatorLooperGestureTests() : juce::UnitTest("CoordinatorLooperGesture") {}

    void runTest() override {
        // Two paths: bootstrap (tap-to-start, tap-to-stop, no cycle yet)
        // and established (queue at tap, wrap promotes to Capturing).
        // Wrap-driven established dispatch needs the message loop to run,
        // which tests don't do — those edges are click-tested.

        beginTest("bootstrap: first tap starts capture, transport plays from 0");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);

            coord.replaceLoopGesture();

            expect(s.getLoopAction(t1) == LoopAction::CapturingReplace);
            expect(coord.getInFlightLoopCapture().has_value());
            expect(coord.getInFlightLoopCapture()->trackId == t1);
            expect(coord.sequencer() && coord.sequencer()->isPlaying());

            coord.resetLooperSession();
        }

        beginTest("bootstrap: second tap commits and sets master cycle from elapsed");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);

            coord.replaceLoopGesture();   // start
            coord.sequencer()->setBeatPosition(8.0);
            coord.replaceLoopGesture();   // stop = commit

            expect(s.getLoopAction(t1) == LoopAction::None);
            expect(! coord.getInFlightLoopCapture().has_value());
            expectWithinAbsoluteError(s.currentSong()->cycleEnd, 8.0, 1e-6);
            auto* t = s.findTrack(t1);
            expect(t && t->loops.size() == 1);
            expectWithinAbsoluteError(t->loops[0].lengthBeats, 8.0, 1e-6);

            coord.sequencer()->stop();
        }

        beginTest("established: tap queues (no immediate capture, transport unchanged)");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);
            // Stand up master cycle the cheap way: bootstrap-record once.
            coord.replaceLoopGesture();
            coord.sequencer()->setBeatPosition(4.0);
            coord.replaceLoopGesture();
            expectWithinAbsoluteError(s.currentSong()->cycleEnd, 4.0, 1e-6);
            // Transport is still playing post-commit. Tap again →
            // established path: queue, don't open capture, don't change
            // transport.
            bool wasPlaying = coord.sequencer()->isPlaying();
            expect(wasPlaying);
            coord.replaceLoopGesture();
            expect(s.getLoopAction(t1) == LoopAction::ReplaceQueued);
            expect(! coord.getInFlightLoopCapture().has_value());
            expect(coord.sequencer()->isPlaying() == wasPlaying);

            coord.resetLooperSession();
        }

        beginTest("established: re-tap during Queued cancels back to None");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);
            coord.replaceLoopGesture();
            coord.sequencer()->setBeatPosition(4.0);
            coord.replaceLoopGesture();   // master set

            coord.replaceLoopGesture();   // queue
            expect(s.getLoopAction(t1) == LoopAction::ReplaceQueued);
            coord.replaceLoopGesture();   // cancel
            expect(s.getLoopAction(t1) == LoopAction::None);

            coord.resetLooperSession();
        }

        beginTest("bail when no focused track");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);
            s.setFocusedTrackId(TrackId{});

            coord.replaceLoopGesture();

            expect(s.getLoopAction(t1) == LoopAction::None);
            expect(! coord.getInFlightLoopCapture().has_value());
            expect(! coord.sequencer()->isPlaying());
        }

        beginTest("bail when not in Looper mode");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");

            coord.replaceLoopGesture();

            expect(s.getLoopAction(t1) == LoopAction::None);
            expect(! coord.sequencer()->isPlaying());
        }

        beginTest("bootstrap: overdub starts CapturingOverdub immediately");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);

            coord.overdubLoopGesture();

            expect(s.getLoopAction(t1) == LoopAction::CapturingOverdub);
            expect(coord.sequencer()->isPlaying());

            coord.resetLooperSession();
        }

        beginTest("resetLooperSession clears loops, cycleEnd, in-flight capture, and stops transport");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);
            coord.resetLooperSession();
            s.setFocusedTrackId(t1);
            coord.replaceLoopGesture();   // bootstrap

            expect(coord.sequencer()->isPlaying());
            expect(coord.getInFlightLoopCapture().has_value());

            coord.resetLooperSession();

            expect(! coord.sequencer()->isPlaying());
            expect(! coord.getInFlightLoopCapture().has_value());
            auto* track = s.findTrack(t1);
            expect(track && track->loops.empty());
            auto* song = s.currentSong();
            expect(song && song->cycleEnd == 0.0);
        }

        beginTest("transport stop during Capturing commits the in-flight capture");
        {
            // Boss-RC behavior — stopping mid-record should persist what
            // the user just played, not drop it. The transport-stop
            // subscription routes through commitInFlightCapture (was:
            // cancelLoopCapture) so the capture state and in-flight slot
            // both clear and the commit lands.
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);
            s.setFocusedTrackId(t1);

            coord.replaceLoopGesture();   // bootstrap → CapturingReplace
            expect(s.getLoopAction(t1) == LoopAction::CapturingReplace);
            expect(coord.getInFlightLoopCapture().has_value());

            coord.sequencer()->setBeatPosition(2.0);   // simulate elapsed
            coord.sequencer()->stop();                 // mid-record stop

            // Post-stop: state cleared, capture slot drained, master cycle
            // adopted from elapsed (this was the first loop).
            expect(s.getLoopAction(t1) == LoopAction::None);
            expect(! coord.getInFlightLoopCapture().has_value());
            expectWithinAbsoluteError(s.currentSong()->cycleEnd, 2.0, 1e-6);
        }

        beginTest("transport stop during Queued drops the deferred action");
        {
            // Queued state has no recorded content yet, so stopping
            // should clear the queue (not pretend-commit nothing).
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);

            // Stand up a master cycle so the next gesture queues instead
            // of bootstrapping.
            coord.replaceLoopGesture();
            coord.sequencer()->setBeatPosition(4.0);
            coord.replaceLoopGesture();   // commit, master = 4
            expectWithinAbsoluteError(s.currentSong()->cycleEnd, 4.0, 1e-6);

            // Re-arm — established path queues.
            coord.replaceLoopGesture();
            expect(s.getLoopAction(t1) == LoopAction::ReplaceQueued);
            expect(! coord.getInFlightLoopCapture().has_value());

            coord.sequencer()->stop();

            expect(s.getLoopAction(t1) == LoopAction::None);
            // Master cycle untouched — there was nothing to commit.
            expectWithinAbsoluteError(s.currentSong()->cycleEnd, 4.0, 1e-6);
        }
    }
};

static CoordinatorLooperGestureTests coordinatorLooperGestureTests;

// ============================================================================
// Mode-switch behavior — handleModeChange stops the (single) transport
// across a mode flip and stashes per-mode playhead position in seconds so
// it survives tempo changes. Looper always re-enters at beat 0.
// ============================================================================
class ModeSwitchTests : public juce::UnitTest {
public:
    ModeSwitchTests() : juce::UnitTest("ModeSwitch") {}

    void runTest() override {
        beginTest("switching to Looper stops transport and snaps beat to 0");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            s.createTrack("T1");
            // Park the transport mid-arrangement and start it.
            coord.sequencer()->setBeatPosition(8.0);
            coord.sequencer()->play();
            expect(coord.sequencer()->isPlaying());

            s.setMode(AppMode::Looper);

            expect(! coord.sequencer()->isPlaying());
            expectWithinAbsoluteError(coord.sequencer()->getBeatPosition(), 0.0, 1e-6);
        }

        beginTest("switching back to Arrangement restores the stashed beat");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            s.createTrack("T1");
            coord.sequencer()->setBeatPosition(12.0);

            s.setMode(AppMode::Looper);
            expectWithinAbsoluteError(coord.sequencer()->getBeatPosition(), 0.0, 1e-6);

            // Move around inside Looper-land — should not affect the stash.
            coord.sequencer()->setBeatPosition(3.5);

            s.setMode(AppMode::Arrangement);
            expectWithinAbsoluteError(coord.sequencer()->getBeatPosition(), 12.0, 1e-6);
        }

        beginTest("stash is in seconds — survives tempo change between leave and return");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            s.createTrack("T1");
            // Default tempo is 120 bpm → 2 beats/sec. Park at beat 8 = 4 sec.
            // Tempo lives on song state (SSOT) — handleModeChange reads it
            // from there, so the sequencer-side setTempo backdoor wouldn't
            // simulate a real tempo change.
            s.setSongTempo(120.0);
            coord.sequencer()->setBeatPosition(8.0);

            s.setMode(AppMode::Looper);
            // Halve the tempo while in Looper. 4 sec is now beat 4.
            s.setSongTempo(60.0);

            s.setMode(AppMode::Arrangement);
            expectWithinAbsoluteError(coord.sequencer()->getBeatPosition(), 4.0, 1e-6);
        }

        beginTest("switching to the same mode is a no-op (no transport stop)");
        {
            TestCoordinator tc;
            auto& coord = tc.get();
            auto& s = tc.state();
            s.createTrack("T1");
            s.setMode(AppMode::Looper);
            coord.sequencer()->setBeatPosition(2.0);
            coord.sequencer()->play();

            s.setMode(AppMode::Looper);  // no-op

            expect(coord.sequencer()->isPlaying());
            expectWithinAbsoluteError(coord.sequencer()->getBeatPosition(), 2.0, 0.5);
            // wide tolerance: real audio engine may have advanced a tick

            coord.sequencer()->stop();   // tidy teardown
        }
    }
};

static ModeSwitchTests modeSwitchTests;

// ============================================================================
// commitLoopAction lengthBeats fix — established-cycle commits land with
// the loop region's lengthBeats=0 (set at creation). The commit now
// stamps it from the current cycle so the playback path can wrap and the
// GUI can render the captured content.
// ============================================================================
class LooperCommitLengthTests : public juce::UnitTest {
public:
    LooperCommitLengthTests() : juce::UnitTest("LooperCommitLength") {}

    void runTest() override {
        beginTest("commit on subsequent track stamps lengthBeats from master cycle");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);
            s.setCycleLength(16.0);       // simulate "master cycle set"
            s.setFocusedTrackId(t1);

            s.replaceLoop();              // → ReplaceQueued
            s.beginLoopCapture(t1);       // → CapturingReplace (simulates wrap)

            std::vector<MidiEventState> events;
            MidiEventState e;
            e.beatOffset = 4.0;
            e.status = 0x90; e.channel = 1; e.data1 = 60; e.data2 = 100;
            events.push_back(e);

            s.commitLoopAction(t1, std::move(events));

            auto* track = s.findTrack(t1);
            expect(track && track->loops.size() == 1);
            // Region's lengthBeats was 0 at creation; commit stamps it from
            // the current master cycle so playback wrap math works.
            expectWithinAbsoluteError(track->loops[0].lengthBeats, 16.0, 1e-6);
        }

        beginTest("commit preserves an existing non-zero lengthBeats");
        {
            TestCoordinator tc;
            auto& s = tc.state();
            auto t1 = s.createTrack("T1");
            s.setMode(AppMode::Looper);
            s.setCycleLength(16.0);
            s.setFocusedTrackId(t1);

            s.replaceLoop();
            s.beginLoopCapture(t1);
            // Pretend a previous step already pinned the length (e.g. the
            // first-loop commit at coord level).
            s.findTrack(t1)->loops[0].lengthBeats = 7.5;

            s.commitLoopAction(t1, {});

            expectWithinAbsoluteError(
                s.findTrack(t1)->loops[0].lengthBeats, 7.5, 1e-6);
        }
    }
};

static LooperCommitLengthTests looperCommitLengthTests;

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
