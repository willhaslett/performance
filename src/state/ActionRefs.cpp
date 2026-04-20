#include "state/ActionRefs.h"
#include "api/StateAPI.h"
#include <juce_core/juce_core.h>
#include <algorithm>

namespace ActionRefs {

namespace {

bool paramIsRef(const ActionInfo& a, int i) {
    if (i < 0 || i >= (int)a.params.size()) return false;
    auto t = a.params[(size_t)i].type;
    return t == ParamType::ChannelRef || t == ParamType::PresetRef;
}

// For each ref-typed arg of an action instance, invoke visit(refString, type).
template <class F>
void forEachRefArg(const ActionInfo& a, const std::string& argsJson, F&& visit) {
    auto args = juce::JSON::parse(juce::String(argsJson));
    auto* arr = args.getArray();
    if (!arr) return;
    int n = std::min((int)arr->size(), (int)a.params.size());
    for (int i = 0; i < n; ++i) {
        if (!paramIsRef(a, i)) continue;
        visit((*arr)[i].toString().toStdString(), a.params[(size_t)i].type);
    }
}

}  // namespace

DependentCount countDependents(const StateAPI& state, const std::string& entityId) {
    DependentCount out;
    if (entityId.empty()) return out;

    auto* song = state.currentSong();
    if (song) {
        // Action events on the song's action track(s).
        for (auto& track : song->tracks) {
            if (track.sourceType != TrackSourceType::Action) continue;
            for (auto& ae : track.actionData) {
                auto* action = state.findActionById(ae.actionId);
                if (!action) continue;
                forEachRefArg(*action, ae.argsJson,
                    [&](const std::string& ref, ParamType) {
                        if (ref == entityId) ++out.actionEvents;
                    });
            }
        }
        // Song-scoped bindings.
        for (auto& b : song->bindings) {
            auto* action = state.findActionById(b.actionId);
            if (!action) continue;
            forEachRefArg(*action, b.args,
                [&](const std::string& ref, ParamType) {
                    if (ref == entityId) ++out.bindings;
                });
        }
    }
    // Global bindings.
    for (auto& b : state.globalBindings()) {
        auto* action = state.findActionById(b.actionId);
        if (!action) continue;
        forEachRefArg(*action, b.args,
            [&](const std::string& ref, ParamType) {
                if (ref == entityId) ++out.bindings;
            });
    }
    return out;
}

void removeDependents(StateAPI& state, const std::string& entityId) {
    if (entityId.empty()) return;

    auto* song = state.currentSong();
    if (song) {
        // Action events: collect event ids in one pass, remove in a second.
        std::vector<std::pair<TrackId, ActionEventId>> eventsToRemove;
        for (auto& track : song->tracks) {
            if (track.sourceType != TrackSourceType::Action) continue;
            for (auto& ae : track.actionData) {
                auto* action = state.findActionById(ae.actionId);
                if (!action) continue;
                forEachRefArg(*action, ae.argsJson,
                    [&](const std::string& ref, ParamType) {
                        if (ref == entityId) eventsToRemove.push_back({ track.id, ae.id });
                    });
            }
        }
        for (auto& [tid, eid] : eventsToRemove) {
            auto* t = state.findTrack(tid);
            if (!t) continue;
            auto& data = t->actionData;
            data.erase(std::remove_if(data.begin(), data.end(),
                [&](const ActionEventData& e) { return e.id == eid; }), data.end());
        }
        if (!eventsToRemove.empty()) state.markDirty();
    }

    // Bindings (song-scoped + global): collect ids, then removeBinding.
    std::vector<BindingId> bindingsToDelete;
    auto scan = [&](const std::vector<BindingState>& list) {
        for (auto& b : list) {
            auto* action = state.findActionById(b.actionId);
            if (!action) continue;
            forEachRefArg(*action, b.args,
                [&](const std::string& ref, ParamType) {
                    if (ref == entityId) bindingsToDelete.push_back(b.id);
                });
        }
    };
    if (song) scan(song->bindings);
    scan(state.globalBindings());

    for (auto& id : bindingsToDelete) state.removeBinding(id);
}

std::vector<StaleRef> findStaleRefs(StateAPI& state) {
    std::vector<StaleRef> out;

    auto refResolves = [&](const std::string& ref, ParamType t) {
        if (ref.empty()) return false;
        if (t == ParamType::ChannelRef) {
            if (ref == "Main") return true;
            if (state.findTrack(TrackId{ref})) return true;
            if (state.findBus(BusId{ref})) return true;
            return false;
        }
        if (t == ParamType::PresetRef) {
            return state.findPresetById(PresetId{ref}) != nullptr;
        }
        return true;
    };

    auto* song = state.currentSong();

    auto scanBindings = [&](const std::vector<BindingState>& list) {
        for (auto& b : list) {
            auto* action = state.findActionById(b.actionId);
            if (!action) continue;
            bool stale = false;
            forEachRefArg(*action, b.args,
                [&](const std::string& ref, ParamType t) {
                    if (!refResolves(ref, t)) stale = true;
                });
            if (stale) {
                auto bid = b.id;
                juce::String s;
                s << juce::String(b.description.empty() ? b.controlType : b.description)
                  << " → " << juce::String(action->label.empty() ? action->name : action->label)
                  << " → (missing reference)";
                out.push_back({ s.toStdString(),
                                [&state, bid]() { state.removeBinding(bid); } });
            }
        }
    };
    if (song) scanBindings(song->bindings);
    scanBindings(state.globalBindings());

    if (song) {
        for (auto& track : song->tracks) {
            if (track.sourceType != TrackSourceType::Action) continue;
            for (auto& ae : track.actionData) {
                auto* action = state.findActionById(ae.actionId);
                if (!action) continue;
                bool stale = false;
                forEachRefArg(*action, ae.argsJson,
                    [&](const std::string& ref, ParamType t) {
                        if (!refResolves(ref, t)) stale = true;
                    });
                if (stale) {
                    auto tid = track.id;
                    auto eid = ae.id;
                    juce::String s;
                    s << "Action event @ beat " << juce::String(ae.beat, 2)
                      << " → " << juce::String(action->label.empty() ? action->name : action->label)
                      << " → (missing reference)";
                    out.push_back({ s.toStdString(),
                                    [&state, tid, eid]() {
                                        auto* t = state.findTrack(tid);
                                        if (!t) return;
                                        auto& data = t->actionData;
                                        data.erase(std::remove_if(data.begin(), data.end(),
                                            [&](const ActionEventData& e) { return e.id == eid; }),
                                            data.end());
                                        state.markDirty();
                                    } });
                }
            }
        }
    }

    return out;
}

}  // namespace ActionRefs
