#pragma once

#include <string>

struct RegionState;
struct SongState;
struct TrackState;

// Bridges between project-state (RegionState's MIDI events) and ABC
// notation text. Composes ABCParser / ABCWriter with project metadata
// (tempo, time signature, key, voice naming) so callers — primarily the
// Lua-exposed CRUD layer — see one verb per direction.
namespace RegionContent {

// Convert a region's active take to a self-contained ABC document.
// Notes are emitted region-local (region start = beat 0). M: and Q:
// come from the song's first time-sig / tempo events. Key defaults to
// "none" until Phase 3 adds key events.
//
// The voice is named after the parent track. isDrums = true if the
// track name (case-insensitive) contains "drum"; emits a %%MIDI drummap
// directive + drum-letter macros.
std::string regionToABC(const RegionState& region,
                         const TrackState& track,
                         const SongState& song);

// Replace a region's active take's MIDI events with the events parsed
// from the ABC string. Region length is updated to match the parsed
// content's lengthBeats. Returns false on parse error and fills `err`;
// region is left untouched on failure.
bool abcToRegion(const std::string& abc,
                  RegionState& region,
                  std::string& err);

// Render all regions on a track as one ABC document, single voice,
// each region marked with `P:B<beat>` so the LLM can refer back via
// getRegion(track, beat). Notes are positioned at piece-absolute
// beats (= each region's startBeat + per-event beatOffset). Header
// (M:, Q:) comes from the song.
std::string trackToABC(const TrackState& track, const SongState& song);

// Render the whole project as one ABC document, multi-voice (one V:
// per track), each voice's regions marked with `P:B<beat>`. Read-only
// — there is no setProject; mutation goes through region/track verbs.
std::string projectToABC(const SongState& song);

}  // namespace RegionContent
