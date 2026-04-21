#include "gui/ChatView.h"
#include "engine/Log.h"

static juce::Colour bubbleColor(ChatView::Bubble::Type type) {
    switch (type) {
        case ChatView::Bubble::User:      return Theme::color(Theme::Color::chatUser);
        case ChatView::Bubble::Assistant:  return Theme::color(Theme::Color::chatAssistant);
        case ChatView::Bubble::Tool:      return Theme::color(Theme::Color::chatTool);
        case ChatView::Bubble::Error:     return Theme::color(Theme::Color::chatError);
    }
    return Theme::color(Theme::Color::chatAssistant);
}

static juce::Colour textColor(ChatView::Bubble::Type type) {
    switch (type) {
        case ChatView::Bubble::User:      return Theme::color(Theme::Color::textOnColor);
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
    inputField.setFont(Theme::font(Theme::fontSizeLg));
    inputField.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    inputField.setColour(juce::TextEditor::textColourId, Theme::color(Theme::Color::textOnColor));
    inputField.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    inputField.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    inputField.setTextToShowWhenEmpty("Message Claude...", Theme::color(Theme::Color::textDim));
    inputField.setJustification(juce::Justification::centredLeft);
    inputField.setIndents((int)(Theme::chatInputHeight * 0.4f), 0);
    inputField.onReturnKey = [this] { sendCurrentInput(); };
    addAndMakeVisible(inputField);

    // Bubble backgrounds (painted behind TextEditors)
    bubbleBg.owner = this;
    messageContainer.addAndMakeVisible(bubbleBg);

    // Typing indicator — added but hidden until a request is in flight
    messageContainer.addChildComponent(typingIndicator);

    // Message container in viewport
    messageViewport.setViewedComponent(&messageContainer, false);
    messageViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(messageViewport);

    // Compose toggle — latching button in the top-right of the pane.
    // Flipping it swaps the system prompt and clears any in-flight
    // conversation (the two personas don't share context cleanly).
    composeToggle.setButtonText("Compose");
    composeToggle.setClickingTogglesState(true);
    composeToggle.setColour(juce::TextButton::buttonColourId,
                             Theme::color(Theme::Color::bgControl));
    composeToggle.setColour(juce::TextButton::buttonOnColourId,
                             Theme::color(Theme::Color::accent));
    composeToggle.setColour(juce::TextButton::textColourOffId,
                             Theme::color(Theme::Color::textSecondary));
    composeToggle.setColour(juce::TextButton::textColourOnId,
                             Theme::color(Theme::Color::textOnColor));
    composeToggle.onClick = [this]() {
        composeActive = composeToggle.getToggleState();
        applyActivePrompt();
        inputField.setTextToShowWhenEmpty(
            composeActive ? "Message composer..." : "Message Claude...",
            Theme::color(Theme::Color::textDim));
        repaint();
    };
    addAndMakeVisible(composeToggle);

    setOpaque(true);
}

void ChatView::setPrompts(const juce::String& perf,
                           const juce::String& composer) {
    perfPrompt = perf;
    composerPrompt = composer;
    // If the composer prompt is empty, hide the toggle entirely so
    // builds without the bundled composer prompt don't advertise it.
    composeToggle.setVisible(composerPrompt.isNotEmpty());
    applyActivePrompt();
}

void ChatView::applyActivePrompt() {
    const auto& p = (composeActive && composerPrompt.isNotEmpty())
                        ? composerPrompt : perfPrompt;
    client.setSystemPrompt(p);
}

void ChatView::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgPanel));

    // Draw rounded input background
    auto inputBounds = inputField.getBounds().toFloat().expanded(2.0f, 0.0f);
    float radius = inputBounds.getHeight() * 0.5f;
    g.setColour(Theme::color(Theme::Color::chatInput));
    g.fillRoundedRectangle(inputBounds, radius);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawRoundedRectangle(inputBounds, radius, 1.0f);
}

void ChatView::resized() {
    auto area = getLocalBounds();
    int pad = Theme::chatBubblePad;

    // Compose toggle in the top-right corner.
    int toggleW = 90, toggleH = 26;
    composeToggle.setBounds(area.getRight() - toggleW - pad, pad,
                             toggleW, toggleH);

    auto inputArea = area.removeFromBottom(Theme::chatInputHeight + pad * 2);
    auto fieldBounds = inputArea.reduced(pad + 4, 0)
                                .withSizeKeepingCentre(inputArea.getWidth() - (pad + 4) * 2,
                                                       Theme::chatInputHeight);
    inputField.setBounds(fieldBounds);

    // Leave vertical room for the toggle at the top.
    area.removeFromTop(toggleH + pad);
    messageViewport.setBounds(area);
    layoutBubbles();
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

void ChatView::onToolUse(const juce::String&, const juce::String&,
                          const juce::String&, bool) {
    // Tool calls, results, and errors are intentionally not rendered in chat —
    // users see only assistant messages. Full tool activity is logged to
    // /tmp/performance.log via ClaudeClient for debugging.
}

void ChatView::onError(const juce::String& error) {
    addBubble(Bubble::Error, error);
}

void ChatView::onBusyChanged(bool busy) {
    typingIndicator.setVisible(busy);
    layoutBubbles();
    scrollToBottom();
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
    editor->setFont(type == Bubble::Tool ? Theme::fontMono(Theme::fontSizeSm)
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

    // Position the typing indicator just below the last bubble when visible.
    // Compact width — matches the iMessage/Slack-style "small composing bubble"
    // rather than a full-width bubble.
    if (typingIndicator.isVisible()) {
        int indicatorHeight = (int)Theme::font(Theme::fontSizeSm).getHeight() + 4
                              + Theme::chatBubblePad * 2;
        int indicatorWidth = Theme::chatBubblePad * 4 + 34;  // 3 dots (6px) + 2 gaps (8px) = 34
        typingIndicator.setBounds(Theme::chatBubblePad, y, indicatorWidth, indicatorHeight);
        y += indicatorHeight + Theme::chatGap;
    }

    int totalHeight = std::max(y, messageViewport.getHeight());
    messageContainer.setSize(width, totalHeight);
    bubbleBg.setBounds(0, 0, width, totalHeight);
    bubbleBg.repaint();
}

// --- TypingIndicator ---

void ChatView::TypingIndicator::visibilityChanged() {
    if (isVisible()) startTimerHz(30);
    else stopTimer();
}

void ChatView::TypingIndicator::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().toFloat();

    // Assistant-style bubble background
    g.setColour(Theme::color(Theme::Color::chatAssistant));
    g.fillRoundedRectangle(bounds, (float)Theme::chatBubbleRadius);

    // Three dots, phase-shifted so they pulse in sequence
    const float dotSize = 6.0f;
    const float dotGap = 8.0f;
    const float totalDotsWidth = dotSize * 3 + dotGap * 2;
    const float startX = bounds.getX() + (bounds.getWidth() - totalDotsWidth) * 0.5f + dotSize * 0.5f;
    const float cy = bounds.getCentreY();

    const double t = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double period = 1.2;
    auto dotBase = Theme::color(Theme::Color::textSecondary);

    for (int i = 0; i < 3; ++i) {
        const double phase = (t / period - i * 0.15) * juce::MathConstants<double>::twoPi;
        const float v = 0.5f * (1.0f + (float)std::sin(phase));
        const float alpha = 0.3f + 0.55f * v;
        const float x = startX + i * (dotSize + dotGap);
        g.setColour(dotBase.withAlpha(alpha));
        g.fillEllipse(x - dotSize * 0.5f, cy - dotSize * 0.5f, dotSize, dotSize);
    }
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
