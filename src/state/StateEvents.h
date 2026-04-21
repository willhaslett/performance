#pragma once
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct StateEvent {
    enum Action { Created, Updated, Deleted };
    enum Entity { Song, Track, Bus, Effect, Send, Binding, Config, Plugin, Preset, Selection, Device,
                  // AppState-level changes (currentMode, and future app-level state).
                  // entityId is unused for App events.
                  App,
                  // Per-song singular focus pointer — the "track I'm playing into."
                  // Kept distinct from Selection (plural, for grouped UI actions).
                  // See docs/LIVE_INPUT_AND_FOCUS.md. entityId is the focused track id
                  // (may be empty, meaning no focus).
                  Focus };

    Action action;
    Entity entity;
    std::string entityId;
    std::string parentId;  // for nested entities (effect→track, send→track)
};

class StateEventBus {
public:
    using Listener = std::function<void(const StateEvent&)>;

    int subscribe(Listener listener) {
        std::lock_guard<std::mutex> lock(mutex);
        int id = nextId++;
        listeners.push_back({ id, std::move(listener) });
        return id;
    }

    void unsubscribe(int id) {
        std::lock_guard<std::mutex> lock(mutex);
        listeners.erase(
            std::remove_if(listeners.begin(), listeners.end(),
                           [id](auto& entry) { return entry.id == id; }),
            listeners.end());
    }

    void emit(const StateEvent& event) {
        std::vector<Listener> snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex);
            snapshot.reserve(listeners.size());
            for (auto& entry : listeners)
                snapshot.push_back(entry.listener);
        }
        for (auto& fn : snapshot)
            fn(event);
    }

private:
    struct Entry {
        int id;
        Listener listener;
    };
    std::vector<Entry> listeners;
    std::mutex mutex;
    int nextId = 1;
};
