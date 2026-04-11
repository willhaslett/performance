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
    // Fires callback for every MIDI event in [prevBeat, currentBeat).
    // Events are raw — noteOn, noteOff, CC, aftertouch, pitch bend, etc.
    using MidiEventCallback = std::function<void(const std::string& trackId,
                                                  const MidiEvent& event,
                                                  double absoluteBeat)>;
    void scanMidiEvents(double prevBeat, double currentBeat, MidiEventCallback callback) const;

    // --- Recording (supports multiple armed tracks simultaneously) ---
    // Start recording into a new region on the given track at the given beat.
    // Call once per armed track. All active recording regions receive events.
    MidiRegion* startRecording(const std::string& trackId, double startBeat);
    void addRecordedEvent(const MidiEvent& event);
    void stopRecording();
    bool isRecording() const { return !recordingRegions.empty(); }

private:
    std::vector<std::unique_ptr<Region>> regions;
    std::vector<MidiRegion*> recordingRegions;  // active recording targets (non-owning)

    static std::string generateId();
};
