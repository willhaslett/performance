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

RegionState* Arrangement::findRegion(const RegionId& regionId) const {
    PERF_ASSERT(songTracks, "Arrangement: songTracks not set");
    for (auto& t : *songTracks)
        for (auto& r : t.regions)
            if (r.id == regionId) return const_cast<RegionState*>(&r);
    return nullptr;
}

// Compute effective loop end for a region (next region start, or explicit loopEndBeat)
static double computeLoopEnd(const RegionState& r, const std::vector<RegionState>& allRegions) {
    if (r.loopEndBeat > 0.0) return r.loopEndBeat;
    // Find next region on same track that starts after this one
    double regionEnd = r.startBeat + r.lengthBeats;
    double nearest = 1e9;
    for (auto& other : allRegions) {
        if (&other == &r || other.muted) continue;
        if (other.startBeat >= regionEnd)
            nearest = std::min(nearest, other.startBeat);
    }
    return nearest < 1e8 ? nearest : regionEnd + r.lengthBeats * 8;  // cap at 8 reps if no next region
}

void Arrangement::scanMidiEvents(double prevBeat, double currentBeat,
                                  EventCallback callback) const {
    if (prevBeat >= currentBeat || !songTracks) return;

    for (auto& t : *songTracks) {
        for (auto& r : t.regions) {
            if (r.type != "midi" || r.muted) continue;

            // Compute repetition range
            double regionEnd = r.startBeat + r.lengthBeats;
            double effectiveEnd = r.looped ? computeLoopEnd(r, t.regions) : regionEnd;
            if (effectiveEnd <= prevBeat || r.startBeat >= currentBeat) continue;

            // Number of repetitions to scan
            int maxReps = r.looped ? (int)std::ceil((effectiveEnd - r.startBeat) / r.lengthBeats) : 1;

            auto* take = r.activeTake();
            if (!take) continue;

            for (int rep = 0; rep < maxReps; ++rep) {
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
                            quantized = std::round(offset / r.quantize) * r.quantize;
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
