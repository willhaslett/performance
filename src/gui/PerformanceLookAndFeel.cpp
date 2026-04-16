#include "gui/PerformanceLookAndFeel.h"

PerformanceLookAndFeel::PerformanceLookAndFeel() {
    // Base colours that JUCE components read via findColour:
    setColour(juce::PopupMenu::backgroundColourId,
              Theme::color(Theme::Color::bgOverlay));
    setColour(juce::PopupMenu::highlightedBackgroundColourId,
              Theme::color(Theme::Color::bgControlHover));
    setColour(juce::PopupMenu::textColourId,
              Theme::color(Theme::Color::textPrimary));
    setColour(juce::PopupMenu::highlightedTextColourId,
              Theme::color(Theme::Color::textPrimary));
    setColour(juce::PopupMenu::headerTextColourId,
              Theme::color(Theme::Color::textSecondary));

    setColour(juce::ComboBox::backgroundColourId,
              Theme::color(Theme::Color::bgControl));
    setColour(juce::ComboBox::textColourId,
              Theme::color(Theme::Color::textPrimary));
    setColour(juce::ComboBox::outlineColourId,
              Theme::color(Theme::Color::border));
    setColour(juce::ComboBox::arrowColourId,
              Theme::color(Theme::Color::textSecondary));

    setColour(juce::TextEditor::backgroundColourId,
              Theme::color(Theme::Color::bgControl));
    setColour(juce::TextEditor::textColourId,
              Theme::color(Theme::Color::textPrimary));
    setColour(juce::TextEditor::outlineColourId,
              Theme::color(Theme::Color::border));

    setColour(juce::DocumentWindow::textColourId,
              Theme::color(Theme::Color::textPrimary));
    setColour(juce::ResizableWindow::backgroundColourId,
              Theme::color(Theme::Color::bgPanel));

    setColour(juce::ScrollBar::thumbColourId,
              Theme::color(Theme::Color::bgControlHover));
    setColour(juce::ScrollBar::trackColourId,
              Theme::color(Theme::Color::bgApp));
}

// --- PopupMenu ---

void PerformanceLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(Theme::color(Theme::Color::bgOverlay));
    g.setColour(Theme::color(Theme::Color::border));
    g.drawRect(0, 0, width, height, 1);
}

void PerformanceLookAndFeel::drawPopupMenuItem(
        juce::Graphics& g,
        const juce::Rectangle<int>& area,
        bool isSeparator, bool isActive, bool isHighlighted,
        bool isTicked, bool hasSubMenu,
        const juce::String& text,
        const juce::String& shortcutKeyText,
        const juce::Drawable* /*icon*/,
        const juce::Colour* textColourToUse) {

    if (isSeparator) {
        auto sepArea = area.reduced(Theme::spacingM, 0);
        g.setColour(Theme::color(Theme::Color::borderSubtle));
        g.drawLine((float)sepArea.getX(), (float)sepArea.getCentreY(),
                   (float)sepArea.getRight(), (float)sepArea.getCentreY(), 1.0f);
        return;
    }

    if (isHighlighted && isActive) {
        g.setColour(Theme::color(Theme::Color::bgControlHover));
        g.fillRect(area);
    }

    auto textCol = textColourToUse ? *textColourToUse
                   : Theme::color(isActive ? Theme::Color::textPrimary
                                           : Theme::Color::textDim);
    g.setColour(textCol);
    g.setFont(Theme::font(Theme::fontSizeMd));

    auto textArea = area.reduced(Theme::spacingL, 0);

    if (isTicked) {
        g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"),
                   textArea.removeFromLeft(20), juce::Justification::centred);
    } else {
        textArea.removeFromLeft(20);
    }

    if (hasSubMenu) {
        auto arrowArea = textArea.removeFromRight(16);
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb6"), arrowArea,
                   juce::Justification::centred);
        g.setColour(textCol);
    }

    if (shortcutKeyText.isNotEmpty()) {
        juce::GlyphArrangement ga;
        ga.addLineOfText(Theme::font(Theme::fontSizeSm), shortcutKeyText, 0, 0);
        auto shortcutArea = textArea.removeFromRight((int)ga.getBoundingBox(0, -1, false).getWidth() + 16);
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(shortcutKeyText, shortcutArea, juce::Justification::centredRight);
        g.setColour(textCol);
        g.setFont(Theme::font(Theme::fontSizeMd));
    }

    g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);
}

void PerformanceLookAndFeel::drawPopupMenuSectionHeader(
        juce::Graphics& g,
        const juce::Rectangle<int>& area,
        const juce::String& sectionName) {
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeSm));
    g.drawText(sectionName, area.reduced(Theme::spacingL, 0),
               juce::Justification::centredLeft);
}

int PerformanceLookAndFeel::getPopupMenuBorderSize() {
    return 1;
}

void PerformanceLookAndFeel::getIdealPopupMenuItemSize(
        const juce::String& text, bool isSeparator,
        int /*standardMenuItemHeight*/,
        int& idealWidth, int& idealHeight) {
    if (isSeparator) {
        idealWidth = 50;
        idealHeight = Theme::spacingM;
        return;
    }
    juce::GlyphArrangement ga;
    ga.addLineOfText(Theme::font(Theme::fontSizeMd), text, 0, 0);
    idealWidth = (int)ga.getBoundingBox(0, -1, false).getWidth() + 60;
    idealHeight = 26;
}

// --- ComboBox ---

void PerformanceLookAndFeel::drawComboBox(
        juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
        int /*buttonX*/, int /*buttonY*/, int /*buttonW*/, int /*buttonH*/,
        juce::ComboBox& box) {
    auto bounds = juce::Rectangle<int>(0, 0, width, height);
    g.setColour(Theme::color(Theme::Color::bgControl));
    g.fillRoundedRectangle(bounds.toFloat(), Theme::cornerRadiusSm);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), Theme::cornerRadiusSm, 1.0f);

    // Arrow
    auto arrowArea = bounds.removeFromRight(height).toFloat().reduced(8.0f);
    juce::Path arrow;
    arrow.addTriangle(arrowArea.getX(), arrowArea.getY(),
                      arrowArea.getRight(), arrowArea.getY(),
                      arrowArea.getCentreX(), arrowArea.getBottom());
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.fillPath(arrow);
}

// --- DocumentWindow chrome ---

juce::Button* PerformanceLookAndFeel::createDocumentWindowButton(int buttonType) {
    auto* btn = juce::LookAndFeel_V4::createDocumentWindowButton(buttonType);
    btn->setColour(juce::TextButton::buttonColourId,
                   Theme::color(Theme::Color::bgControl));
    btn->setColour(juce::TextButton::buttonOnColourId,
                   Theme::color(Theme::Color::bgControlHover));
    return btn;
}
