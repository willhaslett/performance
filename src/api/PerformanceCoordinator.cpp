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
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#include <set>

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

    // Restore saved audio devices (must be after loadInto so config is available)
    {
        auto& dm = audioEngine->getDeviceManager();
        auto setup = dm.getAudioDeviceSetup();
        bool changed = false;

        auto savedOutput = stateAPI->getConfig("audio_output_device");
        auto savedInput = stateAPI->getConfig("audio_input_device");

        perfLog("[Coordinator] Audio config: output saved='%s' current='%s', input saved='%s' current='%s'\n",
                savedOutput.c_str(), setup.outputDeviceName.toRawUTF8(),
                savedInput.c_str(), setup.inputDeviceName.toRawUTF8());

        if (!savedOutput.empty() && setup.outputDeviceName != juce::String(savedOutput)) {
            setup.outputDeviceName = juce::String(savedOutput);
            changed = true;
        }
        if (!savedInput.empty() && setup.inputDeviceName != juce::String(savedInput)) {
            setup.inputDeviceName = juce::String(savedInput);
            changed = true;
        }

        auto savedBuffer = stateAPI->getConfig("audio_buffer_size");
        if (!savedBuffer.empty()) {
            int bufSize = std::stoi(savedBuffer);
            if (setup.bufferSize != bufSize) {
                setup.bufferSize = bufSize;
                changed = true;
            }
        }

        if (changed) {
            auto err = dm.setAudioDeviceSetup(setup, true);
            if (err.isEmpty())
                perfLog("[Coordinator] Restored audio devices: out='%s', in='%s'\n",
                        setup.outputDeviceName.toRawUTF8(), setup.inputDeviceName.toRawUTF8());
            else
                perfLog("[Coordinator] Failed to restore audio devices: %s\n", err.toRawUTF8());
        }
    }

    // Then populate from engine scan — deduplicates by name, keeps DB IDs
    populatePluginCatalog();

    // Register built-in actions — deduplicates by name, keeps DB IDs
    registerBuiltinActions();

    automationEngine = std::make_unique<AutomationEngine>();
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
        } else {
            audioEngine->stopPlayback();
            stopRecording();
            recordModeActive = false;
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

    // Auto-save every 30 seconds if dirty
    startTimer(16);  // ~60Hz — drives sequencer clock smoothly, auto-save gated at 30s
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
            // Drain recorded MIDI events from audio thread
            if (isRecording) {
                drainRecordFIFO();

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

        lastSequencerBeat = uiBeat;
        lastSequencerTimeMs = juce::Time::getMillisecondCounterHiRes();
    }

    // Auto-save (every ~30 seconds, not every tick)
    static int saveCounter = 0;
    if (++saveCounter >= 1800) {  // ~1800 ticks at 60Hz = 30s
        saveCounter = 0;
        if (stateAPI && persistence && stateAPI->isDirty()) {
            persistence->saveFrom(*stateAPI);
            stateAPI->clearDirty();
            perfLog("[Coordinator] Auto-saved\n");
        }
    }
}

void PerformanceCoordinator::startRecordMode() {
    if (!sequencerImpl) return;
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
            auto wavFile = audioDir.getChildFile(juce::String(take->id) + ".wav");
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

    audioEngine->setRecording(true);
    isRecording = true;
    perfLog("[Coordinator] Recording started (%d MIDI, %d audio) at beat %.1f\n",
            (int)recordingTrackIds.size(),
            (int)audioRecordSessions.size(), recordStartBeat);
}

void PerformanceCoordinator::stopRecording() {
    if (!isRecording) return;

    audioEngine->setRecording(false);

    // Drain remaining MIDI events
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
    isRecording = false;
    recordingTrackIds.clear();

    // Resume undo — the next undo will revert to pre-recording state
    stateAPI->resumeUndo();

    perfLog("[Coordinator] Recording stopped at beat %.1f, total regions: %d\n",
            stopBeat, (int)arrangementImpl.allRegions().size());
}

void PerformanceCoordinator::drainRecordFIFO() {
    if (!audioEngine) return;
    auto& fifo = audioEngine->getRecordFIFO();
    RecordedMidiEvent event;
    while (fifo.pop(event)) {
        double beatOffset = event.beat - recordStartBeat;
        if (beatOffset < 0.0) continue;

        MidiEventState re;
        re.beatOffset = beatOffset;
        re.status = event.statusByte;
        re.channel = event.channel;
        re.data1 = event.data1;
        re.data2 = event.data2;
        arrangementImpl.addRecordedEvent(re);
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
    sequencerImpl->setTempo(stateAPI->getSongTempo());
    auto [num, den] = stateAPI->getSongTimeSignature();
    sequencerImpl->setTimeSignature(num, den);

    // Restore cycle range from song
    auto* song = stateAPI->currentSong();
    if (song && song->cycleEnd > song->cycleStart) {
        sequencerImpl->setLoopRange(song->cycleStart, song->cycleEnd);
        sequencerImpl->setLoopEnabled(song->cycleEnabled);
    } else {
        sequencerImpl->setLoopRange(0.0, 0.0);
        sequencerImpl->setLoopEnabled(false);
    }

    perfLog("[Coordinator] Synced tempo %.1f bpm, time sig %d/%d\n",
            stateAPI->getSongTempo(), num, den);
}

void PerformanceCoordinator::loadAudioFilesIntoEngine() {
    if (!stateAPI || !audioEngine) return;
    auto* song = stateAPI->currentSong();
    if (!song) return;

    for (auto& track : song->tracks) {
        if (track.sourceType != TrackSourceType::AudioInput) continue;

        // Load ALL audio regions for this track
        for (auto& region : track.regions) {
            if (region.type != "audio") continue;
            auto* take = region.activeTake();
            if (!take || take->filePath.empty()) continue;

            audioEngine->loadAudioFileForTrack(juce::String(track.id),
                juce::String(region.id), juce::String(take->filePath),
                take->recordTempo, take->sampleRate);

            if (take->peakData.peaks.empty())
                computeAudioPeaks(*take);
        }
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

void PerformanceCoordinator::shutdown() {
    stopTimer();
    if (stateAPI && stateSubscriptionId >= 0)
        stateAPI->events().unsubscribe(stateSubscriptionId);
    // Full save on shutdown — captures processor state and flushes
    if (stateAPI && persistence) {
        captureProcessorState();
        persistence->saveFrom(*stateAPI);
    }
    songRuntime.reset();
    midiEngine.reset();
    engineSync.reset();
    engineAPI.reset();
    audioEngine.reset();
    automationEngine.reset();
    persistence.reset();
    stateAPI.reset();
}

StateAPI& PerformanceCoordinator::state() { return *stateAPI; }
EngineAPI& PerformanceCoordinator::engine() { return *engineAPI; }

// --- Song lifecycle ---

std::string PerformanceCoordinator::createSong(const juce::String& name) {
    auto songId = stateAPI->createSong(name.toStdString());
    stateAPI->setCurrentSong(songId);
    // Every song gets an Actions track
    stateAPI->createActionTrack("Actions");
    perfLog("[Coordinator] Created song \"%s\" (id: %s)\n", name.toRawUTF8(), songId.c_str());
    return songId;
}

void PerformanceCoordinator::loadSong(const std::string& songId) {
    auto* song = stateAPI->findSong(songId);
    if (!song) {
        perfLog("[Coordinator] Song not found: %s\n", songId.c_str());
        return;
    }

    // Capture processor state from current song before switching
    captureProcessorState();

    songRuntime->clearBindings();
    stateAPI->setCurrentSong(songId);  // triggers EngineSync via config event
    restoreBindings();

    // Point arrangement at new song's tracks
    if (auto* newSong = stateAPI->findSong(songId))
        arrangementImpl.setTracks(&newSong->tracks);

    // Apply song tempo and time signature to sequencer
    syncTempoFromState();

    perfLog("[Coordinator] Loaded song: %s\n", song->name.c_str());
}

bool PerformanceCoordinator::restoreSession() {
    auto& songs = stateAPI->allSongs();

    if (songs.empty()) {
        auto songId = stateAPI->createSong("Sandbox");
        stateAPI->setCurrentSong(songId);
        stateAPI->createActionTrack("Actions");
        if (auto* s = stateAPI->currentSong())
            arrangementImpl.setTracks(&s->tracks);
        perfLog("[Coordinator] Created default session\n");
        return true;
    }

    // Restore the last active song
    auto lastSongId = stateAPI->getMasterOutputId();
    if (lastSongId.empty() || !stateAPI->findSong(lastSongId))
        lastSongId = songs[0].id;

    stateAPI->setCurrentSong(lastSongId);
    restoreBindings();

    // Point arrangement at restored song's tracks
    if (auto* s = stateAPI->currentSong())
        arrangementImpl.setTracks(&s->tracks);

    // Load audio files and sync tempo
    loadAudioFilesIntoEngine();
    syncTempoFromState();

    auto* song = stateAPI->currentSong();
    perfLog("[Coordinator] Session restored: %s (%d tracks)\n",
            song ? song->name.c_str() : "?",
            (int)stateAPI->listTracks().size());
    return true;
}

void PerformanceCoordinator::unloadSong() {
    stateAPI->setCurrentSong("");
    songRuntime->clearBindings();
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
        auto* proc = audioEngine->getTrackInstrumentProcessor(juce::String(track.id));
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
            auto* fxProc = audioEngine->getEffectProcessor(juce::String(track.id), juce::String(fx.id));
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
            auto* fxProc = audioEngine->getEffectProcessor(juce::String(bus.id), juce::String(fx.id));
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
        auto* fxProc = audioEngine->getEffectProcessor(juce::String("Output"), juce::String(fx.id));
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
        persistence->saveFrom(*stateAPI);
        perfLog("[Coordinator] Saved\n");
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

// --- Track presets ---

static juce::File getTrackPresetsDir() {
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/performance/track_presets");
}

void PerformanceCoordinator::saveTrackPreset(const juce::String& trackId,
                                              const juce::String& presetName) {
    auto* body = new juce::DynamicObject();

    auto pluginName = juce::String(stateAPI->getTrackPluginName(trackId.toStdString()));
    body->setProperty("plugin", pluginName);

    // Instrument state from engine
    if (auto* proc = audioEngine->getTrackInstrumentProcessor(trackId)) {
        juce::MemoryBlock memState;
        proc->getStateInformation(memState);
        body->setProperty("pluginState", memState.toBase64Encoding());
    }

    // Effects from state + engine processor state
    juce::Array<juce::var> effectsArr;
    for (auto& fx : stateAPI->getTrackEffects(trackId.toStdString())) {
        auto* fxObj = new juce::DynamicObject();
        fxObj->setProperty("plugin", juce::String(fx.pluginName));
        if (auto* proc = audioEngine->getEffectProcessor(trackId, juce::String(fx.effectId))) {
            juce::MemoryBlock memState;
            proc->getStateInformation(memState);
            fxObj->setProperty("state", memState.toBase64Encoding());
        }
        effectsArr.add(juce::var(fxObj));
    }
    body->setProperty("effects", effectsArr);

    // Sends from state
    juce::Array<juce::var> sendsArr;
    for (auto& send : stateAPI->getTrackSends(trackId.toStdString())) {
        auto* sendObj = new juce::DynamicObject();
        sendObj->setProperty("bus", juce::String(send.busName));
        sendObj->setProperty("gain", send.gain);
        sendsArr.add(juce::var(sendObj));
    }
    body->setProperty("sends", sendsArr);

    body->setProperty("gain", stateAPI->getTrackGain(trackId.toStdString()));
    body->setProperty("midiEnabled", stateAPI->isTrackMidiEnabled(trackId.toStdString()));

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
        stateAPI->clearTrackPlugin(trackId.toStdString());
        auto* plugin = stateAPI->findPluginByName(pluginName.toStdString());
        if (plugin)
            stateAPI->setTrackPlugin(trackId.toStdString(), plugin->id);

        // Store captured state on the track — EngineSync restores it
        // automatically when the plugin finishes async loading (LoadStatus → Loaded)
        auto stateB64 = json.getProperty("pluginState", "").toString();
        if (stateB64.isNotEmpty()) {
            auto* track = stateAPI->findTrack(trackId.toStdString());
            if (track) {
                track->processorState = stateB64.toStdString();
                track->processorStateHash.clear();  // will be set on next capture
            }
        }
    }

    // Effects
    if (auto* effectsArr = json.getProperty("effects", juce::var()).getArray()) {
        for (auto& fx : stateAPI->getTrackEffects(trackId.toStdString()))
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

    stateAPI->setTrackGain(trackId.toStdString(), (float)json.getProperty("gain", 1.0));
    stateAPI->setTrackMidiEnabled(trackId.toStdString(), (bool)json.getProperty("midiEnabled", true));
    stateAPI->renameTrack(trackId.toStdString(), presetName.toStdString());

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

    // Check for custom Lua action first
    auto* actionInfo = stateAPI->findActionByName(actionName);
    if (actionInfo && !actionInfo->luaCode.empty() && luaExecutor) {
        perfLog("[Coordinator] Executing custom action: %s\n", actionName.c_str());
        auto result = luaExecutor(actionInfo->luaCode);
        if (!result.empty() && result != "ok")
            perfLog("[Coordinator] Custom action error: %s\n", result.c_str());
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

    auto resolveTrack = [this](const juce::String& id) -> std::string {
        auto s = id.toStdString();
        if (!stateAPI->findTrack(s))
            perfLog("[Coordinator] resolveTrack: '%s' not found\n", s.c_str());
        return s;
    };

    if (actionName == "setActiveTrack") {
        auto targetId = resolveTrack(getArg(0));
        for (auto& t : stateAPI->listTracks()) {
            bool active = (t.id == targetId);
            stateAPI->setTrackMidiEnabled(t.id, active);
            stateAPI->setTrackAudioEnabled(t.id, active);
        }
    }
    else if (actionName == "enableTrack") {
        auto id = resolveTrack(getArg(0));
        stateAPI->setTrackMidiEnabled(id, true);
        stateAPI->setTrackAudioEnabled(id, true);
    }
    else if (actionName == "disableTrack") {
        auto id = resolveTrack(getArg(0));
        stateAPI->setTrackMidiEnabled(id, false);
        stateAPI->setTrackAudioEnabled(id, false);
    }
    else if (actionName == "fadeOut") {
        auto track = resolveTrack(getArg(0));
        auto dur = getArgFloat(1, 3.0f);
        float current = stateAPI->getTrackGain(track);
        automationEngine->interpolate(current, 0.0f, dur,
            [this, track](float v) { stateAPI->setTrackGain(track, v); },
            AutomationEngine::easingByName(getArg(2).toStdString()));
    }
    else if (actionName == "fadeIn") {
        auto track = resolveTrack(getArg(0));
        auto dur = getArgFloat(1, 3.0f);
        float current = stateAPI->getTrackGain(track);
        automationEngine->interpolate(current, 1.0f, dur,
            [this, track](float v) { stateAPI->setTrackGain(track, v); },
            AutomationEngine::easingByName(getArg(2).toStdString()));
    }
    else if (actionName == "crossfade") {
        auto from = resolveTrack(getArg(0));
        auto to = resolveTrack(getArg(1));
        auto dur = getArgFloat(2, 3.0f);
        auto easing = AutomationEngine::easingByName(getArg(3).toStdString());
        automationEngine->interpolate(1.0f, 0.0f, dur,
            [this, from](float v) { stateAPI->setTrackGain(from, v); }, easing);
        automationEngine->interpolate(0.0f, 1.0f, dur,
            [this, to](float v) { stateAPI->setTrackGain(to, v); }, easing);
    }
    else if (actionName == "trackVolume") {
        auto channelId = getArg(0).toStdString();
        float gain = value * value * value * 2.0f;  // cubic curve, +6dB max
        if (channelId == "output") {
            stateAPI->setMasterGain(gain);
        } else if (stateAPI->findTrack(channelId)) {
            stateAPI->setTrackGain(channelId, gain);
        } else if (stateAPI->findBus(channelId)) {
            stateAPI->setBusGain(channelId, gain);
        }
    }
    else if (actionName == "morphToPreset") {
        auto trackId = resolveTrack(getArg(0));
        auto presetName = getArg(1);
        auto dur = getArgFloat(2, 3.0f);
        auto easing = AutomationEngine::easingByName(getArg(3).toStdString());

        auto* proc = audioEngine->getTrackInstrumentProcessor(juce::String(trackId));
        if (!proc) {
            perfLog("[Action] morphToPreset: no processor for track %s\n", trackId.c_str());
        } else {
            auto pluginName = proc->getName();
            auto target = engineAPI->getPresetParams(pluginName, presetName);
            if (target.values.empty()) {
                perfLog("[Action] morphToPreset: no params for preset %s\n", presetName.toRawUTF8());
            } else {
                auto from = EngineAPI::captureParams(proc);
                automationEngine->interpolate(0.0f, 1.0f, dur,
                    [proc, from, target](float t) {
                        EngineAPI::applyParams(proc, target, t, from);
                    }, easing);
                perfLog("[Action] Morphing %s → %s over %.1fs (%d params)\n",
                        pluginName.toRawUTF8(), presetName.toRawUTF8(), dur, (int)target.values.size());
            }
        }
    }
    else if (actionName == "morphChain") {
        // morphChain(track, presetA, presetB, dwell, morphDuration, easing)
        // Load presetA instantly, wait dwell seconds, then morph to presetB
        auto trackId = resolveTrack(getArg(0));
        auto presetA = getArg(1);
        auto presetB = getArg(2);
        auto dwell = getArgFloat(3, 1.0f);
        auto morphDur = getArgFloat(4, 3.0f);
        auto easingName = getArg(5).toStdString();

        auto* proc = audioEngine->getTrackInstrumentProcessor(juce::String(trackId));
        if (!proc) {
            perfLog("[Action] morphChain: no processor for track %s\n", trackId.c_str());
        } else {
            auto pluginName = proc->getName();
            // Load preset A state blob immediately
            engineAPI->loadPreset(juce::String(trackId), "", presetA);
            perfLog("[Action] morphChain: loaded %s, dwell %.1fs then morph to %s over %.1fs\n",
                    presetA.toRawUTF8(), dwell, presetB.toRawUTF8(), morphDur);

            // After dwell, morph to B
            auto* eng = engineAPI.get();
            auto* ae = automationEngine.get();
            automationEngine->delay(dwell, [proc, pluginName, presetB, morphDur, easingName, eng, ae]() {
                auto target = eng->getPresetParams(pluginName, presetB);
                if (target.values.empty()) return;
                auto from = EngineAPI::captureParams(proc);
                auto easing = AutomationEngine::easingByName(easingName);
                ae->interpolate(0.0f, 1.0f, morphDur,
                    [proc, from, target](float t) {
                        EngineAPI::applyParams(proc, target, t, from);
                    }, easing);
            });
        }
    }
    else if (actionName == "morph") {
        auto mode = args.getProperty("mode", "parallel").toString();
        auto subActions = args.getProperty("actions", juce::var());
        if (!subActions.isArray()) {
            perfLog("[Action] morph: 'actions' is not an array\n");
        } else {
            bool sequential = (mode == "sequential");
            perfLog("[Action] Morph: %d sub-actions (%s)\n",
                    subActions.size(), mode.toRawUTF8());

            if (!sequential) {
                for (int i = 0; i < subActions.size(); ++i) {
                    auto sub = subActions[i];
                    auto subName = sub.getProperty("action", "").toString().toStdString();
                    if (subName == "morph") continue;  // no nested morphs
                    auto subArgs = sub.getProperty("args", juce::var());
                    executeAction(subName, subArgs, 1.0f);
                }
            } else {
                // Sequential: chain via completion callbacks
                // Build a list and execute recursively via delay
                struct ChainStep { std::string name; juce::var args; };
                auto steps = std::make_shared<std::vector<ChainStep>>();
                for (int i = 0; i < subActions.size(); ++i) {
                    auto sub = subActions[i];
                    auto subName = sub.getProperty("action", "").toString().toStdString();
                    if (subName == "morph") continue;  // no nested morphs
                    steps->push_back({ subName, sub.getProperty("args", juce::var()) });
                }

                // For sequential mode, we need to know when each action completes.
                // Estimate duration from args (duration field), default 0 = instant.
                auto runChain = std::make_shared<std::function<void(int)>>();
                *runChain = [this, steps, runChain](int idx) {
                    if (idx >= (int)steps->size()) return;
                    auto& step = (*steps)[idx];
                    executeAction(step.name, step.args, 1.0f);
                    // Get duration from args for delay to next
                    float dur = 0.0f;
                    if (step.args.isArray() && step.args.size() >= 3)
                        dur = (float)step.args[2];
                    else if (step.args.hasProperty("duration"))
                        dur = (float)step.args.getProperty("duration", 0.0f);
                    if (dur > 0.01f && idx + 1 < (int)steps->size()) {
                        automationEngine->delay(dur, [runChain, idx]() {
                            (*runChain)(idx + 1);
                        });
                    } else {
                        // Instant action — fire next immediately
                        if (idx + 1 < (int)steps->size())
                            (*runChain)(idx + 1);
                    }
                };
                (*runChain)(0);
            }
        }
    }
    else {
        perfLog("[Action] Unknown action: %s\n", actionName.c_str());
    }
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

    if (event.entity == StateEvent::Track) {
        auto* track = stateAPI->findTrack(event.entityId);
        if (!track || track->instrumentLoadStatus != LoadStatus::Loaded) return;
        if (track->pluginId.empty()) return;
        ensureDefaultPreset(track->id, "", track->pluginId, PresetKind::Instrument);
    }
    else if (event.entity == StateEvent::Effect) {
        auto* fx = stateAPI->findEffect(event.entityId);
        if (!fx || fx->loadStatus != LoadStatus::Loaded) return;
        if (fx->pluginId.empty()) return;
        ensureDefaultPreset(event.parentId, fx->id, fx->pluginId, PresetKind::Effect);
    }
}

void PerformanceCoordinator::ensureDefaultPreset(const std::string& parentId,
                                                   const std::string& effectId,
                                                   const std::string& pluginId,
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
    stateAPI->registerAction("setActiveTrack", "Set active track",
        R"([{"name":"trackName","type":"string"}])");
    stateAPI->registerAction("enableTrack", "Enable track",
        R"([{"name":"trackName","type":"string"}])");
    stateAPI->registerAction("disableTrack", "Disable track",
        R"([{"name":"trackName","type":"string"}])");
    stateAPI->registerAction("fadeOut", "Fade out",
        R"([{"name":"trackName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])", 1);
    stateAPI->registerAction("fadeIn", "Fade in",
        R"([{"name":"trackName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])", 1);
    stateAPI->registerAction("crossfade", "Crossfade",
        R"([{"name":"fromTrack","type":"string"},{"name":"toTrack","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])", 2);
    stateAPI->registerAction("trackVolume", "Track Volume",
        R"([{"name":"channel","type":"channel"}])");
    stateAPI->registerAction("morphToPreset", "Morph to preset",
        R"([{"name":"trackName","type":"string"},{"name":"presetName","type":"string"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])", 2);
    stateAPI->registerAction("morphChain", "Morph chain (A \xe2\x86\x92 dwell \xe2\x86\x92 B)",
        R"([{"name":"trackName","type":"string"},{"name":"presetA","type":"string"},{"name":"presetB","type":"string"},{"name":"dwell","type":"float"},{"name":"duration","type":"float"},{"name":"easing","type":"string"}])", 4);
    stateAPI->registerAction("morph", "Morph",
        R"([{"name":"mode","type":"morph"}])");

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
                                binding.channel, binding.number, binding.deviceId };

        songRuntime->addBinding(control, [this, actionNameStr, argsStr](float value) {
            auto args = juce::JSON::parse(juce::String(argsStr));
            executeAction(actionNameStr, args, value);
        }, juce::String(binding.description));
    }
}
