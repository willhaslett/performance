#include "daw/Arrangement.h"
#include "engine/Log.h"
#include <juce_core/juce_core.h>
#include <map>

std::string Arrangement::generateId() {
    return juce::Uuid().toString().toStdString();
}

RegionState* Arrangement::addMidiRegion(const TrackId& trackId,
                                         double startBeat, double lengthBeats) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks) {
        if (t.id == trackId) {
            RegionState region;
            region.id = RegionId{generateId()};
            region.type = "midi";
            region.startBeat = startBeat;
            region.lengthBeats = lengthBeats;

            // Create initial empty take
            TakeState take;
            take.id = TakeId{generateId()};
            take.name = "Take 1";
            region.activeTakeId = take.id;
            region.takes.push_back(std::move(take));

            t.regions.push_back(std::move(region));
            return &t.regions.back();
        }
    }
    return nullptr;
}

void Arrangement::removeRegion(const RegionId& regionId) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks) {
        t.regions.erase(
            std::remove_if(t.regions.begin(), t.regions.end(),
                [&](auto& r) { return r.id == regionId; }),
            t.regions.end());
    }
}

void Arrangement::moveRegion(const RegionId& regionId,
                              const TrackId& newTrackId, double newStartBeat) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");

    // Find the region and its current track
    RegionState found;
    bool removed = false;
    for (auto& t : *songTracks) {
        for (int i = 0; i < (int)t.regions.size(); ++i) {
            if (t.regions[i].id == regionId) {
                found = std::move(t.regions[i]);
                t.regions.erase(t.regions.begin() + i);
                removed = true;
                break;
            }
        }
        if (removed) break;
    }
    if (!removed) return;

    found.startBeat = newStartBeat;

    // Insert into target track
    for (auto& t : *songTracks) {
        if (t.id == newTrackId) {
            t.regions.push_back(std::move(found));
            return;
        }
    }
    // Target track not found — shouldn't happen, but put it back somewhere
}

RegionState* Arrangement::duplicateRegion(const RegionId& regionId,
                                           const TrackId& targetTrackId,
                                           double startBeat) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");

    // Find source region
    const RegionState* source = nullptr;
    for (auto& t : *songTracks) {
        for (auto& r : t.regions) {
            if (r.id == regionId) { source = &r; break; }
        }
        if (source) break;
    }
    if (!source) return nullptr;

    // Find target track
    for (auto& t : *songTracks) {
        if (t.id == targetTrackId) {
            RegionState copy;
            copy.id = RegionId{generateId()};
            copy.type = source->type;
            copy.name = source->name;
            copy.startBeat = startBeat;
            copy.lengthBeats = source->lengthBeats;

            // Deep-copy takes
            for (auto& srcTake : source->takes) {
                TakeState takeCopy;
                takeCopy.id = TakeId{generateId()};
                takeCopy.name = srcTake.name;
                takeCopy.events = srcTake.events;
                takeCopy.filePath = srcTake.filePath;
                takeCopy.recordTempo = srcTake.recordTempo;
                takeCopy.sampleRate = srcTake.sampleRate;
                takeCopy.channelCount = srcTake.channelCount;
                takeCopy.peakData = srcTake.peakData;
                if (srcTake.id == source->activeTakeId)
                    copy.activeTakeId = takeCopy.id;
                copy.takes.push_back(std::move(takeCopy));
            }
            if (copy.activeTakeId.empty() && !copy.takes.empty())
                copy.activeTakeId = copy.takes[0].id;

            t.regions.push_back(std::move(copy));
            return &t.regions.back();
        }
    }
    return nullptr;
}

RegionState* Arrangement::splitRegion(const RegionId& regionId, double splitBeat, bool splitNotes) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");

    for (auto& t : *songTracks) {
        for (size_t ri = 0; ri < t.regions.size(); ++ri) {
            auto& r = t.regions[ri];
            if (r.id != regionId) continue;

            double regionEnd = r.startBeat + r.lengthBeats;
            if (splitBeat <= r.startBeat || splitBeat >= regionEnd)
                return nullptr;  // split point outside region

            double splitOffset = splitBeat - r.startBeat;  // beat offset within region

            // Create right-side region
            RegionState right;
            right.id = RegionId{generateId()};
            right.type = r.type;
            right.name = r.name;
            right.startBeat = splitBeat;
            right.lengthBeats = regionEnd - splitBeat;
            right.quantize = r.quantize;

            if (r.type == "midi") {
                auto* srcTake = r.activeTake();
                if (srcTake) {
                    // Build note list to find crossing notes
                    auto notes = buildNoteList(*srcTake, r.lengthBeats);

                    TakeState leftTake;
                    leftTake.id = TakeId{generateId()};
                    leftTake.name = srcTake->name;

                    TakeState rightTake;
                    rightTake.id = TakeId{generateId()};
                    rightTake.name = srcTake->name;

                    // Partition events
                    for (auto& ev : srcTake->events) {
                        if (ev.beatOffset < splitOffset) {
                            leftTake.events.push_back(ev);
                        } else {
                            MidiEventState shifted = ev;
                            shifted.beatOffset -= splitOffset;
                            rightTake.events.push_back(shifted);
                        }
                    }

                    // Handle crossing notes
                    for (auto& note : notes) {
                        double noteEnd = note.beatOffset + note.durationBeats;
                        if (note.beatOffset < splitOffset && noteEnd > splitOffset) {
                            // Note crosses the split point
                            // Add noteOff at split point in left region
                            MidiEventState offEv;
                            offEv.beatOffset = splitOffset - 0.001;
                            offEv.status = 0x80;
                            offEv.channel = note.channel;
                            offEv.data1 = note.noteNumber;
                            offEv.data2 = 0;
                            leftTake.events.push_back(offEv);

                            if (splitNotes) {
                                // Re-attack in right region
                                MidiEventState onEv;
                                onEv.beatOffset = 0.0;
                                onEv.status = 0x90;
                                onEv.channel = note.channel;
                                onEv.data1 = note.noteNumber;
                                onEv.data2 = note.velocity;
                                rightTake.events.push_back(onEv);
                            }
                            // Remove the original noteOff from right (it was already shifted)
                            // No action needed — the noteOff was after the split so it's in rightTake
                        }
                    }

                    // Sort events by beat offset
                    auto sortEvents = [](std::vector<MidiEventState>& evs) {
                        std::sort(evs.begin(), evs.end(),
                                  [](auto& a, auto& b) { return a.beatOffset < b.beatOffset; });
                    };
                    sortEvents(leftTake.events);
                    sortEvents(rightTake.events);

                    // Replace left region's take
                    r.takes.clear();
                    r.takes.push_back(std::move(leftTake));
                    r.activeTakeId = r.takes[0].id;

                    right.takes.push_back(std::move(rightTake));
                    right.activeTakeId = right.takes[0].id;
                }
            } else {
                // Audio region: copy take references, adjust nothing
                // (audio playback uses beat-to-sample, split just changes region bounds)
                for (auto& srcTake : r.takes) {
                    TakeState copy;
                    copy.id = TakeId{generateId()};
                    copy.name = srcTake.name;
                    copy.filePath = srcTake.filePath;
                    copy.recordTempo = srcTake.recordTempo;
                    copy.sampleRate = srcTake.sampleRate;
                    copy.channelCount = srcTake.channelCount;
                    copy.peakData = srcTake.peakData;
                    if (srcTake.id == r.activeTakeId)
                        right.activeTakeId = copy.id;
                    right.takes.push_back(std::move(copy));
                }
                if (right.activeTakeId.empty() && !right.takes.empty())
                    right.activeTakeId = right.takes[0].id;
            }

            // Trim left region
            r.lengthBeats = splitOffset;

            // Add right region
            t.regions.push_back(std::move(right));
            return &t.regions.back();
        }
    }
    return nullptr;
}

std::vector<RegionState*> Arrangement::allRegions() const {
    std::vector<RegionState*> result;
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks)
        for (auto& r : t.regions)
            result.push_back(const_cast<RegionState*>(&r));
    return result;
}

std::vector<RegionState*> Arrangement::regionsForTrack(const TrackId& trackId) const {
    std::vector<RegionState*> result;
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks) {
        if (t.id == trackId) {
            for (auto& r : t.regions)
                result.push_back(const_cast<RegionState*>(&r));
            break;
        }
    }
    return result;
}

RegionState* Arrangement::loopForTrack(const TrackId& trackId) const {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks) {
        if (t.id == trackId)
            return t.loops.empty() ? nullptr
                                    : const_cast<RegionState*>(&t.loops[0]);
    }
    return nullptr;
}

RegionState* Arrangement::findRegion(const RegionId& regionId) const {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks)
        for (auto& r : t.regions)
            if (r.id == regionId) return const_cast<RegionState*>(&r);
    return nullptr;
}

// Compute effective loop end for a region (next region start, or explicit loopEndBeat).
// When there's no next region on the track, the loop runs indefinitely — Logic-style.
// The audio scan bounds iteration by the playhead's `currentBeat` per block, so an
// unbounded effectiveEnd doesn't blow up; the inner loop short-circuits as soon as
// repBase >= currentBeat.
static double computeLoopEnd(const RegionState& r, const std::vector<RegionState>& allRegions) {
    if (r.loopEndBeat > 0.0) return r.loopEndBeat;
    double regionEnd = r.startBeat + r.lengthBeats;
    double nearest = 1e9;
    for (auto& other : allRegions) {
        if (&other == &r || other.muted) continue;
        if (other.startBeat >= regionEnd)
            nearest = std::min(nearest, other.startBeat);
    }
    return nearest < 1e8 ? nearest : 1e9;  // unbounded — playhead bounds the actual scan
}

void Arrangement::scanMidiEvents(double prevBeat, double currentBeat,
                                  EventCallback callback) const {
    if (looperModeActive) scanLoopEvents(prevBeat, currentBeat, callback);
    else                  scanArrangementEvents(prevBeat, currentBeat, callback);
}

void Arrangement::scanArrangementEvents(double prevBeat, double currentBeat,
                                         EventCallback callback) const {
    if (prevBeat >= currentBeat || !songTracks) return;

    for (auto& t : *songTracks) {
        for (auto& r : t.regions) {
            if (r.type != "midi" || r.muted) continue;

            // Compute repetition range
            double regionEnd = r.startBeat + r.lengthBeats;
            double effectiveEnd = r.looped ? computeLoopEnd(r, t.regions) : regionEnd;
            if (effectiveEnd <= prevBeat || r.startBeat >= currentBeat) continue;

            // Number of repetitions to scan. effectiveEnd is unbounded when
            // a looped region has no following region on its track — the
            // inner loop short-circuits via `repBase >= currentBeat`, so
            // we don't actually iterate to infinity. firstRep skips ahead
            // to the rep that overlaps the scan window so we don't burn
            // time on a continue-loop for long-running playback.
            int maxReps = r.looped ? (int)std::ceil((effectiveEnd - r.startBeat) / r.lengthBeats) : 1;
            int firstRep = 0;
            if (r.looped && prevBeat > r.startBeat && r.lengthBeats > 0)
                firstRep = std::max(0, (int)std::floor((prevBeat - r.startBeat) / r.lengthBeats));

            auto* take = r.activeTake();
            if (!take) continue;

            // Skip regions whose take is currently being recorded into.
            // The user hears their live MIDI via midiInputNode → plugin
            // already; if we ALSO scan-emit this region we duplicate the
            // events AND fire a synthetic noteOff at the boundary
            // (regionLen = lastEvent + 0.1, which marches just behind
            // every new note), which kills the live voice and makes the
            // performance sound staccato. The recorded events still
            // land via the FIFO drain — only the scanner's view of the
            // in-flight region is suppressed. Region "wakes up" in
            // scan as soon as recording stops and recordingTakes empties.
            bool takeIsRecording = std::find(recordingTakes.begin(),
                                              recordingTakes.end(), take)
                                     != recordingTakes.end();
            if (takeIsRecording) continue;

            for (int rep = firstRep; rep < maxReps; ++rep) {
                double repBase = r.startBeat + rep * r.lengthBeats;
                if (repBase >= effectiveEnd || repBase >= currentBeat) break;
                if (repBase + r.lengthBeats <= prevBeat) continue;

                // Track open notes for synthetic noteOff at boundary
                // Key: (channel << 8 | note), Value: true if on
                std::map<int, MidiEventState> openNotes;

                // Emit events + track note state
                auto emitEvent = [&](const MidiEventState& event, double absBeat) {
                    bool isNoteOn = (event.status & 0xF0) == 0x90 && event.data2 > 0;
                    bool isNoteOff = (event.status & 0xF0) == 0x80
                                  || ((event.status & 0xF0) == 0x90 && event.data2 == 0);
                    int noteKey = (event.channel << 8) | event.data1;

                    if (isNoteOn) openNotes[noteKey] = event;
                    else if (isNoteOff) openNotes.erase(noteKey);

                    if (absBeat >= prevBeat && absBeat < currentBeat)
                        callback(t.id, event, absBeat);
                };

                if (r.quantize > 0.0) {
                    // Quantize in GLOBAL beat space, not region-local —
                    // otherwise quantized notes snap to a grid offset by
                    // (region.startBeat % grid), so they land slightly off
                    // the timeline gridlines and out of sync with the
                    // metronome. Round trip: region-local offset → global
                    // → round to grid → back to region-local.
                    std::map<int, double> noteShifts;
                    for (auto& event : take->events) {
                        double offset = event.beatOffset;
                        if (offset < 0.0 || offset >= r.lengthBeats) continue;

                        bool isNoteOn = (event.status & 0xF0) == 0x90 && event.data2 > 0;
                        bool isNoteOff = (event.status & 0xF0) == 0x80
                                      || ((event.status & 0xF0) == 0x90 && event.data2 == 0);
                        int noteKey = (event.channel << 8) | event.data1;

                        double quantized = offset;
                        if (isNoteOn) {
                            double absOffset = repBase + offset;
                            double absQuant = std::round(absOffset / r.quantize) * r.quantize;
                            quantized = absQuant - repBase;
                            noteShifts[noteKey] = quantized - offset;
                        } else if (isNoteOff) {
                            auto it = noteShifts.find(noteKey);
                            if (it != noteShifts.end())
                                quantized = offset + it->second;
                        }

                        double absBeat = repBase + quantized;
                        if (absBeat >= effectiveEnd) continue;
                        emitEvent(event, absBeat);
                    }
                } else {
                    for (auto& event : take->events) {
                        double offset = event.beatOffset;
                        if (offset < 0.0 || offset >= r.lengthBeats) continue;
                        double absBeat = repBase + offset;
                        if (absBeat >= effectiveEnd) continue;
                        emitEvent(event, absBeat);
                    }
                }

                // Synthetic noteOffs at the repetition/region boundary
                double boundaryBeat = std::min(repBase + r.lengthBeats, effectiveEnd);
                if (boundaryBeat >= prevBeat && boundaryBeat < currentBeat) {
                    for (auto& [noteKey, onEvent] : openNotes) {
                        MidiEventState offEvent;
                        offEvent.status = 0x80;
                        offEvent.channel = onEvent.channel;
                        offEvent.data1 = onEvent.data1;
                        offEvent.data2 = 0;
                        offEvent.beatOffset = 0;  // not meaningful for synthetic
                        callback(t.id, offEvent, boundaryBeat);
                    }
                }
            }  // end rep loop
        }
    }
}

// Looper-mode MIDI scan. Plays each track's FIRST loop region (track.loops[0])
// via modular playback: regionPosition = cyclePos mod region.lengthBeats.
// See docs/LIVE_LOOPING.md for the rule — a 4-bar loop in a 16-bar cycle
// plays 4× per cycle pass; a 20-bar loop in a 16-bar cycle plays its first
// 16 bars and wraps.
//
// prevBeat / currentBeat are cycle-relative positions (the sequencer wraps
// at cycle end, so they never span the wrap — they're always in [0, cycle]).
// If the cycle length is unknown (looperCycleLengthBeats <= 0), no-op.
void Arrangement::scanLoopEvents(double prevBeat, double currentBeat,
                                  EventCallback callback) const {
    if (prevBeat >= currentBeat || !songTracks) return;
    if (looperCycleLengthBeats <= 0.0) return;

    for (auto& t : *songTracks) {
        if (t.loops.empty()) continue;
        auto& r = t.loops[0];  // one loop per track in looper mode
        if (r.type != "midi" || r.muted) continue;
        if (r.lengthBeats <= 0.0) continue;
        // Replace-in-progress silences the existing content immediately —
        // the user is recording over it, the old take is about to be
        // discarded on commit. Overdub keeps it (overdubbing layers on).
        if (r.loopAction == LoopAction::CapturingReplace) continue;

        auto* take = r.activeTake();
        if (!take) continue;

        // Repetition count within one cycle pass. If the region is
        // longer than the cycle, we only play its first cycleLength beats
        // (no repetition — the tail is preserved on disk, silent here).
        double playableLength = std::min(r.lengthBeats, looperCycleLengthBeats);
        (void) playableLength;  // reserved for future clip-to-cycle hygiene

        int maxReps = (int) std::ceil(looperCycleLengthBeats / r.lengthBeats);
        if (maxReps < 1) maxReps = 1;

        std::map<int, MidiEventState> openNotes;

        auto emitEvent = [&](const MidiEventState& event, double absBeat) {
            bool isNoteOn = (event.status & 0xF0) == 0x90 && event.data2 > 0;
            bool isNoteOff = (event.status & 0xF0) == 0x80
                          || ((event.status & 0xF0) == 0x90 && event.data2 == 0);
            int noteKey = (event.channel << 8) | event.data1;

            if (isNoteOn) openNotes[noteKey] = event;
            else if (isNoteOff) openNotes.erase(noteKey);

            if (absBeat >= prevBeat && absBeat < currentBeat)
                callback(t.id, event, absBeat);
        };

        for (int rep = 0; rep < maxReps; ++rep) {
            double repBase = rep * r.lengthBeats;
            // Stop if this repetition starts beyond the cycle.
            if (repBase >= looperCycleLengthBeats) break;
            // Window this repetition's events to the current scan range.
            if (repBase + r.lengthBeats <= prevBeat) continue;

            for (auto& event : take->events) {
                double offset = event.beatOffset;
                if (offset < 0.0 || offset >= r.lengthBeats) continue;
                double absBeat = repBase + offset;
                // Clip events whose tail sits past the cycle boundary:
                // a 20-bar loop in a 16-bar cycle has its beats 16–20
                // inaccessible until the user extends the cycle.
                if (absBeat >= looperCycleLengthBeats) continue;
                emitEvent(event, absBeat);
            }

            // Synthetic noteOffs at this repetition's boundary so notes
            // don't hang when the loop wraps onto itself (same hygiene
            // as the arrangement-scan path).
            double boundaryBeat = std::min(repBase + r.lengthBeats,
                                            looperCycleLengthBeats);
            if (boundaryBeat >= prevBeat && boundaryBeat < currentBeat) {
                for (auto& [_noteKey, onEvent] : openNotes) {
                    MidiEventState offEvent;
                    offEvent.status = 0x80;
                    offEvent.channel = onEvent.channel;
                    offEvent.data1 = onEvent.data1;
                    offEvent.data2 = 0;
                    offEvent.beatOffset = 0;
                    callback(t.id, offEvent, boundaryBeat);
                }
                openNotes.clear();
            }
        }
    }
}

void Arrangement::scanActionEvents(double prevBeat, double currentBeat,
                                    ActionCallback callback) const {
    if (prevBeat >= currentBeat || !songTracks) return;

    for (auto& t : *songTracks) {
        if (t.sourceType != TrackSourceType::Action || t.muted) continue;
        for (auto& ae : t.actionData) {
            if (ae.beat >= prevBeat && ae.beat < currentBeat) {
                SongState::ActionEvent ev;
                ev.id = ae.id;
                ev.beat = ae.beat;
                ev.actionId = ae.actionId;
                ev.argsJson = ae.argsJson;
                callback(ev);
            }
        }
    }
}

RegionState* Arrangement::startRecording(const TrackId& trackId, double startBeat) {
    auto* region = addMidiRegion(trackId, startBeat, 0.0);
    if (region && region->activeTake())
        recordingTakes.push_back(region->activeTake());
    return region;
}

void Arrangement::addRecordedEvent(const MidiEventState& event) {
    for (auto* take : recordingTakes) {
        take->events.push_back(event);
        double end = event.beatOffset + 0.1;
        // Find parent region to extend length — walk tracks
        if (songTracks) {
            for (auto& t : *songTracks) {
                for (auto& r : t.regions) {
                    for (auto& tk : r.takes) {
                        if (&tk == take && end > r.lengthBeats) {
                            r.lengthBeats = end;
                            goto done;
                        }
                    }
                }
            }
            done:;
        }
    }
}

void Arrangement::stopRecording() {
    for (auto* take : recordingTakes) {
        std::sort(take->events.begin(), take->events.end(),
                  [](auto& a, auto& b) { return a.beatOffset < b.beatOffset; });
    }
    recordingTakes.clear();
    // TODO: inject synthetic noteOffs for unclosed notes at stop beat
}

// --- Loop recording ---
//
// Unlike arrangement recording (which creates a new region per pass),
// loop recording adds a new take to the track's single loop region.
// The region is created lazily on the first punch-in and persists
// across subsequent captures; `activeTakeId` moves to the latest.
RegionState* Arrangement::getOrCreateLoopRegion(const TrackId& trackId) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks) {
        if (t.id != trackId) continue;
        if (!t.loops.empty()) return &t.loops[0];
        RegionState r;
        r.id = RegionId{generateId()};
        r.type = "midi";           // caller sets "audio" when placing a file
        r.startBeat = 0.0;
        r.lengthBeats = 0.0;
        TakeState take;
        take.id = TakeId{generateId()};
        take.name = "Take 1";
        r.takes.push_back(std::move(take));
        r.activeTakeId = r.takes.front().id;
        t.loops.push_back(std::move(r));
        return &t.loops[0];
    }
    return nullptr;
}

RegionState* Arrangement::startLoopRecording(const TrackId& trackId) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks) {
        if (t.id != trackId) continue;

        // Ensure there's a loop region; create one if the track has none.
        RegionState* region = t.loops.empty() ? nullptr : &t.loops[0];
        if (!region) {
            RegionState r;
            r.id = RegionId{generateId()};
            r.type = "midi";
            r.startBeat = 0.0;
            r.lengthBeats = 0.0;  // finalized in stopLoopRecording
            t.loops.push_back(std::move(r));
            region = &t.loops[0];
        }

        // Append a new take and route captured events into it.
        TakeState take;
        take.id = TakeId{generateId()};
        take.name = "Take " + std::to_string(region->takes.size() + 1);
        region->takes.push_back(std::move(take));
        recordingTakes.push_back(&region->takes.back());
        return region;
    }
    return nullptr;
}

void Arrangement::discardLastLoopRecording(const TrackId& trackId) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks) {
        if (t.id != trackId) continue;
        if (t.loops.empty()) return;
        auto& region = t.loops[0];
        if (region.takes.empty()) return;

        auto* discarded = &region.takes.back();
        recordingTakes.erase(
            std::remove(recordingTakes.begin(), recordingTakes.end(), discarded),
            recordingTakes.end());
        region.takes.pop_back();

        // If we took the last take out of a region that was freshly
        // created (lengthBeats still 0, no other content), remove the
        // region itself so the track doesn't carry an empty shell.
        if (region.takes.empty() && region.lengthBeats == 0.0) {
            t.loops.erase(t.loops.begin());
        }
        return;
    }
}

void Arrangement::stopLoopRecording(const TrackId& trackId, double lengthBeats) {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks) {
        if (t.id != trackId) continue;
        if (t.loops.empty()) return;
        auto& region = t.loops[0];
        if (region.takes.empty()) return;

        auto* newTake = &region.takes.back();
        // Remove this take from the recording set (if present).
        recordingTakes.erase(
            std::remove(recordingTakes.begin(), recordingTakes.end(), newTake),
            recordingTakes.end());

        std::sort(newTake->events.begin(), newTake->events.end(),
                  [](auto& a, auto& b) { return a.beatOffset < b.beatOffset; });
        // Promote the freshly-recorded take to active. Previous takes
        // persist; performer can swap back to them via setActiveTake.
        region.activeTakeId = newTake->id;
        region.lengthBeats = lengthBeats;
        // Diagnostic: dump the sorted event list so we can spot ordering
        // anomalies (noteOff-before-noteOn, duplicate noteOns, etc.)
        // that would cause stuck-note playback. Remove once the loop
        // recording/playback hang investigation is done.
        perfLog("[LoopDump] stopLoopRecording track=%s take=%s lengthBeats=%.3f events=%zu\n",
                trackId.c_str(), newTake->id.str().c_str(),
                lengthBeats, newTake->events.size());
        for (size_t i = 0; i < newTake->events.size(); ++i) {
            auto& e = newTake->events[i];
            const char* kind = e.isNoteOn() ? "ON " : e.isNoteOff() ? "OFF" : "---";
            perfLog("[LoopDump]   [%zu] beat=%.4f status=0x%02x ch=%d note=%d vel=%d %s\n",
                    i, e.beatOffset, e.status, e.channel, e.data1, e.data2, kind);
        }
        return;
    }
}

std::vector<NoteView> Arrangement::buildNoteList(const TakeState& take, double regionLength) {
    std::vector<NoteView> notes;
    std::map<std::pair<int,int>, int> openNotes;

    for (auto& e : take.events) {
        if (e.isNoteOn()) {
            int idx = (int)notes.size();
            notes.push_back({ e.beatOffset, 0.0, e.data1, e.data2, e.channel });
            openNotes[{e.data1, e.channel}] = idx;
        } else if (e.isNoteOff()) {
            auto it = openNotes.find({e.data1, e.channel});
            if (it != openNotes.end()) {
                notes[it->second].durationBeats = e.beatOffset - notes[it->second].beatOffset;
                openNotes.erase(it);
            }
        }
    }
    for (auto& [key, idx] : openNotes) {
        if (notes[idx].durationBeats <= 0.0)
            notes[idx].durationBeats = regionLength - notes[idx].beatOffset;
    }
    return notes;
}

std::vector<NoteView> Arrangement::buildNoteList(const RegionState& region) {
    auto* take = region.activeTake();
    if (!take) return {};
    return buildNoteList(*take, region.lengthBeats);
}
