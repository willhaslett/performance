#pragma once
#include "state/StateModel.h"
#include <vector>
#include <string>
#include <functional>
#include <map>
#include <algorithm>

// Arrangement — view over RegionState data in the current song's tracks.
// Provides playback scanning and recording API.
// Does not own the data — regions live in TrackState.regions.

// Derived note view for display (computed from raw events, not stored)
struct NoteView {
    double beatOffset = 0.0;
    double durationBeats = 0.5;
    int noteNumber = 60;
    int velocity = 100;
    int channel = 1;
};

class Arrangement {
public:
    // Set the backing tracks (called on song load/switch)
    void setTracks(std::vector<TrackState>* tracks) { songTracks = tracks; }

    // --- Region management ---
    RegionState* addMidiRegion(const std::string& trackId, double startBeat, double lengthBeats);
    void removeRegion(const std::string& regionId);
    void moveRegion(const std::string& regionId, const std::string& newTrackId, double newStartBeat);
    RegionState* duplicateRegion(const std::string& regionId, const std::string& targetTrackId, double startBeat);

    // --- Queries ---
    std::vector<RegionState*> allRegions() const;
    std::vector<RegionState*> regionsForTrack(const std::string& trackId) const;
    RegionState* findRegion(const std::string& regionId) const;

    // --- Playback scanning (reads from active take) ---
    using EventCallback = std::function<void(const std::string& trackId,
                                              const MidiEventState& event,
                                              double absoluteBeat)>;
    void scanMidiEvents(double prevBeat, double currentBeat, EventCallback callback) const;

    // --- Recording (creates/appends to a take in the recording region) ---
    RegionState* startRecording(const std::string& trackId, double startBeat);
    void addRecordedEvent(const MidiEventState& event);
    void stopRecording();
    bool isRecording() const { return !recordingTakes.empty(); }

    // --- Derived views ---
    static std::vector<NoteView> buildNoteList(const TakeState& take, double regionLength);
    // Convenience: build from region's active take
    static std::vector<NoteView> buildNoteList(const RegionState& region);

private:
    std::vector<TrackState>* songTracks = nullptr;
    std::vector<TakeState*> recordingTakes;  // active recording targets

    static std::string generateId();
};
