#pragma once
#include <string>
#include <vector>
#include <algorithm>
#include <map>

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

// A single MIDI event within a region. All MIDI message types.
// beatOffset is relative to the region's start (0.0 = first beat of region).
// This is the source of truth. Notes are noteOn + noteOff pairs in the stream.
struct MidiEvent {
    double beatOffset = 0.0;
    int status = 0x90;   // MIDI status byte (0x90=noteOn, 0x80=noteOff, 0xB0=CC, etc.)
    int channel = 1;
    int data1 = 60;      // note number, CC number, etc.
    int data2 = 100;     // velocity, CC value, pressure, etc.

    bool isNoteOn() const  { return (status & 0xF0) == 0x90 && data2 > 0; }
    bool isNoteOff() const { return (status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && data2 == 0); }
};

// Derived view of a note (noteOn + noteOff paired). Computed, not stored.
struct NoteView {
    double beatOffset = 0.0;
    double durationBeats = 0.5;
    int noteNumber = 60;
    int velocity = 100;
    int channel = 1;
};

// MidiRegion — contains a stream of raw MIDI events (the source of truth).
struct MidiRegion : public Region {
    std::vector<MidiEvent> events;

    Type type() const override { return Type::Midi; }

    void sortEvents() {
        std::sort(events.begin(), events.end(),
                  [](auto& a, auto& b) { return a.beatOffset < b.beatOffset; });
    }

    // Derive note list (noteOn/noteOff pairs) for display and editing.
    std::vector<NoteView> buildNoteList() const {
        std::vector<NoteView> notes;
        // Track openNotes notes: {noteNumber, channel} → index in notes
        std::map<std::pair<int,int>, int> openNotes;
        for (auto& e : events) {
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
        // Close any unclosed notes at region end
        for (auto& [key, idx] : openNotes) {
            if (notes[idx].durationBeats <= 0.0)
                notes[idx].durationBeats = lengthBeats - notes[idx].beatOffset;
        }
        return notes;
    }
};

// AudioRegion — placeholder for future audio clip support.
struct AudioRegion : public Region {
    std::string filePath;  // path to audio file
    double fileStartBeat = 0.0;  // offset within the file

    Type type() const override { return Type::Audio; }
};
