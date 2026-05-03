#pragma once
#include "api/ClaudeClient.h"
#include <vector>

// Persistence interface for chat history. The store is opaque to its
// consumer (ClaudeClient) — load returns whatever was saved last (or
// an empty vector on first run / error), save replaces the whole
// store, clear empties it. Three methods, no other contract.
//
// The v1 implementation is JsonFileChatHistoryStore (one JSON file
// rewritten per round-trip). Future implementations will likely back
// onto SQLite (per-song threads, full-text search) or a remote service
// (cross-device sync). All of those plug in through the same interface
// without touching ClaudeClient.
//
// The store always persists everything it's given. Trimming for token
// cost or context-window management is ClaudeClient's concern — it
// knows about Anthropic's tool_use/tool_result ID linkage and the
// per-install token caps; the store doesn't and shouldn't.
class ChatHistoryStore {
public:
    virtual ~ChatHistoryStore() = default;

    // Load whatever was last saved. Returns an empty vector on first
    // run, missing file, parse error, or any other "no history
    // available" condition — callers should treat empty as the normal
    // first-launch state.
    virtual std::vector<ClaudeClient::Message> load() = 0;

    // Replace the store's contents with the given message list. Called
    // after each round-trip completes. Should be atomic (write-then-
    // rename or equivalent) so a crash mid-save doesn't truncate the
    // existing history.
    virtual void save(const std::vector<ClaudeClient::Message>& messages) = 0;

    // Empty the store — for the user-facing "Clear chat" affordance.
    virtual void clear() = 0;
};
