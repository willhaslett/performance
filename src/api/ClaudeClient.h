#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <vector>
#include <functional>
#include <mutex>

class LuaEngine;

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
    };

    ClaudeClient(LuaEngine& lua);
    ~ClaudeClient() override;

    void setSystemPrompt(const juce::String& prompt);
    void sendMessage(const juce::String& userText);
    void setListener(Listener* l) { listener = l; }
    bool isBusy() const { return isThreadRunning(); }

private:
    void run() override;

    LuaEngine& lua;
    Listener* listener = nullptr;

    juce::String apiKey;
    juce::String systemPrompt;
    juce::String model { "claude-sonnet-4-20250514" };

    std::vector<Message> conversationHistory;
    juce::String pendingUserText;
    std::mutex sendMutex;

    juce::String buildRequestJson();
    Message parseResponse(const juce::String& responseBody);
    juce::String executeLua(const juce::String& code);

    void notifyText(const juce::String& text);
    void notifyToolUse(const juce::String& name, const juce::String& code,
                       const juce::String& result, bool isError);
    void notifyError(const juce::String& error);
    void notifyBusy(bool busy);
};
