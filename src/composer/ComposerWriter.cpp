#include "composer/ComposerWriter.h"
#include "api/StateAPI.h"
#include "api/PerformanceCoordinator.h"
#include "daw/Arrangement.h"
#include "state/StateModel.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

#include <algorithm>
#include <set>

namespace {

// Clamp a float velocity in [0,1] to a MIDI value in [1,127].
// (Matches parse_velocity() in dawai/compiler.py which floors at 1.)
int toMidiVelocity(float v) {
    int out = static_cast<int>(std::round(v * 127.0f));
    return std::max(1, std::min(127, out));
}

// Resolve every distinct trackName in `output` to a TrackId via the
// state. Returns false on first unknown name; fills `err` with the
// offender. On success, `resolved` maps each name in insertion order.
bool resolveTrackIds(const ComposerOutput& output,
                      StateAPI& state,
                      std::vector<std::pair<std::string, TrackId>>& resolved,
                      std::string& err) {
    std::set<std::string> seen;
    for (auto& n : output.notes) {
        if (seen.count(n.trackName)) continue;
        seen.insert(n.trackName);
        auto id = state.findTrackIdByName(n.trackName);
        if (id.empty()) {
            err = "composer: no track named '" + n.trackName + "' in current project";
            return false;
        }
        resolved.emplace_back(n.trackName, id);
    }
    return true;
}

}  // namespace

bool ComposerWriter::apply(const ComposerOutput& output,
                            double startBeat,
                            std::string& err) {
    if (output.notes.empty()) {
        err = "composer: nothing to write (output has no notes)";
        return false;
    }
    if (output.lengthBeats <= 0.0) {
        err = "composer: output has non-positive lengthBeats";
        return false;
    }

    auto& state = coord.state();
    auto& arrangement = coord.arrangement();

    // Map trackName → TrackId up front so a bad name fails before we
    // write any regions.
    std::vector<std::pair<std::string, TrackId>> resolved;
    if (!resolveTrackIds(output, state, resolved, err)) return false;

    // Create one region per target track at the requested startBeat.
    std::vector<std::pair<std::string, RegionState*>> regions;
    regions.reserve(resolved.size());
    for (auto& [name, tid] : resolved) {
        auto* region = arrangement.addMidiRegion(tid, startBeat, output.lengthBeats);
        if (!region) {
            err = "composer: failed to create region on track '" + name + "'";
            return false;
        }
        regions.emplace_back(name, region);
    }

    // Populate each region's active take with note-on / note-off
    // events. Direct state mutation mirrors Arrangement's recording
    // path; the Track::Updated event we emit below lets autosave
    // and any engine-side listeners pick up the change.
    for (auto& note : output.notes) {
        RegionState* target = nullptr;
        for (auto& [name, region] : regions) {
            if (name == note.trackName) { target = region; break; }
        }
        if (!target || !target->activeTake()) continue;

        auto* take = target->activeTake();
        int midiVel = toMidiVelocity(note.velocity);

        MidiEventState on;
        on.beatOffset = note.startBeat;
        on.status = 0x90;
        on.channel = 1;
        on.data1 = note.pitch;
        on.data2 = midiVel;
        take->events.push_back(on);

        MidiEventState off;
        off.beatOffset = note.startBeat + note.durationBeats;
        off.status = 0x80;
        off.channel = 1;
        off.data1 = note.pitch;
        off.data2 = 0;
        take->events.push_back(off);
    }

    // Sort events by beatOffset so playback / renderers can walk them
    // linearly. Note-offs at the same beat as note-ons come first
    // (0x80 < 0x90 numerically), which is what playback expects for
    // back-to-back identical-pitch notes.
    for (auto& [_n, region] : regions) {
        auto* take = region->activeTake();
        if (!take) continue;
        std::sort(take->events.begin(), take->events.end(),
            [](const MidiEventState& a, const MidiEventState& b) {
                if (a.beatOffset != b.beatOffset) return a.beatOffset < b.beatOffset;
                return a.status < b.status;
            });
    }

    // Notify listeners — autosave + engine sync pick up region changes
    // via Track::Updated (regions don't have their own event entity).
    for (auto& [_n, region] : regions) {
        (void) region;
    }
    for (auto& [_n, tid] : resolved) {
        StateEvent ev;
        ev.action = StateEvent::Updated;
        ev.entity = StateEvent::Track;
        ev.entityId = tid.str();
        state.events().emit(ev);
    }

    perfLog("[Composer] wrote %d note(s) across %d track(s) at beat %.2f (length %.2f)\n",
            (int) output.notes.size(),
            (int) regions.size(),
            startBeat,
            output.lengthBeats);

    return true;
}
