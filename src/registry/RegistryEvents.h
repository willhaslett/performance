#pragma once
#include <functional>
#include <string>
#include <vector>
#include <mutex>

namespace EntityType {
    constexpr const char* Plugin   = "plugin";
    constexpr const char* Preset   = "preset";
    constexpr const char* Song     = "song";
    constexpr const char* Track    = "track";
    constexpr const char* Bus      = "bus";
    constexpr const char* Effect   = "effect";
    constexpr const char* Send     = "send";
    constexpr const char* Action   = "action";
    constexpr const char* Binding  = "binding";
}

struct RegistryEvent {
    enum Action { Created, Updated, Deleted };
    Action action;
    std::string entityType;
    std::string entityId;
};

class RegistryEventBus {
public:
    using Listener = std::function<void(const RegistryEvent&)>;

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

    void emit(const RegistryEvent& event) {
        // Copy listeners under lock, call outside to avoid deadlock
        // if a listener triggers another registry operation
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
