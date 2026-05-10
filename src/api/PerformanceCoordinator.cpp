#include "api/PerformanceCoordinator.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "persistence/PersistenceLayer.h"
#include "automation/AutomationEngine.h"
#include "engine/AudioEngine.h"
#include "engine/EngineSync.h"
#include "engine/MIDIEngine.h"
#include "engine/Log.h"
#include "song/SongRuntime.h"
#include "daw/InternalSequencer.h"
#include "gui/Theme.h"
#include "rendering/OfflineRenderer.h"
#include "state/ActionInterpreter.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <set>

// ===== Algebra interpreter adapters ==========================================
// Wire the abstract Scheduler / TargetIO / TemplateResolver to the real
// AutomationEngine and StateAPI. Lives inside PerformanceCoordinator because
// it holds references to those engines; declared in the header as an opaque
// struct so the interpreter stays out of that header's includes.

struct PerformanceCoordinator::AlgebraAdapters {
    struct Scheduler : ActionAlgebra::ActionInterpreter::Scheduler {
        AutomationEngine& eng;
        Scheduler(AutomationEngine& e) : eng(e) {}
        void interpolate(float from, float to, float dur,
                         std::function<void(float)> onTick,
                         std::function<void()> onComplete,
                         const std::string& easing) override {
            eng.interpolate(from, to, dur, std::move(onTick),
                            AutomationEngine::easingByName(easing),
                            std::move(onComplete));
        }
        void delay(float dur, std::function<void()> onComplete) override {
            eng.delay(dur, std::move(onComplete));
        }
    };

    struct TargetIO : ActionAlgebra::ActionInterpreter::TargetIO {
        StateAPI& state;
        AudioEngine& audio;
        TargetIO(StateAPI& s, AudioEngine& a) : state(s), audio(a) {}

        juce::AudioProcessor* procFor(const std::string& trackId) const {
            return audio.getTrackInstrumentProcessor(juce::String(trackId));
        }

        float read(const ActionAlgebra::Target& t) override {
            using K = ActionAlgebra::Target::Kind;
            switch (t.kind) {
                case K::TrackGain:  return state.getTrackGain(TrackId{t.entityId});
                case K::BusGain:    return state.getBusGain(BusId{t.entityId});
                case K::MasterGain: return state.getMasterGain();
                case K::TrackParam: {
                    auto* proc = procFor(t.entityId);
                    if (!proc || t.paramIndex < 0) return 0.0f;
                    auto& params = proc->getParameters();
                    if (t.paramIndex >= params.size()) return 0.0f;
                    return params[t.paramIndex]->getValue();
                }
                case K::Selection:  return 0.0f;  // string target; numeric read meaningless
            }
            return 0.0f;
        }
        void write(const ActionAlgebra::Target& t, float v) override {
            using K = ActionAlgebra::Target::Kind;
            switch (t.kind) {
                case K::TrackGain:  state.setTrackGain(TrackId{t.entityId}, v); break;
                case K::BusGain:    state.setBusGain(BusId{t.entityId}, v);     break;
                case K::MasterGain: state.setMasterGain(v);                     break;
                case K::TrackParam: {
                    auto* proc = procFor(t.entityId);
                    if (!proc || t.paramIndex < 0) break;
                    auto& params = proc->getParameters();
                    if (t.paramIndex >= params.size()) break;
                    params[t.paramIndex]->setValue(v);
                    break;
                }
                case K::Selection:  break;
            }
        }
    };

    struct Resolver : ActionAlgebra::ActionInterpreter::TemplateResolver {
        StateAPI& state;
        mutable Template cached;  // scratch storage for lookup() returning a pointer
        Resolver(StateAPI& s) : state(s) {}
        const Template* lookup(const std::string& name) override {
            for (auto& a : state.allActions()) {
                if (a.name != name) continue;
                if (!a.hasBody && !a.expander) return nullptr;
                cached.body = a.body;
                cached.paramNames.clear();
                for (auto& p : a.params) cached.paramNames.push_back(p.name);
                cached.expander = a.expander;
                return &cached;
            }
            return nullptr;
        }
    };

    // LuaExecutor delegates back to PerformanceCoordinator::luaExecutor. The
    // running Lua engine sets `args` and `value` as globals before executing.
    // Filled in by PerformanceCoordinator after LuaEngine is available.
    struct LuaShim : ActionAlgebra::ActionInterpreter::LuaExecutor {
        std::function<void(const std::string&,
                           const std::vector<ActionAlgebra::Value>&,
                           float)> delegate;
        void executeAction(const std::string& code,
                           const std::vector<ActionAlgebra::Value>& args,
                           float midiValue) override {
            if (delegate) delegate(code, args, midiValue);
        }
    };

    Scheduler scheduler;
    TargetIO  io;
    Resolver  resolver;
    LuaShim   luaShim;
    ActionAlgebra::ActionInterpreter interpreter;

    AlgebraAdapters(AutomationEngine& eng, StateAPI& s, AudioEngine& audio)
        : scheduler(eng), io(s, audio), resolver(s),
          interpreter(scheduler, io, &resolver, &luaShim) {}
};

PerformanceCoordinator::PerformanceCoordinator() {}

PerformanceCoordinator::~PerformanceCoordinator() {
    shutdown();
}

void PerformanceCoordinator::initialise(const juce::String& dbPath) {
    // State store (in-memory)
    stateAPI = std::make_unique<StateAPI>();

    // Persistence (SQLite)
    persistence = std::make_unique<PersistenceLayer>();
    if (dbPath.isNotEmpty()) {
        persistence->open(dbPath.toStdString());
    } else {
        auto configDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                             .getChildFile(".config/performance");
        configDir.createDirectory();
        persistence->open(configDir.getChildFile("state.db").getFullPathName().toStdString());
    }

    // Audio engine
    audioEngine = std::make_unique<AudioEngine>();
    audioEngine->initialise();
    perfLog("[Coordinator] AudioEngine initialised\n");

    // Engine API
    engineAPI = std::make_unique<EngineAPI>(*audioEngine, *stateAPI);

    // Engine sync — must exist before any state mutations so it sees all events
    engineSync = std::make_unique<EngineSync>(*audioEngine, *stateAPI);

    // Load persisted state — establishes plugin/action IDs from DB
    // EngineSync reacts to creation events, building the engine graph
    persistence->loadInto(*stateAPI);

    // Point arrangement at current song's tracks
    if (auto* song = stateAPI->currentSong()) {
        arrangementImpl.setTracks(&song->tracks);
           }

    // Load theme by id. Factory themes are baked into the binary via
    // BinaryData; user themes in ~/.config/performance/themes/ override
    // factory themes on id collision.
    {
        Theme::ensureDefaultThemeFile();
        auto activeThemeId = stateAPI->getConfig("active_theme");
        if (activeThemeId.empty()) activeThemeId = "minimal_dark";
        if (!Theme::loadThemeById(juce::String(activeThemeId)))
            Theme::loadThemeById("minimal_dark");
    }

    // Restore or auto-select audio devices. On first launch the config
    // is empty; fall back to the macOS system-default output explicitly
    // (not just whatever `initialiseWithDefaultDevices` picked) and
    // persist the choice so future launches are deterministic even if
    // the system default changes.
    {
        auto& dm = audioEngine->getDeviceManager();
        auto setup = dm.getAudioDeviceSetup();
        bool changed = false;
        bool persistAfter = false;

        auto savedOutput = stateAPI->getConfig("audio_output_device");
        auto savedInput = stateAPI->getConfig("audio_input_device");

        perfLog("[Coordinator] Audio config: output saved='%s' current='%s', input saved='%s' current='%s'\n",
                savedOutput.c_str(), setup.outputDeviceName.toRawUTF8(),
                savedInput.c_str(), setup.inputDeviceName.toRawUTF8());

        // Resolve saved output device. If the saved name doesn't exist
        // (e.g., Scarlett 2i2 was selected last session but not plugged
        // in this time) fall through to the system-default path —
        // otherwise we'd ask CoreAudio for a missing device, get back
        // a null current device, and the engine would have no audio
        // callback at all.
        auto* deviceType = dm.getCurrentDeviceTypeObject();
        auto outputDeviceNames = deviceType ? deviceType->getDeviceNames(false)
                                            : juce::StringArray{};
        auto inputDeviceNames  = deviceType ? deviceType->getDeviceNames(true)
                                            : juce::StringArray{};

        bool savedOutputAvailable = !savedOutput.empty()
            && outputDeviceNames.contains(juce::String(savedOutput));
        if (!savedOutput.empty() && !savedOutputAvailable) {
            perfLog("[Coordinator] Saved output device '%s' not available — falling back to system default\n",
                    savedOutput.c_str());
        }

        if (savedOutputAvailable && setup.outputDeviceName != juce::String(savedOutput)) {
            setup.outputDeviceName = juce::String(savedOutput);
            changed = true;
        } else if (!savedOutputAvailable) {
            // First-launch / saved-device-missing path. Pick system default
            // explicitly. `initialiseWithDefaultDevices` usually handles this,
            // but on some configurations (aggregate devices, mic-permission
            // denials) it can leave the setup without a usable selection.
            // We DON'T persist here when the user had a saved device — the
            // user might re-plug their interface and expect it to come back.
            if (deviceType && (setup.outputDeviceName.isEmpty()
                                || !dm.getCurrentAudioDevice()
                                || !savedOutput.empty())) {
                int defaultIdx = deviceType->getDefaultDeviceIndex(false);
                if (defaultIdx >= 0 && defaultIdx < outputDeviceNames.size()) {
                    setup.outputDeviceName = outputDeviceNames[defaultIdx];
                    changed = true;
                    perfLog("[Coordinator] Auto-selected system default output '%s'\n",
                            setup.outputDeviceName.toRawUTF8());
                }
            }
            // Only persist on a true first launch (no saved value). If the
            // user has a saved device that's just not plugged in right now,
            // keep the saved name so plugging it back in restores it.
            if (savedOutput.empty()) persistAfter = true;
        }

        bool savedInputAvailable = !savedInput.empty()
            && inputDeviceNames.contains(juce::String(savedInput));
        if (!savedInput.empty() && !savedInputAvailable) {
            perfLog("[Coordinator] Saved input device '%s' not available — leaving JUCE default\n",
                    savedInput.c_str());
        }
        if (savedInputAvailable && setup.inputDeviceName != juce::String(savedInput)) {
            setup.inputDeviceName = juce::String(savedInput);
            changed = true;
        }

        auto savedBuffer = stateAPI->getConfig("audio_buffer_size");
        int bufSize = 0;
        if (!savedBuffer.empty()) {
            bufSize = std::stoi(savedBuffer);
        } else {
            // First-launch default — 128 samples ≈ 2.9 ms at 44.1 kHz.
            // Performers care about latency over reliability for live
            // play; if a device can't sustain it the user can raise it
            // in Settings, which persists.
            bufSize = 128;
            persistAfter = true;
        }
        if (setup.bufferSize != bufSize) {
            setup.bufferSize = bufSize;
            changed = true;
        }

        if (changed) {
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty())
                perfLog("[Coordinator] Applied audio devices: out='%s', in='%s'\n",
                        setup.outputDeviceName.toRawUTF8(), setup.inputDeviceName.toRawUTF8());
            else
                perfLog("[Coordinator] Failed to apply audio devices: %s\n", err.toRawUTF8());
        }

        // Persist the effective selection on first launch so subsequent launches
        // are deterministic (not dependent on macOS default changing).
        if (persistAfter) {
            auto effective = dm.getAudioDeviceSetup();
            if (effective.outputDeviceName.isNotEmpty())
                stateAPI->setConfig("audio_output_device", effective.outputDeviceName.toStdString());
            if (effective.inputDeviceName.isNotEmpty() && savedInput.empty())
                stateAPI->setConfig("audio_input_device", effective.inputDeviceName.toStdString());
            if (savedBuffer.empty() && effective.bufferSize > 0)
                stateAPI->setConfig("audio_buffer_size", std::to_string(effective.bufferSize));
        }
    }

    // Then populate from engine scan — deduplicates by name, keeps DB IDs
    populatePluginCatalog();

    // Register built-in actions — deduplicates by name, keeps DB IDs
    registerBuiltinActions();

    automationEngine = std::make_unique<AutomationEngine>();
    algebraAdapters = std::make_unique<AlgebraAdapters>(*automationEngine, *stateAPI, *audioEngine);
    // Route Op::Lua through the action-scoped executor set by main.
    algebraAdapters->luaShim.delegate = [this](const std::string& code,
                                                 const std::vector<ActionAlgebra::Value>& args,
                                                 float midiValue) {
        if (luaActionExecutor) luaActionExecutor(code, args, midiValue);
    };
    songRuntime = std::make_unique<SongRuntime>();
    sequencerImpl = std::make_unique<InternalSequencer>();
    lastSequencerTimeMs = juce::Time::getMillisecondCounterHiRes();

    // Wire arrangement and transport to engine for audio-thread MIDI scheduling
    audioEngine->setArrangement(&arrangementImpl);
    auto* seq = static_cast<InternalSequencer*>(sequencerImpl.get());
    seq->setTransportCallback([this](bool playing) {
        if (playing) {
            // Jump to cycle start if cycle is enabled and playhead is outside the region
            double beat = sequencerImpl->getBeatPosition();
            bool loopOn = sequencerImpl->isLoopEnabled();
            double loopS = sequencerImpl->getLoopStart();
            double loopE = sequencerImpl->getLoopEnd();
            if (loopOn && loopE > loopS && (beat < loopS || beat >= loopE)) {
                beat = loopS;
                sequencerImpl->setBeatPosition(beat);
            }
            // Single atomic command — no race window
            audioEngine->startPlayback(beat, sequencerImpl->getTempo(),
                                        loopOn, loopS, loopE);
            if (recordModeActive)
                startRecording();

            // Diagnostic: on playback start in Looper mode, dump the
            // event list of every track's active loop take. Lets us
            // confirm what the scanner is about to play and spot
            // ordering anomalies (noteOff-before-noteOn, etc.) that
            // would cause stuck-note hangs. Remove once the loop
            // playback hang investigation is done.
            if (stateAPI && stateAPI->getMode() == AppMode::Looper) {
                auto* song = stateAPI->currentSong();
                if (song) {
                    perfLog("[LoopDump] playback start (Looper) — cycleEnd=%.3f\n",
                            song->cycleEnd);
                    for (auto& t : song->tracks) {
                        if (t.loops.empty()) continue;
                        auto& r = t.loops[0];
                        auto* take = r.activeTake();
                        if (!take) continue;
                        perfLog("[LoopDump] track=%s \"%s\" loopLen=%.3f take=%s events=%zu\n",
                                t.id.c_str(), t.name.c_str(),
                                r.lengthBeats, take->id.str().c_str(),
                                take->events.size());
                        for (size_t i = 0; i < take->events.size(); ++i) {
                            auto& e = take->events[i];
                            const char* kind = e.isNoteOn() ? "ON " : e.isNoteOff() ? "OFF" : "---";
                            perfLog("[LoopDump]   [%zu] beat=%.4f status=0x%02x ch=%d note=%d vel=%d %s\n",
                                    i, e.beatOffset, e.status, e.channel, e.data1, e.data2, kind);
                        }
                    }
                }
            }
        } else {
            audioEngine->stopPlayback();
            stopRecording();
            recordModeActive = false;
            // Transport stop is an implicit punch-out for any active
            // looper gesture — the cycle-wrap handler can't fire once
            // the sequencer isn't advancing, so we'd otherwise leave
            // the state machine stuck mid-capture.
            // Commit (don't cancel) any in-flight Capturing — a stop
            // mid-record should persist what's been captured so the
            // user's work survives. Established Queued state can just
            // be cleared since no recording happened yet.
            if (activeLoopCapture.has_value()) {
                commitInFlightCapture();
                if (stateAPI) stateAPI->endTransaction();
            } else if (stateAPI) {
                // Drop any Queued-but-never-recorded action on the
                // focused track so the next play doesn't surprise the
                // user with a deferred punch-in.
                auto fid = stateAPI->getFocusedTrackId();
                if (! fid.empty()) {
                    auto a = stateAPI->getLoopAction(fid);
                    if (a == LoopAction::ReplaceQueued
                     || a == LoopAction::OverdubQueued)
                        stateAPI->cancelLoopCapture(fid);
                }
            }
        }
    });

    midiEngine = std::make_unique<MIDIEngine>(
        audioEngine->getDeviceManager(), *audioEngine, *stateAPI);
    midiEngine->setSongRuntime(songRuntime.get());
    midiEngine->setMonitorMode(true);
    midiEngine->initialise();
    perfLog("[Coordinator] MIDIEngine initialised\n");

    // Auto-register any connected MIDI devices
    refreshMidiDevices();

    // Subscribe to state events for auto-creating Default presets
    stateSubscriptionId = stateAPI->events().subscribe([this](const StateEvent& event) {
        onStateEvent(event);
    });

    // Debounced autosave: update the "last change" timestamp on every state
    // mutation. timerCallback flushes when 3 seconds of quiet have passed.
    autosaveSubscriptionId = stateAPI->events().subscribe([this](const StateEvent&) {
        lastStateChangeMs = juce::Time::getMillisecondCounterHiRes();
    });

    startTimer(16);  // ~60Hz — drives sequencer clock and autosave check
}

void PerformanceCoordinator::timerCallback() {
    // Sync sequencer ↔ engine. MIDI playback is on the audio thread (GraphWrapper).
    // This timer keeps the UI sequencer in sync and forwards position/tempo changes.
    if (sequencerImpl && audioEngine) {
        double uiBeat = sequencerImpl->getBeatPosition();

        if (sequencerImpl->isPlaying()) {
            // When playing, the audio thread is authoritative — read its position
            double audioBeat = audioEngine->getPlaybackBeatPosition();
            // Keep the UI sequencer in sync with the audio-thread position
            static_cast<InternalSequencer*>(sequencerImpl.get())->setBeatPositionSilent(audioBeat);
            // Forward tempo and metronome state to engine
            audioEngine->setPlaybackState(true, sequencerImpl->getTempo());
            float metVol = 0.5f;
            auto metVolStr = stateAPI->getConfig("metronome_volume");
            if (!metVolStr.empty()) metVol = std::stof(metVolStr);
            audioEngine->setMetronome(sequencerImpl->isMetronomeEnabled(),
                                       sequencerImpl->getTimeSignatureNumerator(), metVol);
            // Sync loop state to engine
            audioEngine->setPlaybackLoop(sequencerImpl->isLoopEnabled(),
                                          sequencerImpl->getLoopStart(),
                                          sequencerImpl->getLoopEnd());

            // Drain recorded MIDI events from audio thread BEFORE the
            // wrap dispatch. Late-cycle events that arrived in the FIFO
            // before the wrap was detected need to land in the OLD
            // capture slot so they get committed; if we wrap-dispatch
            // first, commitInFlightCapture sees an empty events vector
            // and the user's performance vanishes into the new slot.
            if (isRecording) {
                drainRecordFIFO();
            }

            // Cycle-wrap detection for looper mode. When the audio-thread
            // beat position wraps backward (audioBeat < lastSequencerBeat
            // while loop is enabled), the scanner has just crossed the
            // cycle boundary — the right moment to promote any deferred
            // take swaps and fire loop-record transitions. Safe to do on
            // the message thread because the scan reads activeTakeId and
            // record state fresh each call.
            if (sequencerImpl->isLoopEnabled()
                && audioBeat < lastSequencerBeat
                && lastSequencerBeat > 0.0) {
                if (stateAPI && stateAPI->getMode() == AppMode::Looper) {
                    int swapped = stateAPI->commitPendingTakeSwaps();
                    if (swapped > 0) {
                        perfLog("[Looper] cycle wrap — committed %d take swap(s)\n", swapped);
                    }
                    dispatchLoopGesturesAtWrap();
                }
            }
            // Diagnostic snapshot of the just-completed drain.
            if (isRecording) {

                // Diagnostic: log audio-thread counters once per second.
                double nowMs = juce::Time::getMillisecondCounterHiRes();
                if (nowMs - lastRecordDiagLogMs >= 1000.0) {
                    auto d = audioEngine->readRecordDiag();
                    perfLog("[RecDiag] playing=%d / procTicks=%d  midiSeen=%d  fifoPush=%d  "
                            "pops=%d  dropped(offset<0)=%d  recStartBeat=%.2f\n",
                            d.playingTicks, d.processBlockTicks,
                            d.midiEventsSeen, d.fifoPushes,
                            diagFifoPops, diagDroppedNegativeOffset,
                            recordStartBeat);
                    lastRecordDiagLogMs = nowMs;
                }

                // Update audio region lengths and peaks during recording
                for (auto& session : audioRecordSessions) {
                    auto* region = arrangementImpl.findRegion(session.regionId);
                    if (!region || !region->activeTake()) continue;
                    auto* take = region->activeTake();
                    int64_t frames = session.writer->getTotalFramesWritten();
                    double seconds = (take->sampleRate > 0) ? (double)frames / take->sampleRate : 0.0;
                    region->lengthBeats = seconds * (take->recordTempo / 60.0);

                    auto writerPeaks = session.writer->getPeaks();
                    take->peakData.samplesPerPeak = 256;
                    take->peakData.peaks.clear();
                    for (auto& p : writerPeaks)
                        take->peakData.peaks.push_back({ p.min, p.max });
                }
            }
            // Scan and dispatch action events
            if (audioBeat > lastActionScanBeat) {
                arrangementImpl.scanActionEvents(lastActionScanBeat, audioBeat,
                    [this](const SongState::ActionEvent& ev) {
                        auto* action = stateAPI->findActionById(ev.actionId);
                        if (action) {
                            auto args = juce::JSON::parse(juce::String(ev.argsJson));
                            executeAction(action->name, args, 1.0f);
                        }
                    });
                lastActionScanBeat = audioBeat;
            }
        } else {
            // When stopped, the UI is authoritative — forward position to engine
            // so that when play starts, the engine begins from the right place
            double lastSynced = lastSequencerBeat;
            if (std::abs(uiBeat - lastSynced) > 0.001)
                audioEngine->setPlaybackBeatPosition(uiBeat);
            lastActionScanBeat = uiBeat;
        }

        // Record the position the wrap detector should compare against
        // next frame. When playing, that's the audio-thread's beat (the
        // value we just synced into the sequencer); using stale uiBeat
        // here makes the next frame see audioBeat < lastSequencerBeat
        // and fire a phantom wrap, collapsing a Looper recording cycle
        // to a single frame.
        lastSequencerBeat = sequencerImpl->getBeatPosition();
        lastSequencerTimeMs = juce::Time::getMillisecondCounterHiRes();
    }

    // Debounced autosave — saves 3 seconds after the last state change.
    // lastStateChangeMs is updated by the event subscription on every mutation.
    if (stateAPI && persistence && stateAPI->isDirty() && lastStateChangeMs > 0.0) {
        double now = juce::Time::getMillisecondCounterHiRes();
        if (now - lastStateChangeMs >= 3000.0) {
            if (!persistence->saveFrom(*stateAPI))
                perfLog("[Coordinator] Auto-save FAILED — see [Persistence] errors above; DB unchanged, dirty flag held\n");
            else {
                stateAPI->clearDirty();
                lastStateChangeMs = 0.0;
                perfLog("[Coordinator] Auto-saved\n");
            }
        }
    }
}

void PerformanceCoordinator::startRecordMode() {
    if (!sequencerImpl || !stateAPI) return;

    // In Looper mode, the transport record button is a wrapper around
    // the focused-track replace gesture — same behavior whether the
    // user presses it from the GUI or hits a bound pad.
    if (stateAPI->getMode() == AppMode::Looper) {
        replaceLoopGesture();
        return;
    }

    // Arrangement mode — classic record-arm-and-roll.
    recordModeActive = true;
    if (!sequencerImpl->isPlaying())
        sequencerImpl->play();  // transport callback will call startRecording()
    else
        startRecording();  // already playing, start recording now
}

void PerformanceCoordinator::reloadAudioFiles() {
    loadAudioFilesIntoEngine();
}

void PerformanceCoordinator::stopRecordMode() {
    if (!stateAPI) return;

    // In Looper mode, the same gesture that started the action is the
    // one that ends it (replace twice cancels a queue, replace during
    // capture is ignored since the playhead is moving). Route through
    // the gesture entry point.
    if (stateAPI->getMode() == AppMode::Looper) {
        replaceLoopGesture();
        return;
    }

    if (!recordModeActive) return;
    recordModeActive = false;
    stopRecording();
}

void PerformanceCoordinator::startRecording() {
    if (!stateAPI || !audioEngine) return;

    // Push undo snapshot before recording, then suspend during recording
    stateAPI->pushUndo();
    stateAPI->suspendUndo();

    // Find armed MIDI tracks
    recordingTrackIds.clear();
    audioRecordSessions.clear();
    auto tracks = stateAPI->listTracks();
    for (auto& t : tracks) {
        auto* ts = stateAPI->findTrack(t.id);
        if (!ts || !ts->armed) continue;
        if (ts->sourceType == TrackSourceType::Instrument)
            recordingTrackIds.push_back(t.id);
    }

    // Check if any audio tracks are armed (handled below)
    bool hasArmedAudio = false;
    for (auto& t : tracks) {
        auto* ts = stateAPI->findTrack(t.id);
        if (ts && ts->armed && ts->sourceType == TrackSourceType::AudioInput)
            hasArmedAudio = true;
    }

    if (recordingTrackIds.empty() && !hasArmedAudio) return;

    recordStartBeat = sequencerImpl ? sequencerImpl->getBeatPosition() : 0.0;
    openNotes.clear();

    // Start MIDI recording regions
    for (auto& trackId : recordingTrackIds) {
        auto* region = arrangementImpl.startRecording(trackId, recordStartBeat);
        perfLog("[Coordinator] Started MIDI recording on track %s\n", trackId.c_str());
    }

    // Start audio recording for all armed audio tracks
    {
        std::vector<GraphWrapper::AudioRecordTarget> targets;
        auto allTracks = stateAPI->listTracks();
        for (auto& t : allTracks) {
            auto* ts = stateAPI->findTrack(t.id);
            if (!ts || !ts->armed || ts->sourceType != TrackSourceType::AudioInput) continue;

            auto* region = arrangementImpl.addMidiRegion(t.id, recordStartBeat, 0.0);
            if (!region) continue;
            region->type = "audio";
            auto* take = region->activeTake();
            if (!take) continue;

            double sr = audioEngine->getCurrentSampleRate();
            take->recordTempo = sequencerImpl ? sequencerImpl->getTempo() : 120.0;
            take->sampleRate = (int)sr;
            take->channelCount = std::max(1, ts->inputChannelCount);

            auto audioDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                .getChildFile(".config/performance/audio");
            audioDir.createDirectory();
            auto wavFile = audioDir.getChildFile(juce::String(take->id.str()) + ".wav");
            take->filePath = wavFile.getFullPathName().toStdString();

            auto session = AudioRecordSession();
            session.trackId = t.id;
            session.regionId = region->id;
            session.fifo = std::make_unique<AudioRecordFIFO>();
            session.writer = std::make_unique<AudioWriterThread>();
            session.writer->startWriting(*session.fifo, wavFile, sr, take->channelCount);

            targets.push_back({ ts->inputChannelStart, ts->inputChannelCount, session.fifo.get() });
            audioRecordSessions.push_back(std::move(session));

            perfLog("[Coordinator] Started audio recording on track %s → %s\n",
                    t.id.c_str(), take->filePath.c_str());
        }
        if (!targets.empty())
            audioEngine->setAudioRecordTargets(targets);
    }

    arrangementRecordingActive = true;
    beginCapture(recordStartBeat);
    perfLog("[Coordinator] Recording started (%d MIDI, %d audio) at beat %.1f\n",
            (int)recordingTrackIds.size(),
            (int)audioRecordSessions.size(), recordStartBeat);
}

void PerformanceCoordinator::stopRecording() {
    if (!arrangementRecordingActive) return;

    arrangementRecordingActive = false;
    endCapture();

    // Drain remaining MIDI events still in the FIFO.
    drainRecordFIFO();

    // Inject synthetic noteOffs for any notes still open at stop time
    double stopBeat = sequencerImpl ? sequencerImpl->getBeatPosition() : 0.0;
    double stopOffset = stopBeat - recordStartBeat;
    for (auto& [key, beatOffset] : openNotes) {
        MidiEventState noteOff;
        noteOff.beatOffset = stopOffset;
        noteOff.status = 0x80;
        noteOff.channel = key.second;
        noteOff.data1 = key.first;
        noteOff.data2 = 0;
        arrangementImpl.addRecordedEvent(noteOff);
    }
    openNotes.clear();

    // Stop audio recording (all tracks)
    if (!audioRecordSessions.empty()) {
        audioEngine->clearAudioRecordTargets();

        for (auto& session : audioRecordSessions) {
            session.writer->stopWriting();

            auto* region = arrangementImpl.findRegion(session.regionId);
            if (region && region->activeTake()) {
                auto* take = region->activeTake();
                int64_t frames = session.writer->getTotalFramesWritten();
                double seconds = (take->sampleRate > 0) ? (double)frames / take->sampleRate : 0.0;
                region->lengthBeats = seconds * (take->recordTempo / 60.0);

                // Get peaks from writer thread (already computed during write)
                auto writerPeaks = session.writer->getPeaks();
                take->peakData.samplesPerPeak = 256;
                take->peakData.peaks.clear();
                for (auto& p : writerPeaks)
                    take->peakData.peaks.push_back({ p.min, p.max });

                perfLog("[Coordinator] Audio recording on %s: %lld frames, %.1f beats\n",
                        session.trackId.c_str(), frames, region->lengthBeats);
            }
        }
        audioRecordSessions.clear();
        loadAudioFilesIntoEngine();
    }

    arrangementImpl.stopRecording();
    recordingTrackIds.clear();

    // Resume undo — the next undo will revert to pre-recording state
    stateAPI->resumeUndo();

    perfLog("[Coordinator] Recording stopped at beat %.1f, total regions: %d\n",
            stopBeat, (int)arrangementImpl.allRegions().size());

    // Final diagnostic snapshot for the just-ended session.
    auto d = audioEngine->readRecordDiag();
    perfLog("[RecDiag] final: playing=%d / procTicks=%d  midiSeen=%d  fifoPush=%d  "
            "pops=%d  dropped(offset<0)=%d  recStartBeat=%.2f\n",
            d.playingTicks, d.processBlockTicks,
            d.midiEventsSeen, d.fifoPushes,
            diagFifoPops, diagDroppedNegativeOffset,
            recordStartBeat);
}

// Refcounted capture gates. Multiple concurrent flows (arrangement
// recording, looper punch-in) can share the engine's MIDI capture +
// the FIFO drain by each holding one begin/end pair. The engine gate
// flips only on 0↔1 transitions so the other flow isn't interrupted.
//
// originBeat sets `recordStartBeat` for per-event offset computation
// in drainRecordFIFO. For arrangement recording that's the user's
// press-record beat; for looper punch-in it's the cycle start (0).
// Only the first beginCapture in a session sets originBeat — callers
// are responsible for ensuring the two flows don't overlap (see the
// guards in startRecordMode and toggleLoopRecord that refuse when
// the other flow is active).
void PerformanceCoordinator::beginCapture(double originBeat) {
    if (!audioEngine) return;
    captureRefCount++;
    if (captureRefCount == 1) {
        recordStartBeat = originBeat;
        audioEngine->resetRecordDiag();
        diagFifoPops = 0;
        diagDroppedNegativeOffset = 0;
        lastRecordDiagLogMs = 0.0;
        audioEngine->setRecording(true);
        isRecording = true;
    }
}

void PerformanceCoordinator::endCapture() {
    if (!audioEngine || captureRefCount == 0) return;
    captureRefCount--;
    if (captureRefCount == 0) {
        audioEngine->setRecording(false);
        isRecording = false;
    }
}

void PerformanceCoordinator::drainRecordFIFO() {
    if (!audioEngine) return;
    auto& fifo = audioEngine->getRecordFIFO();
    RecordedMidiEvent event;
    while (fifo.pop(event)) {
        ++diagFifoPops;
        double beatOffset = event.beat - recordStartBeat;
        if (beatOffset < 0.0) {
            ++diagDroppedNegativeOffset;
            continue;
        }

        MidiEventState re;
        re.beatOffset = beatOffset;
        re.status = event.statusByte;
        re.channel = event.channel;
        re.data1 = event.data1;
        re.data2 = event.data2;
        arrangementImpl.addRecordedEvent(re);

        // Phase 6 — also feed the gesture capture slot if one is open
        // AND it targets an instrument track. Audio tracks accept the
        // same gesture (so the lane visual still pulses / fills), but
        // capturing audio is the audio FIFO's job; MIDI events would
        // be nonsense content on an audio loop.
        if (activeLoopCapture.has_value()) {
            auto* targetTrack = stateAPI ? stateAPI->findTrack(activeLoopCapture->trackId) : nullptr;
            if (targetTrack && targetTrack->sourceType == TrackSourceType::Instrument)
                activeLoopCapture->events.push_back(re);
        }

        perfLog("[Coordinator] Recorded event: status=0x%02x data1=%d beat=%.3f\n",
                re.status, re.data1, re.beatOffset);

        // Track open notes for synthetic noteOff at stop
        if (re.isNoteOn()) {
            openNotes[{re.data1, re.channel}] = beatOffset;
        } else if (re.isNoteOff()) {
            openNotes.erase({re.data1, re.channel});
        }
    }
}

void PerformanceCoordinator::syncTempoFromState() {
    if (!stateAPI || !sequencerImpl) return;
    auto [num, den] = stateAPI->getSongTimeSignature();
    sequencerImpl->setTimeSignature(num, den);
    // Push the full tempo map to BOTH:
    //   - InternalSequencer (UI-side beat clock + getTempo for the LCD)
    //   - AudioEngine / GraphWrapper (the actual playback path —
    //     this is what makes a tempo change at bar 5 audible)
    // setTempoEvents updates the sequencer's atomic tempo to
    // events.front().bpm, so we don't need a separate setTempo() call.
    auto* song = stateAPI->currentSong();
    const std::vector<TempoEvent> emptyMap;
    const auto& events = song ? song->tempoEvents : emptyMap;
    if (auto* internal = dynamic_cast<InternalSequencer*>(sequencerImpl.get()))
        internal->setTempoEvents(events);
    if (audioEngine)
        audioEngine->setPlaybackTempoEvents(events);

    // Restore cycle range from song
    if (song && song->cycleEnd > song->cycleStart) {
        sequencerImpl->setLoopRange(song->cycleStart, song->cycleEnd);
        sequencerImpl->setLoopEnabled(song->cycleEnabled);
    } else {
        sequencerImpl->setLoopRange(0.0, 0.0);
        sequencerImpl->setLoopEnabled(false);
    }

    // Push app-mode state into the arrangement scanner so it knows
    // to dispatch to the loop-playback path. Cycle length is cycleEnd
    // from the current song (normalized to start=0 by StateAPI invariants).
    bool looperMode = stateAPI->getMode() == AppMode::Looper;
    arrangementImpl.updateLooperMode(looperMode,
                                      looperMode && song ? song->cycleEnd : 0.0);

    perfLog("[Coordinator] Synced tempo %.1f bpm, time sig %d/%d\n",
            stateAPI->getSongTempo(), num, den);
}

void PerformanceCoordinator::loadAudioFilesIntoEngine() {
    if (!stateAPI || !audioEngine) return;
    auto* song = stateAPI->currentSong();
    if (!song) return;

    for (auto& track : song->tracks) {
        if (track.sourceType != TrackSourceType::AudioInput) continue;

        auto loadFromPool = [&](std::vector<RegionState>& pool) {
            for (auto& region : pool) {
                if (region.type != "audio") continue;
                auto* take = region.activeTake();
                if (!take || take->filePath.empty()) continue;

                audioEngine->loadAudioFileForTrack(juce::String(track.id.str()),
                    juce::String(region.id.str()), juce::String(take->filePath),
                    take->recordTempo, take->sampleRate);

                if (take->peakData.peaks.empty())
                    computeAudioPeaks(*take);
            }
        };
        // Both arrangement regions and looper loops can carry audio
        // takes; AudioFileNode keeps them by region id, so dispatch
        // (in GraphWrapper) just looks up whichever pool is active.
        loadFromPool(track.regions);
        loadFromPool(track.loops);
    }
}

void PerformanceCoordinator::computeAudioPeaks(TakeState& take) {
    if (take.filePath.empty()) return;

    juce::File file(take.filePath);
    if (!file.existsAsFile()) return;

    juce::WavAudioFormat wav;
    auto stream = file.createInputStream();
    if (!stream) return;

    std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(stream.release(), true));
    if (!reader) return;

    take.peakData.samplesPerPeak = 256;
    take.peakData.peaks.clear();

    int64_t totalFrames = reader->lengthInSamples;
    int chunkSize = take.peakData.samplesPerPeak;
    juce::AudioBuffer<float> buf(reader->numChannels, chunkSize);

    for (int64_t pos = 0; pos < totalFrames; pos += chunkSize) {
        int framesToRead = (int)std::min((int64_t)chunkSize, totalFrames - pos);
        reader->read(&buf, 0, framesToRead, pos, true, true);

        float mn = 0, mx = 0;
        for (int ch = 0; ch < (int)reader->numChannels; ++ch) {
            auto* data = buf.getReadPointer(ch);
            for (int i = 0; i < framesToRead; ++i) {
                mn = std::min(mn, data[i]);
                mx = std::max(mx, data[i]);
            }
        }
        take.peakData.peaks.push_back({ mn, mx });
    }

    perfLog("[Coordinator] Computed %d peaks for %s\n",
            (int)take.peakData.peaks.size(), take.filePath.c_str());
}

void PerformanceCoordinator::recomputeAudioPeaksFromFile(TakeState& take) {
    take.peakData.peaks.clear();
    computeAudioPeaks(take);
}

void PerformanceCoordinator::mergeOverdubAudio(const AudioRecordSession& session) {
    if (! session.isOverdub) return;
    juce::File targetFile(session.targetPath);
    juce::File tempFile(session.tempPath);
    if (! tempFile.existsAsFile()) {
        perfLog("[Looper] overdub merge: temp file missing (%s) — skipping\n",
                session.tempPath.c_str());
        return;
    }

    juce::WavAudioFormat wav;
    auto readBuffer = [&](const juce::File& file) -> juce::AudioBuffer<float> {
        auto stream = file.createInputStream();
        if (! stream) return {};
        std::unique_ptr<juce::AudioFormatReader> reader(
            wav.createReaderFor(stream.release(), true));
        if (! reader) return {};
        juce::AudioBuffer<float> buf((int) reader->numChannels,
                                       (int) reader->lengthInSamples);
        if (reader->lengthInSamples > 0)
            reader->read(&buf, 0, (int) reader->lengthInSamples, 0, true, true);
        return buf;
    };

    juce::AudioBuffer<float> oldBuf = targetFile.existsAsFile()
                                       ? readBuffer(targetFile)
                                       : juce::AudioBuffer<float>{};
    juce::AudioBuffer<float> newBuf = readBuffer(tempFile);

    if (newBuf.getNumSamples() == 0) {
        perfLog("[Looper] overdub merge: temp file empty — discarding\n");
        tempFile.deleteFile();
        return;
    }

    int outChans = std::max(oldBuf.getNumChannels(), newBuf.getNumChannels());
    int outFrames = std::max(oldBuf.getNumSamples(), newBuf.getNumSamples());
    if (outChans <= 0) outChans = 1;

    juce::AudioBuffer<float> mixed(outChans, outFrames);
    mixed.clear();
    for (int ch = 0; ch < outChans; ++ch) {
        if (ch < oldBuf.getNumChannels())
            mixed.addFrom(ch, 0, oldBuf, ch, 0, oldBuf.getNumSamples());
        if (ch < newBuf.getNumChannels())
            mixed.addFrom(ch, 0, newBuf, ch, 0, newBuf.getNumSamples());
    }

    // Write the mix to a temp-of-temp, then atomically replace the
    // target. juce::File::createOutputStream doesn't truncate, so we
    // delete the target first to avoid leaving stale tail data.
    juce::File mixFile(targetFile.getFullPathName() + ".mix");
    mixFile.deleteFile();
    {
        auto stream = mixFile.createOutputStream();
        if (! stream) {
            perfLog("[Looper] overdub merge: failed to open %s for write\n",
                    mixFile.getFullPathName().toRawUTF8());
            tempFile.deleteFile();
            return;
        }
        // Reuse existing tempo / sample-rate from the take. Get sample rate
        // from the new file's reader (writer wrote at engine sample rate).
        int sr = 48000;
        if (auto inStream = tempFile.createInputStream()) {
            if (auto* r = wav.createReaderFor(inStream.release(), true)) {
                sr = (int) r->sampleRate;
                delete r;
            }
        }
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wav.createWriterFor(stream.release(), sr, outChans, 24, {}, 0));
        if (! writer) {
            perfLog("[Looper] overdub merge: failed to create WAV writer\n");
            tempFile.deleteFile();
            return;
        }
        writer->writeFromAudioSampleBuffer(mixed, 0, outFrames);
    }  // writer destructor flushes + closes
    targetFile.deleteFile();
    mixFile.moveFileTo(targetFile);
    tempFile.deleteFile();

    perfLog("[Looper] overdub merge: %d frames × %d ch → %s\n",
            outFrames, outChans, session.targetPath.c_str());
}

void PerformanceCoordinator::shutdown() {
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    stopTimer();
    if (stateAPI && stateSubscriptionId >= 0)
        stateAPI->events().unsubscribe(stateSubscriptionId);

    // Full save on shutdown. Two perf moves vs. naive: (1) skip
    // captureProcessorState if autosave just ran one — getStateInformation
    // on heavy samplers (Kontakt, Keyscape) can take hundreds of ms each
    // and is the dominant cost at quit. (2) Skip the .bak.db backup —
    // backups protect against partial writes; clean shutdown isn't a
    // partial-write risk.
    if (stateAPI && persistence) {
        auto now = juce::Time::getMillisecondCounterHiRes();
        const double captureFreshnessMs = 5000.0;  // last 5s counts as fresh
        bool captureFresh = lastCaptureMs > 0.0
                          && (now - lastCaptureMs) < captureFreshnessMs;
        if (!captureFresh) {
            auto cs0 = juce::Time::getMillisecondCounterHiRes();
            captureProcessorState();
            perfLog("[Coordinator] Shutdown captureProcessorState %.0f ms\n",
                    juce::Time::getMillisecondCounterHiRes() - cs0);
        } else {
            perfLog("[Coordinator] Shutdown: skipping captureProcessorState (autosave %.0f ms ago)\n",
                    now - lastCaptureMs);
        }
        auto sv0 = juce::Time::getMillisecondCounterHiRes();
        if (!persistence->saveFrom(*stateAPI, /*createBackup=*/false))
            perfLog("[Coordinator] Shutdown save FAILED — session may not be fully persisted\n");
        perfLog("[Coordinator] Shutdown saveFrom %.0f ms\n",
                juce::Time::getMillisecondCounterHiRes() - sv0);
    }

    auto teardown0 = juce::Time::getMillisecondCounterHiRes();
    songRuntime.reset();
    midiEngine.reset();
    engineSync.reset();
    engineAPI.reset();
    audioEngine.reset();
    automationEngine.reset();
    persistence.reset();
    stateAPI.reset();
    perfLog("[Coordinator] Shutdown engine teardown %.0f ms\n",
            juce::Time::getMillisecondCounterHiRes() - teardown0);
    perfLog("[Coordinator] Shutdown total %.0f ms\n",
            juce::Time::getMillisecondCounterHiRes() - t0);
}

StateAPI& PerformanceCoordinator::state() { return *stateAPI; }
EngineAPI& PerformanceCoordinator::engine() { return *engineAPI; }

// --- Song lifecycle ---

std::string PerformanceCoordinator::createSong(const juce::String& name) {
    auto songId = stateAPI->createSong(name.toStdString());
    stateAPI->setCurrentSong(songId);
    // Every song gets an Actions track
    stateAPI->createActionTrack("Actions");
    // Point arrangement at the new song's tracks — without this, any
    // later addMidiRegion / startRecording / etc. on the new song
    // would hit the PERF_ASSERT in Arrangement for songTracks not set.
    if (auto* song = stateAPI->findSong(songId))
        arrangementImpl.setTracks(&song->tracks);
    perfLog("[Coordinator] Created song \"%s\" (id: %s)\n", name.toRawUTF8(), songId.str().c_str());
    return songId.str();
}

void PerformanceCoordinator::loadSong(const std::string& songId) {
    auto* song = stateAPI->findSong(SongId{songId});
    if (!song) {
        perfLog("[Coordinator] Song not found: %s\n", songId.c_str());
        return;
    }

    // Capture processor state from current song before switching
    captureProcessorState();

    songRuntime->clearBindings();
    stateAPI->setCurrentSong(SongId{songId});  // triggers EngineSync via config event
    restoreBindings();

    // Point arrangement at new song's tracks
    if (auto* newSong = stateAPI->findSong(SongId{songId}))
        arrangementImpl.setTracks(&newSong->tracks);

    // Apply song tempo and time signature to sequencer
    syncTempoFromState();

    // Load any persisted audio region files into the engine (and compute
    // waveform peaks). Without this, audio regions come back visually
    // but silent + waveform-less until the user explicitly reloads.
    loadAudioFilesIntoEngine();

    // Persist as the "last opened song" so the next launch can skip
    // the chooser if `restore_last_project` is on.
    stateAPI->setConfig("last_open_song_id", songId);

    perfLog("[Coordinator] Loaded song: %s\n", song->name.c_str());
    if (onSongLoaded) onSongLoaded();
}

std::string PerformanceCoordinator::createDefaultSong(const std::string& name) {
    auto songId = stateAPI->createSong(name);
    stateAPI->setCurrentSong(songId);
    stateAPI->createActionTrack("Actions");

    // Track 1: DLS Piano (default plugin program — Acoustic Grand
    // Piano under GM program 0).
    auto trackId = stateAPI->createTrack("Piano");
    // Find DLS plugin in catalog
    for (auto& p : stateAPI->allPlugins()) {
        if (p.name == "DLSMusicDevice" && p.isInstrument) {
            // 3rd arg is presetId (a preset UUID), not the plugin name.
            // Leave empty — the plugin will load with its default state
            // and Coordinator::captureProcessorState will create a real
            // "Default" preset once the instance is live.
            stateAPI->setTrackPlugin(trackId, p.id);
            break;
        }
    }

    // Track 2: Audio input (disarmed, monitoring off)
    stateAPI->createAudioInputTrack("Audio In", -1, 0);
    auto tracks = stateAPI->listTracks();
    for (auto& t : tracks) {
        auto* ts = stateAPI->findTrack(t.id);
        if (ts && ts->sourceType == TrackSourceType::AudioInput) {
            stateAPI->setTrackArmed(t.id, false);
            stateAPI->setTrackInputMonitoring(t.id, false);
            break;
        }
    }

    if (auto* s = stateAPI->currentSong())
        arrangementImpl.setTracks(&s->tracks);

    // Focus-on-create moved focus to whichever track was created last
    // (the Audio In). The default-song intent is for the user to play
    // into the Piano, so restore focus there.
    stateAPI->setFocusedTrackId(trackId);

    perfLog("[Coordinator] Created default song '%s' with DLS Piano + Audio In\n",
            name.c_str());
    return songId.str();
}

bool PerformanceCoordinator::restoreSession() {
    auto& songs = stateAPI->allSongs();

    if (songs.empty()) {
        createDefaultSong("Untitled");
        return true;
    }

    // Skip the chooser if auto-loading the last opened song is enabled
    // (default ON — opt out via Settings → About) AND that song still
    // exists. Falls through to the chooser otherwise (saved id missing,
    // deleted, or setting explicitly disabled).
    if (stateAPI->getConfig("restore_last_project") != "0") {
        auto lastId = stateAPI->getConfig("last_open_song_id");
        if (!lastId.empty() && stateAPI->findSong(SongId{lastId})) {
            perfLog("[Coordinator] Auto-loading last project: %s\n", lastId.c_str());
            loadSong(lastId);
            return true;
        }
    }

    // 1+ songs exist and no auto-load — show the chooser.
    startupChooserNeeded = true;
    perfLog("[Coordinator] %d song(s) found — startup chooser will be shown\n",
            (int)songs.size());
    return true;
}

bool PerformanceCoordinator::needsStartupSongChooser() const {
    return startupChooserNeeded;
}

void PerformanceCoordinator::syncPluginCatalog() {
    populatePluginCatalog();
}

void PerformanceCoordinator::unloadSong() {
    stateAPI->setCurrentSong(SongId{});
    songRuntime->clearBindings();
}

// --- Loop recording entry points (see docs/LIVE_INPUT_AND_FOCUS.md) ---
//
// The whole flow is gesture-driven: replaceLoopGesture / overdubLoopGesture
// (defined below) are the public entry points. Each is a tap-to-toggle:
// on a None state they kick off Capturing immediately; on a Capturing
// state they commit. The first commit on an empty looper sets the
// master cycle length from elapsed beats.

bool PerformanceCoordinator::isInRecordMode() const {
    if (recordModeActive) return true;
    if (activeLoopCapture.has_value()) return true;
    if (stateAPI) {
        auto fid = stateAPI->getFocusedTrackId();
        if (!fid.empty() && stateAPI->getLoopAction(fid) != LoopAction::None)
            return true;
    }
    return false;
}

std::optional<PerformanceCoordinator::InFlightLoopCapture>
PerformanceCoordinator::getInFlightLoopCapture() const {
    if (!activeLoopCapture.has_value()) return std::nullopt;
    return InFlightLoopCapture{ activeLoopCapture->trackId,
                                 &activeLoopCapture->events };
}

std::optional<PerformanceCoordinator::InFlightLoopAudio>
PerformanceCoordinator::getInFlightLoopAudio() const {
    if (!activeLoopAudioSession.has_value() || !activeLoopAudioSession->writer) return std::nullopt;
    if (!stateAPI) return std::nullopt;
    auto* t = stateAPI->findTrack(activeLoopAudioSession->trackId);
    if (!t || t->loops.empty()) return std::nullopt;
    auto* take = t->loops[0].activeTake();
    if (!take) return std::nullopt;
    return InFlightLoopAudio{
        activeLoopAudioSession->trackId,
        activeLoopAudioSession->writer->getPeaks(),
        256,                  // matches AudioWriterThread::writeInterleavedToFile
        take->sampleRate,
        take->recordTempo
    };
}

int PerformanceCoordinator::addActionFireListener(ActionFireListener listener) {
    int id = nextActionFireListenerId++;
    actionFireListeners[id] = std::move(listener);
    return id;
}

void PerformanceCoordinator::removeActionFireListener(int id) {
    actionFireListeners.erase(id);
}

void PerformanceCoordinator::togglePlay() {
    if (sequencerImpl) sequencerImpl->togglePlayStop();
}

void PerformanceCoordinator::handleModeChange() {
    if (!stateAPI || !sequencerImpl) return;
    auto newMode = stateAPI->getMode();
    if (newMode == lastSeenMode) return;

    // Always stop the transport across a mode flip — both modes share
    // one clock and one playhead, and letting it advance through a mode
    // switch is exactly the surprise we're avoiding (Looper drives the
    // wall beat → Producer thinks "I'm at bar 143").
    if (sequencerImpl->isPlaying()) sequencerImpl->stop();

    // Read tempo from state (SSOT), not from the sequencer. setMode emits
    // Song *and* App events; the Song handler runs first and rewrites the
    // sequencer's tempo from song state. Reading sequencer here on the
    // App branch can therefore see a stale value if the song tempo lags.
    double bpm = stateAPI->getSongTempo();
    double bps = bpm > 0.0 ? bpm / 60.0 : 2.0;  // 120bpm fallback if tempo missing

    if (lastSeenMode == AppMode::Arrangement && newMode == AppMode::Looper) {
        // Stash where the user was in Arrangement (in seconds — survives
        // tempo changes) and reset the transport to 0 for the looper.
        stashedArrangementSeconds = sequencerImpl->getBeatPosition() / bps;
        sequencerImpl->setBeatPosition(0.0);
    } else if (lastSeenMode == AppMode::Looper && newMode == AppMode::Arrangement) {
        // Restore Arrangement to where the user left it.
        sequencerImpl->setBeatPosition(stashedArrangementSeconds * bps);
    }

    lastSeenMode = newMode;
}

// Two paths:
//   * Bootstrap (no master cycle yet): tap-to-start, tap-to-stop. The
//     transport snaps to 0 if it isn't playing; capture opens
//     immediately; second tap commits and the elapsed beats become the
//     master cycle.
//   * Established (cycle is set): tap → ReplaceQueued / OverdubQueued
//     on the focused track. Existing content keeps playing + visible.
//     At the next cycle wrap, dispatchLoopGesturesAtWrap promotes
//     Queued → Capturing for one cycle (existing silenced + hidden,
//     new captured), then commits at the next wrap.
//
// Re-pressing the same gesture during Queued cancels back to None;
// pressing the other switches the kind. Either gesture during
// Capturing is ignored — the cycle owns the boundary.
void PerformanceCoordinator::replaceLoopGesture() {
    fireLoopCaptureToggle(LoopAction::ReplaceQueued, "replace");
}

void PerformanceCoordinator::overdubLoopGesture() {
    fireLoopCaptureToggle(LoopAction::OverdubQueued, "overdub");
}

void PerformanceCoordinator::fireLoopCaptureToggle(LoopAction queueKind,
                                                    const char* label) {
    if (!stateAPI || !sequencerImpl) return;
    if (stateAPI->getMode() != AppMode::Looper) {
        perfLog("[Looper] %s gesture ignored — not in Looper mode\n", label);
        return;
    }
    auto focusedId = stateAPI->getFocusedTrackId();
    if (focusedId.empty()) {
        perfLog("[Looper] %s gesture ignored — no focused track\n", label);
        return;
    }

    auto act = stateAPI->getLoopAction(focusedId);

    // ----- Established path (bootstrap is below) ---------------------
    auto* song = stateAPI->currentSong();
    bool hasCycle = song && song->cycleEnd > song->cycleStart
                    && sequencerImpl->isPlaying();
    if (hasCycle) {
        // Capturing? Cycle owns the boundary — ignore the tap.
        if (act == LoopAction::CapturingReplace
         || act == LoopAction::CapturingOverdub) {
            perfLog("[Looper] %s ignored — recording (cycle commits at next wrap)\n", label);
            return;
        }
        // None / Queued — let StateAPI's queue logic toggle/cancel/switch.
        if (queueKind == LoopAction::ReplaceQueued) stateAPI->replaceLoop();
        else                                         stateAPI->overdubLoop();
        auto newAct = stateAPI->getLoopAction(focusedId);
        perfLog("[Looper] %s queued on %s (state now %d)\n",
                label, focusedId.c_str(), (int) newAct);
        return;
    }

    // ----- Bootstrap path -------------------------------------------
    if (act == LoopAction::CapturingReplace
     || act == LoopAction::CapturingOverdub) {
        // Stop edge: commit, set master cycle from elapsed beats.
        commitInFlightCapture();
        if (stateAPI) stateAPI->endTransaction();
        return;
    }

    // Start edge. One transaction wraps tap → tap so a single undo
    // backs out the whole recording.
    stateAPI->beginTransaction();
    if (! sequencerImpl->isPlaying()) {
        sequencerImpl->setBeatPosition(0.0);
        sequencerImpl->play();
    }
    captureStartBeat = sequencerImpl->getBeatPosition();
    auto kind = (queueKind == LoopAction::ReplaceQueued)
                 ? LoopAction::CapturingReplace
                 : LoopAction::CapturingOverdub;
    stateAPI->startLoopCaptureNow(kind);
    openLoopCaptureForTrack(focusedId, captureStartBeat);
    perfLog("[Looper] %s bootstrap start on %s at beat %.2f\n",
            label, focusedId.c_str(), captureStartBeat);
}

// Bring up MIDI capture (activeLoopCapture + beginCapture refcount)
// and an audio session for AudioInput tracks. Used by both the
// bootstrap-immediate path and the wrap-driven established path.
void PerformanceCoordinator::openLoopCaptureForTrack(const TrackId& trackId,
                                                      double captureBeat) {
    captureStartBeat = captureBeat;
    activeLoopCapture = LoopCaptureSlot{ trackId, {} };

    auto* focusedTrack = stateAPI ? stateAPI->findTrack(trackId) : nullptr;
    if (focusedTrack
        && focusedTrack->sourceType == TrackSourceType::AudioInput
        && audioEngine
        && ! focusedTrack->loops.empty()) {
        auto& region = focusedTrack->loops[0];
        region.type = "audio";
        bool isOverdub = (region.loopAction == LoopAction::CapturingOverdub);
        if (auto* take = region.activeTake()) {
            double sr = audioEngine->getCurrentSampleRate();
            take->recordTempo  = sequencerImpl ? sequencerImpl->getTempo() : 120.0;
            take->sampleRate   = (int) sr;
            take->channelCount = std::max(1, focusedTrack->inputChannelCount);

            auto audioDir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                                .getChildFile(".config/performance/audio");
            audioDir.createDirectory();
            auto wavFile = audioDir.getChildFile(juce::String(take->id.str()) + ".wav");
            // For overdub, write to a temp sibling so we can mix with the
            // existing wav at commit time; for replace, write straight to
            // the target after deleting it. createOutputStream doesn't
            // truncate — without the delete, a shorter new recording
            // leaves stale tail data and the WAV reader's lengthInSamples
            // reflects the OLD (longer) file, so playback walks past the
            // new content into the leftover bytes.
            auto tempFile = isOverdub
                ? audioDir.getChildFile(juce::String(take->id.str()) + ".overdub.wav")
                : wavFile;
            if (! isOverdub)
                wavFile.deleteFile();
            take->filePath = wavFile.getFullPathName().toStdString();

            AudioRecordSession session;
            session.trackId    = trackId;
            session.regionId   = region.id;
            session.fifo       = std::make_unique<AudioRecordFIFO>();
            session.writer     = std::make_unique<AudioWriterThread>();
            session.tempPath   = tempFile.getFullPathName().toStdString();
            session.targetPath = wavFile.getFullPathName().toStdString();
            session.isOverdub  = isOverdub;
            session.writer->startWriting(*session.fifo, tempFile, sr, take->channelCount);

            std::vector<GraphWrapper::AudioRecordTarget> targets;
            targets.push_back({ focusedTrack->inputChannelStart,
                                focusedTrack->inputChannelCount,
                                session.fifo.get() });
            audioEngine->setAudioRecordTargets(targets);

            activeLoopAudioSession = std::move(session);
            perfLog("[Looper] audio %s armed on %s → %s\n",
                    isOverdub ? "overdub capture" : "replace capture",
                    trackId.c_str(),
                    activeLoopAudioSession->tempPath.c_str());
        }
    }

    beginCapture(captureBeat);
}

// Drain & commit an in-flight capture. Used by:
//   * dispatchLoopGesturesAtWrap (established path commits at next wrap)
//   * fireLoopCaptureToggle bootstrap stop edge
//   * transport-stop subscription (so a stop mid-record persists, not
//     drops, what was captured so far — Boss-RC behavior)
void PerformanceCoordinator::commitInFlightCapture() {
    if (! activeLoopCapture.has_value()) return;
    auto tid    = activeLoopCapture->trackId;
    auto events = std::move(activeLoopCapture->events);
    activeLoopCapture.reset();

    double endBeat = sequencerImpl ? sequencerImpl->getBeatPosition() : captureStartBeat;
    double elapsed = endBeat - captureStartBeat;
    auto*  song    = stateAPI ? stateAPI->currentSong() : nullptr;
    bool   firstLoop = ! song || song->cycleEnd <= song->cycleStart;
    // Single wrap-around during the capture: add the cycle back so the
    // computed elapsed isn't negative.
    if (! firstLoop && elapsed < 0.0)
        elapsed += (song->cycleEnd - song->cycleStart);

    if (stateAPI) stateAPI->commitLoopAction(tid, std::move(events));
    endCapture();

    // Finalize an audio session if one was open. Close the writer, then
    // for an overdub capture merge the just-written temp file with the
    // existing target file (sample-by-sample sum) and write the result
    // back to the take's filePath. Replace skips the merge — writer
    // already wrote straight to the target. After either path: stamp
    // peaks + length on the take, reload audio files into the engine.
    if (activeLoopAudioSession.has_value() && audioEngine) {
        audioEngine->clearAudioRecordTargets();
        activeLoopAudioSession->writer->stopWriting();

        auto session = std::move(*activeLoopAudioSession);
        activeLoopAudioSession.reset();

        if (session.isOverdub) {
            mergeOverdubAudio(session);
        }

        if (stateAPI) {
            if (auto* t = stateAPI->findTrack(session.trackId)) {
                if (! t->loops.empty()) {
                    auto& region = t->loops[0];
                    if (auto* take = region.activeTake()) {
                        // Length comes from the target file's frame count
                        // (same for replace and overdub — overdub merge
                        // preserves the longer of the two file lengths).
                        juce::WavAudioFormat wav;
                        auto file = juce::File(session.targetPath);
                        int64_t frames = 0;
                        if (file.existsAsFile()) {
                            auto stream = file.createInputStream();
                            if (stream) {
                                std::unique_ptr<juce::AudioFormatReader> reader(
                                    wav.createReaderFor(stream.release(), true));
                                if (reader) frames = reader->lengthInSamples;
                            }
                        }
                        double seconds = (take->sampleRate > 0)
                                       ? (double) frames / take->sampleRate : 0.0;
                        region.lengthBeats = seconds * (take->recordTempo / 60.0);
                        recomputeAudioPeaksFromFile(*take);
                        if (firstLoop) elapsed = region.lengthBeats;
                        perfLog("[Looper] audio commit on %s (%s) — %lld frames, %.3f beats\n",
                                session.trackId.c_str(),
                                session.isOverdub ? "overdub" : "replace",
                                frames, region.lengthBeats);
                    }
                }
            }
        }
        loadAudioFilesIntoEngine();
    }

    if (firstLoop && elapsed > 0.0 && stateAPI) {
        stateAPI->setCycleLength(elapsed);
        if (auto* t = stateAPI->findTrack(tid))
            if (! t->loops.empty()
                && t->loops[0].type != "audio"
                && t->loops[0].lengthBeats <= 0.0)
                t->loops[0].lengthBeats = elapsed;
    }

    perfLog("[Looper] commit on %s — elapsed=%.3f beats, events=%zu%s\n",
            tid.c_str(), elapsed, events.size(),
            firstLoop ? " (set master cycle)" : "");
}

// Bootstrap stop edge. Established path commits at wrap, not here.
void PerformanceCoordinator::finishLoopCapture() {
    commitInFlightCapture();
}

// Wrap-driven dispatch for the established path. Two steps in order:
//   1. Commit any in-flight Capturing — its cycle just ended.
//   2. Promote any focused-track Queued → Capturing for the next cycle.
// Both happen synchronously on the message thread (called from the
// timerCallback wrap detector). One capture in flight at a time —
// the "focused track" anchor collapses the multi-track question.
void PerformanceCoordinator::dispatchLoopGesturesAtWrap() {
    if (!stateAPI) return;

    if (activeLoopCapture.has_value()) {
        commitInFlightCapture();
        if (stateAPI) stateAPI->endTransaction();
    }

    auto focusedId = stateAPI->getFocusedTrackId();
    if (focusedId.empty()) return;
    auto act = stateAPI->getLoopAction(focusedId);
    if (act != LoopAction::ReplaceQueued && act != LoopAction::OverdubQueued)
        return;

    stateAPI->beginTransaction();
    stateAPI->beginLoopCapture(focusedId);
    // Wrap-driven captures start at cycle 0 (the wrap's audioBeat is
    // tiny — a few samples past zero). Storing events relative to 0
    // matches the existing scanLoopEvents' cycle-relative reads.
    openLoopCaptureForTrack(focusedId, 0.0);
    perfLog("[Looper] wrap → capture begins on %s (kind=%s)\n",
            focusedId.c_str(),
            act == LoopAction::ReplaceQueued ? "replace" : "overdub");
}

void PerformanceCoordinator::resetLooperSession() {
    perfLog("[Looper] resetLooperSession — wiping all looper state\n");

    // 1. Stop transport so no further wraps fire while we tear down.
    if (sequencerImpl && sequencerImpl->isPlaying())
        sequencerImpl->stop();

    // 2. Drop any in-flight gesture capture. cancelLoopCapture in
    //    state resets loopAction; release the engine refcount. Close
    //    any open undo transaction so the next mutation isn't grouped
    //    with the abandoned recording. Also tear down any audio writer
    //    so the half-finished WAV gets closed (and not re-armed).
    if (activeLoopCapture.has_value()) {
        if (stateAPI) {
            stateAPI->cancelLoopCapture(activeLoopCapture->trackId);
            stateAPI->endTransaction();
        }
        activeLoopCapture.reset();
        endCapture();
    }
    if (activeLoopAudioSession.has_value() && audioEngine) {
        audioEngine->clearAudioRecordTargets();
        activeLoopAudioSession->writer->stopWriting();
        activeLoopAudioSession.reset();
    }

    // 3. Wipe per-track looper runtime: queued/capturing flags +
    //    undo/redo stacks. Then DELETE every loop region outright
    //    (not just empty events — regions persist across saves and
    //    keep painting empty bars; the panic button should leave
    //    nothing behind on the timeline) and reset cycleEnd to 0.
    if (stateAPI) {
        stateAPI->resetLoopRuntime();
        if (auto* song = stateAPI->currentSong()) {
            for (auto& t : song->tracks)
                t.loops.clear();
            song->cycleEnd = 0.0;
            song->cycleEnabled = false;
            stateAPI->events().emit({ StateEvent::Updated, StateEvent::Song,
                                       song->id.str(), "" });
        }
    }

    perfLog("[Looper] resetLooperSession — done; ready for fresh bootstrap\n");
}

// --- Persistence ---

static std::string computeHash(const juce::MemoryBlock& data) {
    juce::SHA256 sha(data);
    return sha.toHexString().toStdString();
}

static void captureProcessorBlob(juce::AudioProcessor* proc, std::string& outState, std::string& outHash) {
    if (!proc) return;
    juce::MemoryBlock block;
    proc->getStateInformation(block);
    if (block.getSize() > 0) {
        outState = block.toBase64Encoding().toStdString();
        outHash = computeHash(block);
    }
}

void PerformanceCoordinator::captureProcessorState() {
    auto* song = stateAPI->currentSong();
    if (!song) return;

    int captured = 0;
    for (auto& track : song->tracks) {
        // Instrument
        auto* proc = audioEngine->getTrackInstrumentProcessor(juce::String(track.id.str()));
        if (proc) {
            std::string newHash;
            std::string newState;
            captureProcessorBlob(proc, newState, newHash);
            if (newHash != track.processorStateHash) {
                track.processorState = std::move(newState);
                track.processorStateHash = std::move(newHash);
                captured++;
            }
        }

        // Effects
        for (auto& fx : track.effects) {
            auto* fxProc = audioEngine->getEffectProcessor(juce::String(track.id.str()), juce::String(fx.id.str()));
            if (fxProc) {
                std::string newHash;
                std::string newState;
                captureProcessorBlob(fxProc, newState, newHash);
                if (newHash != fx.processorStateHash) {
                    fx.processorState = std::move(newState);
                    fx.processorStateHash = std::move(newHash);
                    captured++;
                }
            }
        }
    }

    // Bus effects
    for (auto& bus : song->busses) {
        for (auto& fx : bus.effects) {
            auto* fxProc = audioEngine->getEffectProcessor(juce::String(bus.id.str()), juce::String(fx.id.str()));
            if (fxProc) {
                std::string newHash;
                std::string newState;
                captureProcessorBlob(fxProc, newState, newHash);
                if (newHash != fx.processorStateHash) {
                    fx.processorState = std::move(newState);
                    fx.processorStateHash = std::move(newHash);
                    captured++;
                }
            }
        }
    }

    // Master effects
    for (auto& fx : song->masterEffects) {
        auto* fxProc = audioEngine->getEffectProcessor(juce::String("Output"), juce::String(fx.id.str()));
        if (fxProc) {
            std::string newHash;
            std::string newState;
            captureProcessorBlob(fxProc, newState, newHash);
            if (newHash != fx.processorStateHash) {
                fx.processorState = std::move(newState);
                fx.processorStateHash = std::move(newHash);
                captured++;
            }
        }
    }

    if (captured > 0) {
        stateAPI->markDirty();
        perfLog("[Coordinator] Captured %d processor states\n", captured);
    }
    lastCaptureMs = juce::Time::getMillisecondCounterHiRes();
}

void PerformanceCoordinator::onUndoRedoRestore() {
    // Re-point arrangement at current song's tracks
    if (auto* s = stateAPI->currentSong())
        arrangementImpl.setTracks(&s->tracks);

    // Reload audio files (regions may have changed)
    loadAudioFilesIntoEngine();

    // Sync tempo and cycle
    syncTempoFromState();

    // Cancel any in-flight automations
    if (automationEngine)
        automationEngine->cancelAll();

    // Flush notes (regions may have been removed)
    if (audioEngine)
        audioEngine->setPlaybackBeatPosition(sequencerImpl->getBeatPosition());

    perfLog("[Coordinator] Undo/redo restore complete\n");
}

void PerformanceCoordinator::save() {
    captureProcessorState();
    // Persist cycle range to current song
    if (sequencerImpl && stateAPI) {
        auto* song = stateAPI->currentSong();
        if (song) {
            song->cycleStart = sequencerImpl->getLoopStart();
            song->cycleEnd = sequencerImpl->getLoopEnd();
            song->cycleEnabled = sequencerImpl->isLoopEnabled();
        }
    }
    if (persistence && stateAPI) {
        if (persistence->saveFrom(*stateAPI))
            perfLog("[Coordinator] Saved\n");
        else
            perfLog("[Coordinator] Explicit save FAILED — see [Persistence] errors above\n");
    }
}

// --- Song state snapshots ---

void PerformanceCoordinator::saveInitialState() {
    // TODO: serialize current state to SongState.initialState JSON
    perfLog("[Coordinator] saveInitialState not yet implemented for new system\n");
}

void PerformanceCoordinator::loadInitialState() {
    // TODO: deserialize SongState.initialState and rebuild
    perfLog("[Coordinator] loadInitialState not yet implemented for new system\n");
}

// --- Score ---

void PerformanceCoordinator::replayScore(int upToStep) {
    auto steps = stateAPI->scoreSteps();
    if (steps.empty()) {
        perfLog("[Coordinator] No score steps to replay\n");
        return;
    }

    // TODO: load initial state first, then replay
    int count = (upToStep < 0) ? (int)steps.size() : std::min(upToStep, (int)steps.size());
    perfLog("[Coordinator] Replaying score: %d of %d steps\n", count, (int)steps.size());

    for (int i = 0; i < count; ++i) {
        auto& step = steps[i];
        auto* action = stateAPI->findActionById(step.actionId);
        if (!action) continue;
        auto args = juce::JSON::parse(juce::String(step.args));
        executeAction(action->name, args, 1.0f);
        perfLog("[Coordinator] Score step %d: %s\n", i + 1, step.description.c_str());
    }
}

// --- Offline render (bounce) ---

PerformanceCoordinator::BounceResult
PerformanceCoordinator::bounce(const juce::File& outputFile) {
    BounceResult out;
    if (!sequencerImpl) {
        out.errorMessage = "engine not initialised";
        return out;
    }
    if (!sequencerImpl->isLoopEnabled()) {
        out.errorMessage = "no cycle active — enable cycle mode or pass startBeat + endBeat explicitly";
        return out;
    }
    const double loopStart = sequencerImpl->getLoopStart();
    const double loopEnd = sequencerImpl->getLoopEnd();
    if (loopEnd <= loopStart) {
        out.errorMessage = "cycle range is empty — set a cycle region first";
        return out;
    }
    return bounce(outputFile, loopStart, loopEnd);
}

PerformanceCoordinator::BounceResult
PerformanceCoordinator::bounce(const juce::File& outputFile,
                                double startBeat, double endBeat) {
    BounceResult out;
    out.startBeat = startBeat;
    out.endBeat = endBeat;
    if (!audioEngine || !sequencerImpl) {
        out.errorMessage = "engine not initialised";
        return out;
    }

    // Snapshot transport so we restore the live playhead after rendering.
    const bool wasPlaying = sequencerImpl->isPlaying();
    const double savedBeat = sequencerImpl->getBeatPosition();
    const double tempo = sequencerImpl->getTempo();

    // Stop live transport so the sequencer's timer doesn't fight the render.
    sequencerImpl->stop();

    // Cancel any running interpolations so automation doesn't mutate state
    // mid-render. (Spike punt: no automation during bounce.)
    cancelAllAutomation();

    audioEngine->pauseDeviceProcessing();

    auto result = OfflineRenderer::render(*audioEngine, outputFile,
                                          startBeat, endBeat, tempo);

    audioEngine->resumeDeviceProcessing();

    // Restore transport. Position set explicitly; we don't auto-resume
    // playback — if the user was playing, they'll hit play again.
    sequencerImpl->setBeatPosition(savedBeat);
    (void)wasPlaying;

    out.ok = result.ok;
    out.errorMessage = result.errorMessage;
    out.wallClockSeconds = result.wallClockSeconds;
    out.audioDurationSeconds = result.audioDurationSeconds;
    return out;
}

// --- Track presets ---

static juce::File getTrackPresetsDir() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/performance/track_presets");
}

void PerformanceCoordinator::saveTrackPreset(const juce::String& trackId,
                                              const juce::String& presetName) {
    auto* body = new juce::DynamicObject();

    auto pluginName = juce::String(stateAPI->getTrackPluginName(TrackId{TrackId{trackId.toStdString()}}));
    body->setProperty("plugin", pluginName);

    // Instrument state from engine
    if (auto* proc = audioEngine->getTrackInstrumentProcessor(trackId)) {
        juce::MemoryBlock memState;
        proc->getStateInformation(memState);
        body->setProperty("pluginState", memState.toBase64Encoding());
    }

    // Effects from state + engine processor state
    juce::Array<juce::var> effectsArr;
    for (auto& fx : stateAPI->getTrackEffects(TrackId{TrackId{trackId.toStdString()}})) {
        auto* fxObj = new juce::DynamicObject();
        fxObj->setProperty("plugin", juce::String(fx.pluginName));
        if (auto* proc = audioEngine->getEffectProcessor(trackId, juce::String(fx.effectId.str()))) {
            juce::MemoryBlock memState;
            proc->getStateInformation(memState);
            fxObj->setProperty("state", memState.toBase64Encoding());
        }
        effectsArr.add(juce::var(fxObj));
    }
    body->setProperty("effects", effectsArr);

    // Sends from state
    juce::Array<juce::var> sendsArr;
    for (auto& send : stateAPI->getTrackSends(TrackId{TrackId{trackId.toStdString()}})) {
        auto* sendObj = new juce::DynamicObject();
        sendObj->setProperty("bus", juce::String(send.busName));
        sendObj->setProperty("gain", send.gain);
        sendsArr.add(juce::var(sendObj));
    }
    body->setProperty("sends", sendsArr);

    body->setProperty("gain", stateAPI->getTrackGain(TrackId{TrackId{trackId.toStdString()}}));

    auto dir = getTrackPresetsDir();
    dir.createDirectory();
    auto file = dir.getChildFile(presetName + ".json");
    file.replaceWithText(juce::JSON::toString(juce::var(body), true));

    perfLog("[Coordinator] Saved track preset \"%s\"\n", presetName.toRawUTF8());
}

void PerformanceCoordinator::loadTrackPreset(const juce::String& trackId,
                                              const juce::String& presetName) {
    auto file = getTrackPresetsDir().getChildFile(presetName + ".json");
    if (!file.existsAsFile()) return;

    auto json = juce::JSON::parse(file.loadFileAsString());
    auto pluginName = json.getProperty("plugin", "").toString();

    if (pluginName.isNotEmpty()) {
        stateAPI->clearTrackPlugin(TrackId{TrackId{trackId.toStdString()}});
        auto* plugin = stateAPI->findPluginByName(pluginName.toStdString());
        if (plugin)
            stateAPI->setTrackPlugin(TrackId{TrackId{trackId.toStdString()}}, plugin->id);

        // Store captured state on the track — EngineSync restores it
        // automatically when the plugin finishes async loading (LoadStatus → Loaded)
        auto stateB64 = json.getProperty("pluginState", "").toString();
        if (stateB64.isNotEmpty()) {
            auto* track = stateAPI->findTrack(TrackId{TrackId{trackId.toStdString()}});
            if (track) {
                track->processorState = stateB64.toStdString();
                track->processorStateHash.clear();  // will be set on next capture
            }
        }
    }

    // Effects
    if (auto* effectsArr = json.getProperty("effects", juce::var()).getArray()) {
        for (auto& fx : stateAPI->getTrackEffects(TrackId{TrackId{trackId.toStdString()}}))
            stateAPI->removeEffect(fx.effectId);

        for (auto& fxVar : *effectsArr) {
            auto fxPlugin = fxVar.getProperty("plugin", "").toString();
            if (fxPlugin.isNotEmpty()) {
                auto* plugin = stateAPI->findPluginByName(fxPlugin.toStdString());
                if (plugin)
                    stateAPI->addEffect(trackId.toStdString(), fxPlugin.toStdString(), plugin->id);
            }
        }
    }

    stateAPI->setTrackGain(TrackId{TrackId{trackId.toStdString()}}, (float)json.getProperty("gain", 1.0));
    stateAPI->renameTrack(TrackId{TrackId{trackId.toStdString()}}, presetName.toStdString());

    perfLog("[Coordinator] Loaded track preset \"%s\"\n", presetName.toRawUTF8());
}

std::vector<juce::String> PerformanceCoordinator::listTrackPresets() {
    std::vector<juce::String> names;
    auto dir = getTrackPresetsDir();
    if (!dir.isDirectory()) return names;
    for (auto& entry : juce::RangedDirectoryIterator(dir, false, "*.json"))
        names.push_back(entry.getFile().getFileNameWithoutExtension());
    std::sort(names.begin(), names.end());
    return names;
}

// --- Automation ---

int PerformanceCoordinator::interpolate(float from, float to, float durationSec,
                                         AutomationCallback callback, EasingFn easing) {
    return automationEngine->interpolate(from, to, durationSec, std::move(callback), std::move(easing));
}

int PerformanceCoordinator::delay(float delaySec, std::function<void()> callback) {
    return automationEngine->delay(delaySec, std::move(callback));
}

void PerformanceCoordinator::cancelAutomation(int handle) {
    automationEngine->cancel(handle);
}

void PerformanceCoordinator::cancelAllAutomation() {
    automationEngine->cancelAll();
}

// --- Action dispatch ---

static MIDIControl::Type parseControlType(const juce::String& type) {
    if (type.equalsIgnoreCase("cc")) return MIDIControl::CC;
    if (type.equalsIgnoreCase("note")) return MIDIControl::Note;
    if (type.equalsIgnoreCase("pitchbend")) return MIDIControl::PitchBend;
    if (type.equalsIgnoreCase("pressure")) return MIDIControl::Pressure;
    return MIDIControl::CC;
}

void PerformanceCoordinator::executeAction(const std::string& actionName,
                                            const juce::var& args, float value) {
    // Skip value=0 for trigger actions (note-off), but allow for continuous (CC faders)
    static const std::set<std::string> continuousActions = { "trackVolume" };
    if (value == 0.0f && !continuousActions.count(actionName)) return;

    // Notify any GUI listening for action fires (e.g. activity lights
    // on the looper button group). Single chokepoint for all dispatch
    // sources — Lua, MIDI bindings, GUI clicks all flow through here.
    // Iterate a snapshot so a listener that removes itself in its
    // callback (rare but possible) can't invalidate the iterator.
    auto snapshot = actionFireListeners;
    for (auto& [id, fn] : snapshot)
        if (fn) fn(actionName);

    // Check for custom Lua action first
    auto* actionInfo = stateAPI->findActionByName(actionName);
    if (actionInfo && !actionInfo->luaCode.empty() && luaExecutor) {
        perfLog("[Coordinator] Executing custom action: %s\n", actionName.c_str());
        auto result = luaExecutor(actionInfo->luaCode);
        if (!result.empty() && result != "ok")
            perfLog("[Coordinator] Custom action error: %s\n", result.c_str());
        return;
    }

    // Algebra path — action has a tree body or an expander.
    if (actionInfo && (actionInfo->hasBody || actionInfo->expander) && algebraAdapters) {
        std::vector<ActionAlgebra::Value> argValues;
        if (auto* arr = args.getArray()) {
            for (int i = 0; i < arr->size(); ++i) {
                auto v = (*arr)[i];
                if (v.isDouble() || v.isInt() || v.isInt64())
                    argValues.push_back(ActionAlgebra::num((double)v));
                else
                    argValues.push_back(ActionAlgebra::text(v.toString().toStdString()));
            }
        } else if (!args.isVoid() && !args.isUndefined()) {
            // Non-array args (e.g. morph's compound object) — pass the whole
            // thing as one Text value (JSON-stringified). Expanders that want
            // the structure parse it themselves.
            argValues.push_back(ActionAlgebra::text(
                juce::JSON::toString(args, true).toStdString()));
        }
        algebraAdapters->interpreter.trigger(actionName, argValues, value);
        return;
    }

    auto getArg = [&](int index) -> juce::String {
        if (auto* arr = args.getArray())
            if (index < arr->size())
                return (*arr)[index].toString();
        return {};
    };
    auto getArgFloat = [&](int index, float def = 0.0f) -> float {
        if (auto* arr = args.getArray())
            if (index < arr->size())
                return (float)(*arr)[index];
        return def;
    };

    auto resolveTrack = [this](const juce::String& id) -> TrackId {
        TrackId tid{id.toStdString()};
        if (!stateAPI->findTrack(tid))
            perfLog("[Coordinator] resolveTrack: '%s' not found\n", tid.c_str());
        return tid;
    };

    // The C++ dispatch ladder is empty. Every built-in action has either a
    // static body (setActiveTrack, fadeOut, fadeIn, crossfade, trackVolume),
    // an expander (morphToPreset, morphChain, morph), or a legacy Lua body.
    // All three paths are handled above by the early-returns. Reaching here
    // means we got an actionName with no registered mechanism.
    perfLog("[Action] Unknown action: %s\n", actionName.c_str());
}

void PerformanceCoordinator::refreshMidiDevices() {
    if (midiEngine) {
        midiEngine->rescanDevices();
    }

    // Auto-register any connected MIDI devices not yet in state
    auto midiDevices = juce::MidiInput::getAvailableDevices();
    for (auto& d : midiDevices) {
        auto portName = d.name.toStdString();
        // Skip virtual MIDI buses (macOS IAC)
        if (d.name.containsIgnoreCase("IAC Driver")) continue;
        if (!stateAPI->findDeviceByPortName(portName)) {
            stateAPI->registerDevice(portName, portName);
            perfLog("[Coordinator] Auto-registered MIDI device: %s\n", portName.c_str());
        }
    }
}

void PerformanceCoordinator::startMidiLearn(const std::string& deviceId,
    std::function<void(const std::string& controlType, int channel, int number,
                       const std::string& portName)> callback) {
    if (midiEngine) midiEngine->startLearn(deviceId, std::move(callback));
}

void PerformanceCoordinator::cancelMidiLearn() {
    if (midiEngine) midiEngine->cancelLearn();
}

void PerformanceCoordinator::setMidiDeviceMonitor(const std::string& deviceId,
    std::function<void(const std::string& description,
                       const std::string& type, int channel, int number)> callback) {
    if (midiEngine) midiEngine->setDeviceMonitor(deviceId, std::move(callback));
}

void PerformanceCoordinator::clearMidiDeviceMonitor() {
    if (midiEngine) midiEngine->clearDeviceMonitor();
}

void PerformanceCoordinator::setGlobalMidiMonitor(
    std::function<void(const std::string& deviceName, const std::string& description,
                       const std::string& type, int channel, int number, int value)> callback) {
    if (midiEngine) midiEngine->setGlobalMonitor(std::move(callback));
}

void PerformanceCoordinator::clearGlobalMidiMonitor() {
    if (midiEngine) midiEngine->clearGlobalMonitor();
}

int64_t PerformanceCoordinator::getMidiDeviceActivityMs(const std::string& deviceId) {
    return midiEngine ? midiEngine->getDeviceLastActivityMs(deviceId) : 0;
}

int64_t PerformanceCoordinator::getMidiPortActivityMs(const std::string& portName) {
    return midiEngine ? midiEngine->getPortLastActivityMs(juce::String(portName)) : 0;
}

SequencerAPI* PerformanceCoordinator::sequencer() {
    return sequencerImpl.get();
}

void PerformanceCoordinator::log(const juce::String& message) {
    perfLog("[Coordinator] %s\n", message.toRawUTF8());
}

// --- Internal ---

// --- State event handler: auto-create Default preset ---

void PerformanceCoordinator::onStateEvent(const StateEvent& event) {
    // Binding changes — rebuild runtime dispatch map
    if (event.entity == StateEvent::Binding) {
        restoreBindings();
        return;
    }

    // Watch for Track or Effect Updated events — LoadStatus may have changed to Loaded
    if (event.action != StateEvent::Updated) return;

    // Song Updated — may be a cycle-length change from setCycleLength,
    // or a cycle flip triggered by setMode. Push state → sequencer so
    // the playback clock reflects the new cycle, and so the next save()
    // snapshot (which reads from sequencer) matches what state says.
    // App Updated — mode changed. Also re-sync so the arrangement
    // scanner dispatches to the right pool.
    if (event.entity == StateEvent::Song || event.entity == StateEvent::App) {
        if (event.entity == StateEvent::App) handleModeChange();
        syncTempoFromState();
        return;
    }

    if (event.entity == StateEvent::Track) {
        auto* track = stateAPI->findTrack(TrackId{event.entityId});
        if (!track || track->instrumentLoadStatus != LoadStatus::Loaded) return;
        if (track->pluginId.empty()) return;
        ensureDefaultPreset(track->id.str(), "", track->pluginId, PresetKind::Instrument);
    }
    else if (event.entity == StateEvent::Effect) {
        auto* fx = stateAPI->findEffect(EffectId{event.entityId});
        if (!fx || fx->loadStatus != LoadStatus::Loaded) return;
        if (fx->pluginId.empty()) return;
        ensureDefaultPreset(event.parentId, fx->id.str(), fx->pluginId, PresetKind::Effect);
    }
}

void PerformanceCoordinator::ensureDefaultPreset(const std::string& parentId,
                                                   const std::string& effectId,
                                                   const PluginId& pluginId,
                                                   PresetKind kind) {
    // Already has a Default preset?
    if (stateAPI->findPreset(pluginId, "Default")) return;

    auto* plugin = stateAPI->findPluginById(pluginId);
    if (!plugin) return;

    // Get the processor and capture its initial state
    juce::AudioProcessor* proc = nullptr;
    auto engParent = (parentId == stateAPI->getMasterOutputId())
                         ? juce::String("Output") : juce::String(parentId);
    if (effectId.empty())
        proc = audioEngine->getTrackInstrumentProcessor(juce::String(parentId));
    else
        proc = audioEngine->getEffectProcessor(engParent, juce::String(effectId));
    if (!proc) return;

    juce::MemoryBlock state;
    proc->getStateInformation(state);
    if (state.getSize() == 0) return;

    // Save to disk
    auto dir = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                   .getChildFile(".config/performance/snapshots")
                   .getChildFile(juce::String(plugin->name));
    dir.createDirectory();
    auto file = dir.getChildFile("Default.state");
    file.replaceWithData(state.getData(), state.getSize());

    // Register in state
    stateAPI->createPreset(pluginId, "Default", file.getFullPathName().toStdString(), kind);

    perfLog("[Coordinator] Created Default preset for %s (%d bytes)\n",
            plugin->name.c_str(), (int)state.getSize());
}

void PerformanceCoordinator::populatePluginCatalog() {
    int count = 0;
    for (auto& type : audioEngine->getKnownPlugins().getTypes()) {
        stateAPI->registerPlugin(
            type.name.toStdString(),
            type.manufacturerName.toStdString(),
            type.fileOrIdentifier.toStdString(),
            type.isInstrument);
        count++;
    }
    perfLog("[Coordinator] Registered %d plugins in catalog\n", count);
}

void PerformanceCoordinator::registerBuiltinActions() {
    // Typed schema builders (see docs/ACTION_INSTANCES_REFACTOR.md).
    auto track = [](const std::string& name,
                    std::vector<std::string> sourceTypes = {}) {
        ParamSchema p;
        p.name = name;
        p.type = ParamType::ChannelRef;
        p.scope = { "track" };
        p.sourceTypes = std::move(sourceTypes);
        return p;
    };
    auto channel = [](const std::string& name) {
        ParamSchema p;
        p.name = name;
        p.type = ParamType::ChannelRef;  // scope empty = any channel (track / bus / master)
        return p;
    };
    auto preset = [](const std::string& name) {
        ParamSchema p;
        p.name = name;
        p.type = ParamType::PresetRef;
        return p;
    };
    auto duration = [](const std::string& name, double defaultSec = 3.0) {
        ParamSchema p;
        p.name = name;
        p.type = ParamType::Float;
        p.minValue = 0.0;
        p.defaultValue = std::to_string(defaultSec);
        return p;
    };
    auto easing = []() {
        ParamSchema p;
        p.name = "easing";
        p.type = ParamType::Enum;
        p.enumValues = { "linear", "easein", "easeout", "cosine", "scurve" };
        p.defaultValue = "easein";
        p.required = false;
        return p;
    };
    auto morph = []() {
        ParamSchema p;
        p.name = "mode";
        p.type = ParamType::Morph;
        return p;
    };

    // Algebra bodies for the gain-family built-ins. Param placeholders:
    // - track/bus refs → Value::Text substituted into Target.entityId via "$name" sigil
    // - duration / easing → substituted as Values directly
    //
    // Explicitly qualify ActionAlgebra::... — we already have local lambdas
    // named `track`, `duration`, and `easing` above that shadow any
    // unqualified use.
    namespace AA = ActionAlgebra;
    auto trackGainTarget = [](const std::string& entityPh) {
        return AA::Target { AA::Target::Kind::TrackGain, "$" + entityPh, -1 };
    };

    stateAPI->registerAction("setActiveTrack", "Set active track",
        std::vector<ParamSchema>{ track("trackName", { "Instrument" }) },
        AA::lua("selectTrack(args[1])"));

    stateAPI->registerAction("fadeOut", "Fade out",
        std::vector<ParamSchema>{ track("trackName"), duration("duration"), easing() },
        AA::interpolate(trackGainTarget("trackName"),
                         AA::captureCurrent(), AA::num(0),
                         AA::placeholder("duration"), "easein"),
        1);
    stateAPI->registerAction("fadeIn", "Fade in",
        std::vector<ParamSchema>{ track("trackName"), duration("duration"), easing() },
        AA::interpolate(trackGainTarget("trackName"),
                         AA::captureCurrent(), AA::num(1),
                         AA::placeholder("duration"), "easein"),
        1);
    stateAPI->registerAction("crossfade", "Crossfade",
        std::vector<ParamSchema>{ track("fromTrack"), track("toTrack"),
                                   duration("duration"), easing() },
        AA::parallel({
            AA::invoke("fadeOut", { AA::placeholder("fromTrack"),
                                     AA::placeholder("duration"),
                                     AA::placeholder("easing") }),
            AA::invoke("fadeIn",  { AA::placeholder("toTrack"),
                                     AA::placeholder("duration"),
                                     AA::placeholder("easing") }),
        }),
        2);
    // Phase 6 looper gestures — momentary one-shots, fire on press
    // (value > 0) only so a release event doesn't immediately cancel
    // the queue. All target the focused track. See
    // docs/LIVE_INPUT_AND_FOCUS.md.
    stateAPI->registerAction("replaceLoop", "Loop: replace",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then replaceLoop() end"));
    stateAPI->registerAction("overdubLoop", "Loop: overdub",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then overdubLoop() end"));
    stateAPI->registerAction("undoLoop", "Loop: undo",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then undoLoop() end"));
    stateAPI->registerAction("redoLoop", "Loop: redo",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then redoLoop() end"));
    stateAPI->registerAction("clearLoop", "Loop: clear focused track",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then clearLoop() end"));
    stateAPI->registerAction("clearAllLoops", "Loop: clear all + reset cycle",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then clearAllLoops() end"));
    stateAPI->registerAction("resetLooperSession", "Loop: reset session",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then resetLooperSession() end"));

    // Transport + focus + per-track-mute. Mode-agnostic but most useful
    // alongside the looper gestures (the looper button group binds them).
    stateAPI->registerAction("togglePlay", "Transport: play / stop",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then togglePlay() end"));
    stateAPI->registerAction("focusPrevTrack", "Focus: previous track",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then focusPrevTrack() end"));
    stateAPI->registerAction("focusNextTrack", "Focus: next track",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then focusNextTrack() end"));
    stateAPI->registerAction("toggleFocusedMute", "Focus: toggle mute",
        std::vector<ParamSchema>{},
        AA::lua("if value > 0 then toggleFocusedMute() end"));

    // trackVolume: CC fader → cubic curve → target channel gain.
    // Cubic gives more resolution at low gains (useful for subtle control).
    // The channel arg can resolve to a track, bus, or master.
    stateAPI->registerAction("trackVolume", "Track Volume",
        std::vector<ParamSchema>{ channel("channel") },
        AA::lua("setChannelGain(args[1], value * value * value * 2.0)"));

    // morphToPreset — dynamic Parallel of per-param Interpolates. Plugin param
    // count isn't knowable statically, so we use an expander.
    auto morphToPresetId = stateAPI->registerAction("morphToPreset", "Morph to preset",
        std::vector<ParamSchema>{ track("trackName", { "Instrument" }), preset("presetName"),
                                  duration("duration"), easing() }, 2);
    stateAPI->setActionExpander(morphToPresetId,
        [this](const std::vector<AA::Value>& args) -> AA::ActionNode {
            if (args.size() < 4) return {};
            auto trackIdStr = args[0].kind == AA::Value::Kind::Text ? args[0].text : std::string{};
            auto presetName = args[1].kind == AA::Value::Kind::Text ? args[1].text : std::string{};
            double dur = args[2].kind == AA::Value::Kind::Number ? args[2].number : 3.0;
            auto easingStr  = args[3].kind == AA::Value::Kind::Text ? args[3].text : std::string("easein");
            auto* proc = audioEngine->getTrackInstrumentProcessor(juce::String(trackIdStr));
            if (!proc) {
                perfLog("[Action] morphToPreset: no processor for track %s\n", trackIdStr.c_str());
                return {};
            }
            auto target = engineAPI->getPresetParams(proc->getName(), juce::String(presetName));
            if (target.values.empty()) {
                perfLog("[Action] morphToPreset: preset '%s' has no params\n", presetName.c_str());
                return {};
            }
            auto from = EngineAPI::captureParams(proc);
            std::vector<AA::ActionNode> children;
            int n = std::min((int)from.values.size(), (int)target.values.size());
            for (int i = 0; i < n; ++i) {
                AA::Target t { AA::Target::Kind::TrackParam, trackIdStr, i };
                children.push_back(AA::interpolate(t,
                                                    AA::num(from.values[i]),
                                                    AA::num(target.values[i]),
                                                    AA::num(dur), easingStr));
            }
            return AA::parallel(std::move(children));
        });

    // morphChain — Sequence of [morphToPreset(A), Delay(dwell), morphToPreset(B)].
    // Delegates to morphToPreset's expander via Invoke.
    auto morphChainId = stateAPI->registerAction(
        "morphChain", "Morph chain (A \xe2\x86\x92 dwell \xe2\x86\x92 B)",
        std::vector<ParamSchema>{ track("trackName", { "Instrument" }), preset("presetA"),
                                  preset("presetB"), duration("dwell"),
                                  duration("duration"), easing() }, 4);
    stateAPI->setActionExpander(morphChainId,
        [](const std::vector<AA::Value>& args) -> AA::ActionNode {
            if (args.size() < 6) return {};
            auto trackName = args[0];
            auto presetA   = args[1];
            auto presetB   = args[2];
            auto dwell     = args[3];
            auto dur       = args[4];
            auto easingV   = args[5];
            return AA::sequence({
                AA::invoke("morphToPreset", { trackName, presetA, dur, easingV }),
                AA::delay(dwell,
                    AA::invoke("morphToPreset", { trackName, presetB, dur, easingV })),
            });
        });

    // morph — compound action. args[0] is a Text JSON blob with
    // {mode, actions: [{action, args}, ...]}. Expands to Parallel or
    // Sequence of Invokes.
    auto morphId = stateAPI->registerAction("morph", "Morph",
        std::vector<ParamSchema>{ morph() });
    stateAPI->setActionExpander(morphId,
        [](const std::vector<AA::Value>& args) -> AA::ActionNode {
            if (args.empty() || args[0].kind != AA::Value::Kind::Text) return {};
            auto parsed = juce::JSON::parse(juce::String(args[0].text));
            auto modeStr = parsed.getProperty("mode", "parallel").toString();
            auto subActions = parsed.getProperty("actions", juce::var());
            if (!subActions.isArray()) return {};

            std::vector<AA::ActionNode> children;
            for (int i = 0; i < subActions.size(); ++i) {
                auto sub = subActions[i];
                auto subName = sub.getProperty("action", "").toString().toStdString();
                if (subName.empty() || subName == "morph") continue;
                auto subArgs = sub.getProperty("args", juce::var());
                std::vector<AA::Value> subArgValues;
                if (auto* arr = subArgs.getArray()) {
                    for (int j = 0; j < arr->size(); ++j) {
                        auto v = (*arr)[j];
                        if (v.isDouble() || v.isInt() || v.isInt64())
                            subArgValues.push_back(AA::num((double)v));
                        else
                            subArgValues.push_back(AA::text(v.toString().toStdString()));
                    }
                }
                children.push_back(AA::invoke(subName, std::move(subArgValues)));
            }
            return (modeStr == "sequential") ? AA::sequence(std::move(children))
                                              : AA::parallel(std::move(children));
        });

    perfLog("[Coordinator] Registered %d built-in actions\n", (int)stateAPI->allActions().size());
}

void PerformanceCoordinator::restoreBindings() {
    songRuntime->clearBindings();

    for (auto& binding : stateAPI->effectiveBindings()) {
        auto* action = stateAPI->findActionById(binding.actionId);
        if (!action) continue;

        auto actionNameStr = action->name;
        auto argsStr = binding.args;
        MIDIControl control = { parseControlType(juce::String(binding.controlType)),
                                binding.channel, binding.number, binding.deviceId.str() };

        songRuntime->addBinding(control, [this, actionNameStr, argsStr](float value) {
            auto args = juce::JSON::parse(juce::String(argsStr));
            executeAction(actionNameStr, args, value);
        }, juce::String(binding.description));
    }
}
