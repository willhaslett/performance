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
    recordingRegion = nullptr;
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

        // Check if this region overlaps the scan range
        if (midi->endBeat() <= prevBeat || midi->startBeat >= currentBeat) continue;

        for (auto& note : midi->notes) {
            double noteAbsBeat = midi->startBeat + note.beatOffset;
            double noteEndBeat = noteAbsBeat + note.durationBeats;

            // Note-on: fires when we cross the note start
            if (noteAbsBeat >= prevBeat && noteAbsBeat < currentBeat) {
                callback(midi->trackId, note.noteNumber, note.velocity, note.channel, noteAbsBeat);
            }

            // Note-off: fires when we cross the note end
            if (noteEndBeat >= prevBeat && noteEndBeat < currentBeat) {
                callback(midi->trackId, note.noteNumber, 0, note.channel, noteEndBeat);
            }
        }
    }
}

MidiRegion* Arrangement::startRecording(const std::string& trackId, double startBeat) {
    auto* region = addMidiRegion(trackId, startBeat, 0.0);
    recordingRegion = region;
    return region;
}

void Arrangement::addRecordedNote(int noteNumber, int velocity, int channel, double beatOffset) {
    if (!recordingRegion) return;
    MidiNoteEvent event;
    event.beatOffset = beatOffset;
    event.durationBeats = 0.0;  // will be set on note-off
    event.noteNumber = noteNumber;
    event.velocity = velocity;
    event.channel = channel;
    recordingRegion->notes.push_back(event);

    // Extend region length
    double end = beatOffset + 0.5;  // minimum length
    if (end > recordingRegion->lengthBeats)
        recordingRegion->lengthBeats = end;
}

void Arrangement::stopRecording() {
    if (recordingRegion) {
        recordingRegion->sortNotes();
        recordingRegion = nullptr;
    }
}
