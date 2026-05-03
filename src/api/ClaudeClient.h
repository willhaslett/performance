#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>
#include <functional>
#include <memory>
#include <mutex>

class LuaEngine;
class ChatHistoryStore;

class ClaudeClient : private juce::Thread {
public:
    struct ContentBlock {
        enum Type { Text, ToolUse, ToolResult };
        Type type;
        juce::String text;
        juce::String toolUseId;
        juce::String toolName;
        juce::String toolInput;   // JSON string
        juce::String toolOutput;
        bool isError = false;
    };

    struct Message {
        juce::String role;  // "user" or "assistant"
        std::vector<ContentBlock> content;
    };

    struct Listener {
        virtual ~Listener() = default;
        virtual void onAssistantText(const juce::String& text) = 0;
        virtual void onToolUse(const juce::String& name, const juce::String& code,
                              const juce::String& result, bool isError) = 0;
        virtual void onError(const juce::String& error) = 0;
        virtual void onBusyChanged(bool busy) = 0;
        // Called after clearHistory() so the chat UI can drop its
        // existing bubbles. Default no-op for listeners that don't care.
        virtual void onHistoryCleared() {}
    };

    ClaudeClient(LuaEngine& lua);
    ~ClaudeClient() override;

    void setSystemPrompt(const juce::String& prompt);
    void sendMessage(const juce::String& userText);
    void setListener(Listener* l) { listener = l; }
    bool isBusy() const { return isThreadRunning(); }

    // Wire up persistence. Pass nullptr to disable (in-memory only).
    // On set, the store is queried for any prior conversation and the
    // result becomes the in-memory history. After every completed
    // round-trip, the store's save() is called with the current history.
    // The store is opaque — see api/ChatHistoryStore.h for the contract.
    void setHistoryStore(std::unique_ptr<ChatHistoryStore> store);

    // Snapshot of the current in-memory conversation. Used by ChatView
    // to render bubbles for messages restored from a previous session.
    const std::vector<Message>& history() const { return conversationHistory; }

    // Empty the in-memory conversation and the backing store. Triggers
    // the listener's onHistoryCleared so the UI can drop its bubbles.
    void clearHistory();

private:
    void run() override;

    LuaEngine& lua;
    Listener* listener = nullptr;

    juce::String systemPrompt;

    std::vector<Message> conversationHistory;
    juce::String pendingUserText;
    std::mutex sendMutex;
    std::unique_ptr<ChatHistoryStore> historyStore;

    // Trim conversationHistory in-place to at most kMaxContextMessages,
    // dropping oldest first but keeping turn integrity (never split a
    // user/assistant pair, never orphan a tool_use without its matching
    // tool_result — Anthropic's API errors on either).
    static constexpr size_t kMaxContextMessages = 40;
    void trimHistoryForContext();
    void persistHistory();

    juce::String buildRequestJson();
    // Streams an SSE response from the proxy and accumulates it into a
    // Message. Returns false if the request failed (handler will have
    // already called notifyError). On success, message is fully populated.
    bool streamResponse(juce::InputStream& stream, Message& outMessage);
    juce::String executeLua(const juce::String& code);

    void notifyText(const juce::String& text);
    void notifyToolUse(const juce::String& name, const juce::String& code,
                       const juce::String& result, bool isError);
    void notifyError(const juce::String& error);
    void notifyBusy(bool busy);
};
