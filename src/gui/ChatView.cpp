#include "gui/ChatView.h"
#include "engine/Log.h"

static juce::Colour bubbleColor(ChatView::Bubble::Type type) {
    switch (type) {
        case ChatView::Bubble::User:      return Theme::color(Theme::Color::chatBgUser);
        case ChatView::Bubble::Assistant:  return Theme::color(Theme::Color::chatBgAsst);
        case ChatView::Bubble::Tool:      return Theme::color(Theme::Color::chatBgTool);
        case ChatView::Bubble::Error:     return Theme::color(Theme::Color::chatBgError);
    }
    return Theme::color(Theme::Color::chatBgAsst);
}

static juce::Colour textColor(ChatView::Bubble::Type type) {
    switch (type) {
        case ChatView::Bubble::User:      return Theme::color(Theme::Color::textWhite);
        case ChatView::Bubble::Assistant:  return Theme::color(Theme::Color::textPrimary);
        case ChatView::Bubble::Tool:      return Theme::color(Theme::Color::textSecondary);
        case ChatView::Bubble::Error:     return juce::Colour(0xffcc8888);
    }
    return Theme::color(Theme::Color::textPrimary);
}

ChatView::ChatView(LuaEngine& lua) : client(lua) {
    client.setListener(this);

    // Input field — rounded, chat-style
    inputField.setMultiLine(false);
    inputField.setReturnKeyStartsNewLine(false);
    inputField.setFont(Theme::font(Theme::fontSize));
    inputField.setColour(juce::TextEditor::backgroundColourId, Theme::color(Theme::Color::chatInput));
    inputField.setColour(juce::TextEditor::textColourId, Theme::color(Theme::Color::textWhite));
    inputField.setColour(juce::TextEditor::outlineColourId, Theme::color(Theme::Color::border));
    inputField.setColour(juce::TextEditor::focusedOutlineColourId, Theme::color(Theme::Color::accent));
    inputField.setTextToShowWhenEmpty("Message Claude...", Theme::color(Theme::Color::textDim));
    inputField.setJustification(juce::Justification::centredLeft);
    inputField.onReturnKey = [this] { sendCurrentInput(); };
    addAndMakeVisible(inputField);

    // Bubble backgrounds (painted behind TextEditors)
    bubbleBg.owner = this;
    messageContainer.addAndMakeVisible(bubbleBg);

    // Message container in viewport
    messageViewport.setViewedComponent(&messageContainer, false);
    messageViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(messageViewport);

    setOpaque(true);
}

void ChatView::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));
}

void ChatView::resized() {
    auto area = getLocalBounds();
    auto inputArea = area.removeFromBottom(Theme::chatInputHeight + Theme::chatGap * 2);
    inputField.setBounds(inputArea.reduced(Theme::chatBubblePad, Theme::chatGap));

    messageViewport.setBounds(area);
    layoutBubbles();
}

void ChatView::focusInput() {
    inputField.grabKeyboardFocus();
}

void ChatView::unfocusInput() {
    inputField.giveAwayKeyboardFocus();
}

void ChatView::sendCurrentInput() {
    auto text = inputField.getText().trim();
    if (text.isEmpty() || client.isBusy()) return;

    inputField.clear();
    addBubble(Bubble::User, text);
    client.sendMessage(text);
}

// --- ClaudeClient::Listener ---

void ChatView::onAssistantText(const juce::String& text) {
    addBubble(Bubble::Assistant, text);
}

void ChatView::onToolUse(const juce::String&, const juce::String& code,
                          const juce::String& result, bool) {
    addBubble(Bubble::Tool, code + "\n> " + result);
}

void ChatView::onError(const juce::String& error) {
    addBubble(Bubble::Error, error);
}

void ChatView::onBusyChanged(bool busy) {
    inputField.setEnabled(!busy);
    if (!busy)
        inputField.grabKeyboardFocus();
}

// --- Bubble management ---

ChatView::Bubble* ChatView::addBubble(Bubble::Type type, const juce::String& text) {
    auto bubble = std::make_unique<Bubble>();
    bubble->type = type;

    auto editor = std::make_unique<juce::TextEditor>();
    editor->setMultiLine(true);
    editor->setReadOnly(true);
    editor->setScrollbarsShown(false);
    editor->setPopupMenuEnabled(true);  // right-click copy
    editor->setFont(type == Bubble::Tool ? Theme::fontMono(Theme::fontSizeXs)
                                          : Theme::font(Theme::fontSizeSm));
    editor->setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::textColourId, textColor(type));
    editor->setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    editor->setColour(juce::TextEditor::highlightColourId, Theme::color(Theme::Color::accent).withAlpha(0.4f));
    editor->setText(text, false);

    messageContainer.addAndMakeVisible(*editor);
    bubble->textEditor = std::move(editor);

    auto* ptr = bubble.get();
    bubbles.push_back(std::move(bubble));

    layoutBubbles();
    scrollToBottom();
    return ptr;
}

void ChatView::layoutBubbles() {
    int width = messageViewport.getWidth();
    if (width <= 0) return;

    int bubbleWidth = width - Theme::chatBubblePad * 2;
    int y = Theme::chatGap;

    for (auto& bubble : bubbles) {
        auto& editor = *bubble->textEditor;
        int textWidth = bubbleWidth - Theme::chatBubblePad * 2;

        // Measure text height using TextLayout
        juce::AttributedString attrStr;
        attrStr.append(editor.getText(), editor.getFont(),
                       editor.findColour(juce::TextEditor::textColourId));
        juce::TextLayout layout;
        layout.createLayout(attrStr, (float)textWidth);
        int textHeight = std::max((int)editor.getFont().getHeight() + 4,
                                   (int)std::ceil(layout.getHeight()) + 4);

        int bubbleHeight = textHeight + Theme::chatBubblePad * 2;
        bubble->height = bubbleHeight;

        int bx = Theme::chatBubblePad;
        editor.setBounds(bx + Theme::chatBubblePad, y + Theme::chatBubblePad,
                          textWidth, textHeight);

        y += bubbleHeight + Theme::chatGap;
    }

    int totalHeight = std::max(y, messageViewport.getHeight());
    messageContainer.setSize(width, totalHeight);
    bubbleBg.setBounds(0, 0, width, totalHeight);
    bubbleBg.repaint();
}

void ChatView::scrollToBottom() {
    juce::MessageManager::callAsync([this] {
        messageViewport.setViewPosition(0,
            std::max(0, messageContainer.getHeight() - messageViewport.getHeight()));
    });
}

// --- Bubble background painting ---

void ChatView::BubbleBackground::paint(juce::Graphics& g) {
    if (!owner) return;

    int width = getWidth();
    int bubbleWidth = width - Theme::chatBubblePad * 2;
    int y = Theme::chatGap;

    for (auto& bubble : owner->bubbles) {
        auto area = juce::Rectangle<int>(Theme::chatBubblePad, y, bubbleWidth, bubble->height);
        g.setColour(bubbleColor(bubble->type));
        g.fillRoundedRectangle(area.toFloat(), Theme::chatBubbleRadius);
        y += bubble->height + Theme::chatGap;
    }
}
