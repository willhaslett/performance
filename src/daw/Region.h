#pragma once
#include <string>
#include <vector>
#include <algorithm>

// Region — a time-bounded container of events placed on a track.
// Base class for MidiRegion and future AudioRegion.
// Position and length are in beats (not seconds — tempo-relative).

struct Region {
    std::string id;
    std::string trackId;      // which track this region belongs to
    double startBeat = 0.0;   // position on the timeline
    double lengthBeats = 4.0; // duration in beats
    std::string name;

    double endBeat() const { return startBeat + lengthBeats; }

    enum class Type { Midi, Audio };
    virtual Type type() const = 0;
    virtual ~Region() = default;
};

// A single MIDI note event within a region.
// beatOffset is relative to the region's start (0.0 = first beat of region).
struct MidiNoteEvent {
    double beatOffset = 0.0;    // when the note starts (relative to region start)
    double durationBeats = 0.5; // how long the note lasts
    int noteNumber = 60;
    int velocity = 100;
    int channel = 1;
};

// MidiRegion — contains MIDI note events.
struct MidiRegion : public Region {
    std::vector<MidiNoteEvent> notes;

    Type type() const override { return Type::Midi; }

    // Sort notes by beat offset for efficient playback scanning
    void sortNotes() {
        std::sort(notes.begin(), notes.end(),
                  [](auto& a, auto& b) { return a.beatOffset < b.beatOffset; });
    }

    // Find notes that start within a beat range (for playback)
    // Returns indices into notes vector
    void notesInRange(double fromBeat, double toBeat,
                       std::vector<int>& outIndices) const {
        outIndices.clear();
        for (int i = 0; i < (int)notes.size(); ++i) {
            if (notes[i].beatOffset >= fromBeat && notes[i].beatOffset < toBeat)
                outIndices.push_back(i);
        }
    }
};

// AudioRegion — placeholder for future audio clip support.
struct AudioRegion : public Region {
    std::string filePath;  // path to audio file
    double fileStartBeat = 0.0;  // offset within the file

    Type type() const override { return Type::Audio; }
};
