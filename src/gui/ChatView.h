#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "api/ClaudeClient.h"
#include "gui/Theme.h"
#include <vector>
#include <memory>

class LuaEngine;

class ChatView : public juce::Component,
                 public ClaudeClient::Listener {
public:
    ChatView(LuaEngine& lua);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void setSystemPrompt(const juce::String& prompt) { client.setSystemPrompt(prompt); }

    // A chat bubble: rounded background + read-only TextEditor for selectable text
    struct Bubble {
        enum Type { User, Assistant, Tool, Error };
        Type type;
        std::unique_ptr<juce::TextEditor> textEditor;
        int height = 0;
    };

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
    juce::Component messageContainer;

    std::vector<std::unique_ptr<Bubble>> bubbles;

    void sendCurrentInput();
    Bubble* addBubble(Bubble::Type type, const juce::String& text);
    void layoutBubbles();
    void scrollToBottom();

    // Custom paint for bubble backgrounds (drawn behind the TextEditors)
    class BubbleBackground : public juce::Component {
    public:
        ChatView* owner = nullptr;
        void paint(juce::Graphics& g) override;
    };
    BubbleBackground bubbleBg;
};
