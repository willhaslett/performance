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
    // Set the backing tracks and action events (called on song load/switch)
    void setTracks(std::vector<TrackState>* tracks) { songTracks = tracks; }
    // Action events are scanned from Action track regions (no separate pointer needed)

    // --- Region management ---
    RegionState* addMidiRegion(const TrackId& trackId, double startBeat, double lengthBeats);
    void removeRegion(const RegionId& regionId);
    void moveRegion(const RegionId& regionId, const TrackId& newTrackId, double newStartBeat);
    RegionState* duplicateRegion(const RegionId& regionId, const TrackId& targetTrackId, double startBeat);

    // Split a region at a beat position. Returns the new right-side region (or nullptr).
    // splitNotes: if true, notes crossing the split get noteOff in left + noteOn in right.
    //             if false, crossing notes are trimmed (noteOff at split, absent from right).
    RegionState* splitRegion(const RegionId& regionId, double splitBeat, bool splitNotes);

    // --- Queries ---
    std::vector<RegionState*> allRegions() const;
    std::vector<RegionState*> regionsForTrack(const TrackId& trackId) const;
    RegionState* findRegion(const RegionId& regionId) const;

    // --- Playback scanning (reads from active take) ---
    using EventCallback = std::function<void(const TrackId& trackId,
                                              const MidiEventState& event,
                                              double absoluteBeat)>;
    void scanMidiEvents(double prevBeat, double currentBeat, EventCallback callback) const;

    // --- Looper mode (see docs/LIVE_LOOPING.md) ---
    // When looper mode is active, scanMidiEvents dispatches to a
    // modular-playback path that walks each track's loop pool
    // (track.loops) instead of the arrangement pool (track.regions),
    // wrapping each loop's content at (cyclePos mod lengthBeats).
    // Callers (Coordinator) set this via updateLooperMode; the scanner
    // owns the dispatch so engine callers don't need to know.
    void updateLooperMode(bool active, double cycleLengthBeats) {
        looperModeActive = active;
        looperCycleLengthBeats = cycleLengthBeats;
    }
    bool isLooperModeActive() const { return looperModeActive; }

    // --- Action event scanning ---
    using ActionCallback = std::function<void(const SongState::ActionEvent& event)>;
    void scanActionEvents(double prevBeat, double currentBeat, ActionCallback callback) const;

    // --- Recording (creates/appends to a take in the recording region) ---
    RegionState* startRecording(const TrackId& trackId, double startBeat);
    void addRecordedEvent(const MidiEventState& event);
    void stopRecording();
    bool isRecording() const { return !recordingTakes.empty(); }

    // --- Loop recording (Looper mode) ---
    // Creates (if absent) the track's single loop region at startBeat=0
    // and adds a new take to it. Routes incoming recorded events into
    // the new take via the shared recordingTakes list; lengthBeats is
    // updated at stopLoopRecording.
    //
    // `stopLoopRecording` sets the loop region's lengthBeats from the
    // caller-supplied value (typically punch-out beat − punch-in beat
    // in the performance timeline) and promotes the new take to active.
    // Previous takes remain on disk; performer can swap back to them.
    RegionState* startLoopRecording(const TrackId& trackId);
    void stopLoopRecording(const TrackId& trackId, double lengthBeats);

    // Drop the most recently-started loop take (and its loop region,
    // if we just created it via startLoopRecording and it's now
    // empty). Used to abort a recording that never completed a cycle.
    void discardLastLoopRecording(const TrackId& trackId);

    // --- Derived views ---
    static std::vector<NoteView> buildNoteList(const TakeState& take, double regionLength);
    // Convenience: build from region's active take
    static std::vector<NoteView> buildNoteList(const RegionState& region);

private:
    std::vector<TrackState>* songTracks = nullptr;
    std::vector<TakeState*> recordingTakes;  // active recording targets

    bool looperModeActive = false;
    double looperCycleLengthBeats = 0.0;

    // Internal dispatch helpers for scanMidiEvents. The caller-facing
    // scanMidiEvents picks one based on the looper-mode flag.
    void scanArrangementEvents(double prevBeat, double currentBeat,
                                EventCallback callback) const;
    void scanLoopEvents(double prevBeat, double currentBeat,
                        EventCallback callback) const;

    static std::string generateId();
};
