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

            for (auto& event : r.events) {
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
    if (region)
        recordingRegions.push_back(region);
    return region;
}

void Arrangement::addRecordedEvent(const RegionState::Event& event) {
    for (auto* region : recordingRegions) {
        region->events.push_back(event);
        double end = event.beatOffset + 0.1;
        if (end > region->lengthBeats)
            region->lengthBeats = end;
    }
}

void Arrangement::stopRecording() {
    for (auto* region : recordingRegions) {
        std::sort(region->events.begin(), region->events.end(),
                  [](auto& a, auto& b) { return a.beatOffset < b.beatOffset; });
    }
    recordingRegions.clear();
    // TODO: inject synthetic noteOffs for unclosed notes at stop beat
}

std::vector<NoteView> Arrangement::buildNoteList(const RegionState& region) {
    std::vector<NoteView> notes;
    std::map<std::pair<int,int>, int> openNotes;  // {noteNumber, channel} → index in notes

    for (auto& e : region.events) {
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
    // Close unclosed notes at region end
    for (auto& [key, idx] : openNotes) {
        if (notes[idx].durationBeats <= 0.0)
            notes[idx].durationBeats = region.lengthBeats - notes[idx].beatOffset;
    }
    return notes;
}
