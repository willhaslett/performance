#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "api/ClaudeClient.h"
#include "gui/Theme.h"
#include <vector>

class LuaEngine;

class ChatView : public juce::Component,
                 public ClaudeClient::Listener {
public:
    ChatView(LuaEngine& lua);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void focusInput();
    void unfocusInput();
    void setSystemPrompt(const juce::String& prompt) { client.setSystemPrompt(prompt); }

    // ClaudeClient::Listener
    void onAssistantText(const juce::String& text) override;
    void onToolUse(const juce::String& name, const juce::String& code,
                   const juce::String& result, bool isError) override;
    void onError(const juce::String& error) override;
    void onBusyChanged(bool busy) override;

private:
    ClaudeClient client;
    juce::TextEditor inputField;
    juce::Viewport messageViewport;

    // Message container inside viewport
    class MessageList : public juce::Component {
    public:
        void paint(juce::Graphics& g) override;
    };
    MessageList messageList;

    struct DisplayMessage {
        enum Type { User, Assistant, Tool, Error };
        Type type;
        juce::String text;
        juce::String toolCode;
        juce::String toolResult;
    };
    std::vector<DisplayMessage> messages;

    void sendCurrentInput();
    void addMessage(DisplayMessage msg);
    void layoutMessages();
    void scrollToBottom();

    int computeMessageHeight(const DisplayMessage& msg, int width) const;

    static constexpr int inputHeight = 36;
    static constexpr int messagePadding = 10;
    static constexpr int bubblePadding = 10;
};
