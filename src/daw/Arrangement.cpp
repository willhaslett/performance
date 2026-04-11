#include "daw/Arrangement.h"
#include <juce_core/juce_core.h>

std::string Arrangement::generateId() {
    return juce::Uuid().toString().toStdString();
}

MidiRegion* Arrangement::addMidiRegion(const std::string& trackId,
                                        double startBeat, double lengthBeats) {
    auto region = std::make_unique<MidiRegion>();
    region->id = generateId();
    region->trackId = trackId;
    region->startBeat = startBeat;
    region->lengthBeats = lengthBeats;
    auto* ptr = region.get();
    regions.push_back(std::move(region));
    return ptr;
}

void Arrangement::removeRegion(const std::string& regionId) {
    regions.erase(
        std::remove_if(regions.begin(), regions.end(),
            [&](auto& r) { return r->id == regionId; }),
        regions.end());
}

void Arrangement::clearTrack(const std::string& trackId) {
    regions.erase(
        std::remove_if(regions.begin(), regions.end(),
            [&](auto& r) { return r->trackId == trackId; }),
        regions.end());
}

void Arrangement::clearAll() {
    regions.clear();
    recordingRegions.clear();
}

std::vector<Region*> Arrangement::regionsForTrack(const std::string& trackId) const {
    std::vector<Region*> result;
    for (auto& r : regions)
        if (r->trackId == trackId) result.push_back(r.get());
    return result;
}

Region* Arrangement::findRegion(const std::string& regionId) const {
    for (auto& r : regions)
        if (r->id == regionId) return r.get();
    return nullptr;
}

void Arrangement::scanMidiEvents(double prevBeat, double currentBeat,
                                  MidiEventCallback callback) const {
    if (prevBeat >= currentBeat) return;

    for (auto& r : regions) {
        if (r->type() != Region::Type::Midi) continue;
        auto* midi = static_cast<MidiRegion*>(r.get());

        if (midi->endBeat() <= prevBeat || midi->startBeat >= currentBeat) continue;

        for (auto& event : midi->events) {
            double absBeat = midi->startBeat + event.beatOffset;
            if (absBeat >= prevBeat && absBeat < currentBeat)
                callback(midi->trackId, event, absBeat);
        }
        // TODO: fire synthetic noteOffs at region end for unclosed notes (stuck note prevention)
    }
}

MidiRegion* Arrangement::startRecording(const std::string& trackId, double startBeat) {
    auto* region = addMidiRegion(trackId, startBeat, 0.0);
    recordingRegions.push_back(region);
    return region;
}

void Arrangement::addRecordedEvent(const MidiEvent& event) {
    for (auto* region : recordingRegions) {
        region->events.push_back(event);
        double end = event.beatOffset + 0.1;
        if (end > region->lengthBeats)
            region->lengthBeats = end;
    }
}

void Arrangement::stopRecording() {
    for (auto* region : recordingRegions)
        region->sortEvents();
    recordingRegions.clear();
    // TODO: inject synthetic noteOffs for any unclosed notes at stop beat
}
