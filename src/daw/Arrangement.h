#pragma once
#include "daw/Region.h"
#include <memory>
#include <vector>
#include <string>
#include <functional>

// Arrangement — all regions for a song, plus playback scanning.
// Owns the regions. Provides queries for the sequencer and UI.

class Arrangement {
public:
    // --- Region management ---
    MidiRegion* addMidiRegion(const std::string& trackId, double startBeat, double lengthBeats);
    void removeRegion(const std::string& regionId);
    void clearTrack(const std::string& trackId);
    void clearAll();

    // --- Queries ---
    const std::vector<std::unique_ptr<Region>>& allRegions() const { return regions; }
    std::vector<Region*> regionsForTrack(const std::string& trackId) const;
    Region* findRegion(const std::string& regionId) const;

    // --- Playback scanning ---
    // Get MIDI events that should fire between prevBeat and currentBeat.
    // For each note-on, calls the callback with (trackId, noteNumber, velocity, channel).
    // For note-offs, velocity is 0.
    using MidiEventCallback = std::function<void(const std::string& trackId,
                                                  int noteNumber, int velocity, int channel,
                                                  double eventBeat)>;
    void scanMidiEvents(double prevBeat, double currentBeat, MidiEventCallback callback) const;

    // --- Recording ---
    // Start recording into a new region on the given track at the given beat.
    // Returns the region being recorded into.
    MidiRegion* startRecording(const std::string& trackId, double startBeat);
    void addRecordedNote(int noteNumber, int velocity, int channel, double beatOffset);
    void stopRecording();
    bool isRecording() const { return recordingRegion != nullptr; }

private:
    std::vector<std::unique_ptr<Region>> regions;
    MidiRegion* recordingRegion = nullptr;

    static std::string generateId();
};
