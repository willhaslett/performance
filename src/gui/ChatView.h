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

    // The caller supplies both prompts at startup (from BinaryData). The
    // chat defaults to `perf` mode; the Compose toggle swaps to the
    // composer prompt in-session.
    void setPrompts(const juce::String& perfPrompt,
                    const juce::String& composerPrompt);

    // Legacy single-prompt setter — keeps existing call-sites working
    // during the transition; effectively sets the perf prompt and
    // leaves the composer prompt empty.
    void setSystemPrompt(const juce::String& prompt) { setPrompts(prompt, {}); }

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

    // Latching Compose toggle — header button. When pressed, the
    // composer system prompt is active; when released, the general
    // perf prompt is active. Toggling clears conversation history
    // because the two personas have incompatible context.
    juce::TextButton composeToggle;
    juce::String perfPrompt;
    juce::String composerPrompt;
    bool composeActive = false;
    void applyActivePrompt();

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

    // "Assistant is composing" indicator. Three dots in an assistant-style
    // bubble, pulsing with phase-shifted opacity. Timer runs only while
    // visible (visibilityChanged handles start/stop).
    class TypingIndicator : public juce::Component, private juce::Timer {
    public:
        TypingIndicator() = default;
        void paint(juce::Graphics& g) override;
        void visibilityChanged() override;
    private:
        void timerCallback() override { repaint(); }
    };
    TypingIndicator typingIndicator;
};
