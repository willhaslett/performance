#include "composer/RegionContent.h"

#include "composer/ABCParser.h"
#include "composer/ABCWriter.h"
#include "composer/ComposerOutput.h"
#include "state/StateModel.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_map>

namespace {

bool nameHasDrum(const std::string& s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return lower.find("drum") != std::string::npos;
}

// Build a list of ABCWriteInput::Note from a take's note-on/note-off events.
// We pair note-ons with the next matching note-off on the same channel+pitch.
// Unmatched note-ons are skipped.
std::vector<ABCWriteInput::Note> notesFromEvents(const std::vector<MidiEventState>& events) {
    std::vector<ABCWriteInput::Note> out;
    // Sorted copy by beat, then status (offs before ons at same beat).
    std::vector<MidiEventState> sorted = events;
    std::sort(sorted.begin(), sorted.end(),
        [](const MidiEventState& a, const MidiEventState& b) {
            if (a.beatOffset != b.beatOffset) return a.beatOffset < b.beatOffset;
            return a.status < b.status;
        });

    // Track open note-ons indexed by (channel, pitch).
    struct Open { double startBeat; int velocity; size_t noteIdx; };
    std::unordered_map<int, Open> openByKey;   // key = channel * 128 + pitch

    for (auto& e : sorted) {
        int statusType = e.status & 0xF0;
        int key = (e.channel & 0x0F) * 128 + (e.data1 & 0x7F);
        if (statusType == 0x90 && e.data2 > 0) {
            ABCWriteInput::Note n;
            n.startBeat     = e.beatOffset;
            n.durationBeats = 0.0;        // filled in when we see the noteOff
            n.pitch         = e.data1;
            n.velocity      = e.data2;
            out.push_back(n);
            openByKey[key] = {e.beatOffset, e.data2, out.size() - 1};
        } else if (statusType == 0x80
                   || (statusType == 0x90 && e.data2 == 0)) {
            auto it = openByKey.find(key);
            if (it != openByKey.end()) {
                out[it->second.noteIdx].durationBeats = e.beatOffset - it->second.startBeat;
                openByKey.erase(it);
            }
        }
    }

    // Drop any zero-duration notes (orphan note-ons with no off — rare).
    out.erase(std::remove_if(out.begin(), out.end(),
        [](const ABCWriteInput::Note& n) { return n.durationBeats <= 0.0; }),
        out.end());

    return out;
}

void eventsFromNotes(const std::vector<ComposerOutput::Note>& notes,
                      std::vector<MidiEventState>& outEvents) {
    outEvents.clear();
    outEvents.reserve(notes.size() * 2);
    for (auto& n : notes) {
        int vel = static_cast<int>(std::lround(n.velocity * 127.0f));
        vel = std::max(1, std::min(127, vel));

        MidiEventState on;
        on.beatOffset = n.startBeat;
        on.status     = 0x90;
        on.channel    = 1;
        on.data1      = n.pitch;
        on.data2      = vel;
        outEvents.push_back(on);

        MidiEventState off;
        off.beatOffset = n.startBeat + n.durationBeats;
        off.status     = 0x80;
        off.channel    = 1;
        off.data1      = n.pitch;
        off.data2      = 0;
        outEvents.push_back(off);
    }
    std::sort(outEvents.begin(), outEvents.end(),
        [](const MidiEventState& a, const MidiEventState& b) {
            if (a.beatOffset != b.beatOffset) return a.beatOffset < b.beatOffset;
            return a.status < b.status;
        });
}

}  // namespace

std::string RegionContent::regionToABC(const RegionState& region,
                                         const TrackState& track,
                                         const SongState& song) {
    ABCWriteInput in;
    in.title             = region.name.empty() ? track.name : region.name;
    in.tempo             = song.tempoEvents.empty() ? 120.0 : song.tempoEvents.front().bpm;
    if (!song.timeSigEvents.empty()) {
        in.timeSignatureNum = song.timeSigEvents.front().numerator;
        in.timeSignatureDen = song.timeSigEvents.front().denominator;
    }
    in.key = "none";  // keyEvents added in a later step
    in.lengthBeats       = region.lengthBeats;

    ABCWriteInput::Voice v;
    v.name    = track.name;
    v.isDrums = nameHasDrum(track.name);

    const TakeState* take = region.activeTake();
    if (take) v.notes = notesFromEvents(take->events);

    in.voices.push_back(v);

    ABCWriter w;
    return w.write(in);
}

bool RegionContent::abcToRegion(const std::string& abc,
                                  RegionState& region,
                                  std::string& err) {
    ABCParser p;
    ComposerOutput out;
    if (!p.parse(juce::String(abc), out, err)) return false;

    TakeState* take = region.activeTake();
    if (!take) {
        err = "region has no active take to write into";
        return false;
    }

    eventsFromNotes(out.notes, take->events);
    if (out.lengthBeats > 0.0) region.lengthBeats = out.lengthBeats;
    return true;
}

namespace {

// Build a Voice with one Segment per region, notes shifted to
// piece-absolute beats (region.startBeat + event.beatOffset).
ABCWriteInput::Voice buildVoiceForTrack(const TrackState& track) {
    ABCWriteInput::Voice v;
    v.name    = track.name;
    v.isDrums = nameHasDrum(track.name);

    // Sort regions by startBeat so segment markers come out in order.
    std::vector<const RegionState*> sorted;
    sorted.reserve(track.regions.size());
    for (auto& r : track.regions) sorted.push_back(&r);
    std::sort(sorted.begin(), sorted.end(),
        [](const RegionState* a, const RegionState* b) {
            return a->startBeat < b->startBeat;
        });

    for (auto* region : sorted) {
        const TakeState* take = region->activeTake();
        if (!take) continue;
        ABCWriteInput::Segment seg;
        // Label format: "B<beat>" — same beat the LLM passes to getRegion.
        seg.label = "B" + std::to_string(static_cast<long long>(std::llround(region->startBeat)));
        auto noteList = notesFromEvents(take->events);
        for (auto& n : noteList) {
            n.startBeat += region->startBeat;       // shift to piece-absolute
            seg.notes.push_back(n);
        }
        v.segments.push_back(std::move(seg));
    }
    return v;
}

double trackLengthBeats(const TrackState& track) {
    double len = 0.0;
    for (auto& r : track.regions) {
        len = std::max(len, r.startBeat + r.lengthBeats);
    }
    return len;
}

}  // namespace

std::string RegionContent::trackToABC(const TrackState& track, const SongState& song) {
    ABCWriteInput in;
    in.title = track.name;
    in.tempo = song.tempoEvents.empty() ? 120.0 : song.tempoEvents.front().bpm;
    if (!song.timeSigEvents.empty()) {
        in.timeSignatureNum = song.timeSigEvents.front().numerator;
        in.timeSignatureDen = song.timeSigEvents.front().denominator;
    }
    in.key         = "none";  // keyEvents added in a later step
    in.lengthBeats = trackLengthBeats(track);
    in.voices.push_back(buildVoiceForTrack(track));

    ABCWriter w;
    return w.write(in);
}

std::string RegionContent::projectToABC(const SongState& song) {
    ABCWriteInput in;
    in.title = song.name;
    in.tempo = song.tempoEvents.empty() ? 120.0 : song.tempoEvents.front().bpm;
    if (!song.timeSigEvents.empty()) {
        in.timeSignatureNum = song.timeSigEvents.front().numerator;
        in.timeSignatureDen = song.timeSigEvents.front().denominator;
    }
    in.key = "none";  // keyEvents added in a later step

    double maxLen = 0.0;
    for (auto& t : song.tracks) {
        if (t.sourceType != TrackSourceType::Instrument) continue;
        in.voices.push_back(buildVoiceForTrack(t));
        maxLen = std::max(maxLen, trackLengthBeats(t));
    }
    in.lengthBeats = maxLen;

    ABCWriter w;
    return w.write(in);
}
