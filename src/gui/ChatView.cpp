#include "gui/ChatView.h"
#include "engine/Log.h"

ChatView::ChatView(LuaEngine& lua) : client(lua) {
    client.setListener(this);

    // Input field
    inputField.setMultiLine(false);
    inputField.setReturnKeyStartsNewLine(false);
    inputField.setFont(Theme::font(Theme::fontSize));
    inputField.setColour(juce::TextEditor::backgroundColourId, Theme::color(Theme::Color::bgSlot));
    inputField.setColour(juce::TextEditor::textColourId, Theme::color(Theme::Color::textWhite));
    inputField.setColour(juce::TextEditor::outlineColourId, Theme::color(Theme::Color::border));
    inputField.setColour(juce::TextEditor::focusedOutlineColourId, Theme::color(Theme::Color::accent));
    inputField.setTextToShowWhenEmpty("Message Claude...", Theme::color(Theme::Color::textDim));
    inputField.onReturnKey = [this] { sendCurrentInput(); };
    addAndMakeVisible(inputField);

    // Message list in viewport
    messageList.owner = this;
    messageViewport.setViewedComponent(&messageList, false);
    messageViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(messageViewport);

    setOpaque(true);
}

void ChatView::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));
}

void ChatView::resized() {
    auto area = getLocalBounds();

    // Input at bottom
    inputField.setBounds(area.removeFromBottom(inputHeight).reduced(messagePadding, 4));

    // Messages fill the rest
    messageViewport.setBounds(area);
    layoutMessages();
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
    addMessage({ DisplayMessage::User, text, {}, {} });
    client.sendMessage(text);
}

// --- ClaudeClient::Listener ---

void ChatView::onAssistantText(const juce::String& text) {
    addMessage({ DisplayMessage::Assistant, text, {}, {} });
}

void ChatView::onToolUse(const juce::String&, const juce::String& code,
                          const juce::String& result, bool) {
    addMessage({ DisplayMessage::Tool, {}, code, result });
}

void ChatView::onError(const juce::String& error) {
    addMessage({ DisplayMessage::Error, error, {}, {} });
}

void ChatView::onBusyChanged(bool busy) {
    inputField.setEnabled(!busy);
    if (!busy)
        inputField.grabKeyboardFocus();
}

// --- Message display ---

void ChatView::addMessage(DisplayMessage msg) {
    messages.push_back(std::move(msg));
    layoutMessages();
    scrollToBottom();
}

int ChatView::computeMessageHeight(const DisplayMessage& msg, int width) const {
    int contentWidth = width - messagePadding * 4;
    auto font = Theme::font(Theme::fontSizeSm);

    if (msg.type == DisplayMessage::Tool) {
        auto monoFont = Theme::fontMono(Theme::fontSizeXs);
        int lineH = (int)monoFont.getHeight();
        int codeLines = std::max(1, (int)msg.toolCode.length() / std::max(1, contentWidth / 7) + 1);
        int codeHeight = codeLines * lineH + 8;
        int resultHeight = lineH + 8;
        return codeHeight + resultHeight + bubblePadding * 2 + 4;
    }

    auto text = msg.text;
    int lineH = (int)font.getHeight();
    // Estimate lines from character count and average char width
    int charsPerLine = std::max(1, contentWidth / 7);
    int lines = std::max(1, (int)text.length() / charsPerLine + 1);
    // Account for actual newlines
    int newlines = 0;
    for (auto c : text) if (c == '\n') newlines++;
    lines = std::max(lines, newlines + 1);
    return lines * lineH + bubblePadding * 2;
}

void ChatView::layoutMessages() {
    int width = messageViewport.getWidth();
    if (width <= 0) return;

    int totalHeight = messagePadding;
    for (auto& msg : messages)
        totalHeight += computeMessageHeight(msg, width) + messagePadding;

    messageList.setSize(width, std::max(totalHeight, messageViewport.getHeight()));
}

void ChatView::scrollToBottom() {
    messageViewport.setViewPosition(0, std::max(0, messageList.getHeight() - messageViewport.getHeight()));
}

// --- Message rendering ---

void ChatView::MessageList::paint(juce::Graphics& g) {
    if (!owner) return;
    auto* chatView = owner;

    int width = getWidth();
    int y = ChatView::messagePadding;

    for (auto& msg : chatView->messages) {
        int msgHeight = chatView->computeMessageHeight(msg, width);
        auto bubbleArea = juce::Rectangle<int>(ChatView::messagePadding, y,
                                                width - ChatView::messagePadding * 2, msgHeight);

        // Background
        juce::Colour bgColor;
        switch (msg.type) {
            case DisplayMessage::User:
                bgColor = Theme::color(Theme::Color::bgHeader).withAlpha(0.3f);
                break;
            case DisplayMessage::Assistant:
                bgColor = Theme::color(Theme::Color::bgSlot);
                break;
            case DisplayMessage::Tool:
                bgColor = Theme::color(Theme::Color::bgPanel);
                break;
            case DisplayMessage::Error:
                bgColor = juce::Colour(0xff442222);
                break;
        }
        g.setColour(bgColor);
        g.fillRoundedRectangle(bubbleArea.toFloat(), Theme::cornerRadiusSm);

        auto textArea = bubbleArea.reduced(ChatView::bubblePadding);

        if (msg.type == DisplayMessage::Tool) {
            // Code block
            auto monoFont = Theme::fontMono(Theme::fontSizeXs);
            g.setFont(monoFont);
            g.setColour(Theme::color(Theme::Color::instrument));

            auto codeArea = textArea.removeFromTop(textArea.getHeight() / 2);
            g.drawFittedText(msg.toolCode, codeArea, juce::Justification::topLeft, 10);

            // Result
            g.setColour(Theme::color(Theme::Color::textSecondary));
            g.drawFittedText(msg.toolResult, textArea, juce::Justification::topLeft, 3);
        } else {
            // Label
            auto font = Theme::font(Theme::fontSizeXs);
            g.setFont(font);
            if (msg.type == DisplayMessage::User)
                g.setColour(Theme::color(Theme::Color::textDim));
            else if (msg.type == DisplayMessage::Error)
                g.setColour(juce::Colour(0xffcc6666));
            else
                g.setColour(Theme::color(Theme::Color::textPrimary));

            // Text
            g.setFont(Theme::font(Theme::fontSizeSm));
            g.drawFittedText(msg.text, textArea, juce::Justification::topLeft, 100);
        }

        y += msgHeight + ChatView::messagePadding;
    }
}
