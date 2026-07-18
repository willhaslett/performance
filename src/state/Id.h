#pragma once
#include <functional>
#include <string>
#include <utility>

// Strongly-typed ID wrapper. Prevents mixing e.g. PluginId with PresetId at
// compile time while staying string-backed (compatible with UUIDs, JSON,
// SQLite TEXT columns, and the existing UUID machinery).
//
// Background: see docs/INCIDENT_2026-04-18_PERSISTENCE.md — the first-session
// data-loss incident was caused by a call site passing a plugin's display
// name where a preset UUID was expected, both typed as std::string. Typed
// IDs make that call a compile error instead of a silent FK violation at
// save time.
//
// Conventions:
//   - Construction from string is EXPLICIT; no implicit conversion either way.
//   - An empty Id (v.empty()) is the "not set" / "none" sentinel. Code that
//     previously wrote `if (!someId.empty())` keeps working.
//   - Conversion to std::string is via `.str()` (named, explicit). There's
//     deliberately no operator std::string or implicit string-contextual use.
//   - Hash + ordering specializations live in this header so typed IDs are
//     drop-in replaceable as keys in std::map / std::unordered_map.

template <class Tag>
struct Id {
    std::string v;

    Id() = default;
    explicit Id(std::string s) : v(std::move(s)) {}
    explicit Id(const char* s) : v(s ? s : "") {}

    const std::string& str() const noexcept { return v; }
    const char* c_str() const noexcept { return v.c_str(); }
    bool empty() const noexcept { return v.empty(); }
    void clear() noexcept { v.clear(); }

    friend bool operator==(const Id& a, const Id& b) noexcept { return a.v == b.v; }
    friend bool operator!=(const Id& a, const Id& b) noexcept { return a.v != b.v; }
    friend bool operator< (const Id& a, const Id& b) noexcept { return a.v <  b.v; }
};

using PluginId      = Id<struct PluginTag>;
using PresetId      = Id<struct PresetTag>;
using ActionId      = Id<struct ActionTag>;
using DeviceId      = Id<struct DeviceTag>;
using SongId        = Id<struct SongTag>;
using TrackId       = Id<struct TrackTag>;
using BusId         = Id<struct BusTag>;
using EffectId      = Id<struct EffectTag>;
using SendId        = Id<struct SendTag>;
using RegionId      = Id<struct RegionTag>;
using TakeId        = Id<struct TakeTag>;
using AssetId       = Id<struct AssetTag>;
using BindingId     = Id<struct BindingTag>;
using ActionEventId = Id<struct ActionEventTag>;
using EventId       = Id<struct EventTag>;       // unified id space for tempo/timesig/key events; action events keep ActionEventId

namespace std {
    template <class Tag>
    struct hash<Id<Tag>> {
        size_t operator()(const Id<Tag>& id) const noexcept {
            return std::hash<std::string>{}(id.v);
        }
    };
}
