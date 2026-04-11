#include "daw/Arrangement.h"
#include <juce_core/juce_core.h>

std::string Arrangement::generateId() {
    return juce::Uuid().toString().toStdString();
}

RegionState* Arrangement::addMidiRegion(const std::string& trackId,
                                         double startBeat, double lengthBeats) {
    if (!songTracks) return nullptr;
    for (auto& t : *songTracks) {
        if (t.id == trackId) {
            RegionState region;
            region.id = generateId();
            region.type = "midi";
            region.startBeat = startBeat;
            region.lengthBeats = lengthBeats;

            // Create initial empty take
            TakeState take;
            take.id = generateId();
            take.name = "Take 1";
            region.activeTakeId = take.id;
            region.takes.push_back(std::move(take));

            t.regions.push_back(std::move(region));
            return &t.regions.back();
        }
    }
    return nullptr;
}

void Arrangement::removeRegion(const std::string& regionId) {
    if (!songTracks) return;
    for (auto& t : *songTracks) {
        t.regions.erase(
            std::remove_if(t.regions.begin(), t.regions.end(),
                [&](auto& r) { return r.id == regionId; }),
            t.regions.end());
    }
}

std::vector<RegionState*> Arrangement::allRegions() const {
    std::vector<RegionState*> result;
    if (!songTracks) return result;
    for (auto& t : *songTracks)
        for (auto& r : t.regions)
            result.push_back(const_cast<RegionState*>(&r));
    return result;
}

std::vector<RegionState*> Arrangement::regionsForTrack(const std::string& trackId) const {
    std::vector<RegionState*> result;
    if (!songTracks) return result;
    for (auto& t : *songTracks) {
        if (t.id == trackId) {
            for (auto& r : t.regions)
                result.push_back(const_cast<RegionState*>(&r));
            break;
        }
    }
    return result;
}

RegionState* Arrangement::findRegion(const std::string& regionId) const {
    if (!songTracks) return nullptr;
    for (auto& t : *songTracks)
        for (auto& r : t.regions)
            if (r.id == regionId) return const_cast<RegionState*>(&r);
    return nullptr;
}

void Arrangement::scanMidiEvents(double prevBeat, double currentBeat,
                                  EventCallback callback) const {
    if (prevBeat >= currentBeat || !songTracks) return;

    for (auto& t : *songTracks) {
        for (auto& r : t.regions) {
            if (r.type != "midi") continue;
            double endBeat = r.startBeat + r.lengthBeats;
            if (endBeat <= prevBeat || r.startBeat >= currentBeat) continue;

            // Scan the active take's events
            auto* take = r.activeTake();
            if (!take) continue;

            for (auto& event : take->events) {
                double absBeat = r.startBeat + event.beatOffset;
                if (absBeat >= prevBeat && absBeat < currentBeat)
                    callback(t.id, event, absBeat);
            }
            // TODO: fire synthetic noteOffs at region end for unclosed notes
        }
    }
}

RegionState* Arrangement::startRecording(const std::string& trackId, double startBeat) {
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
