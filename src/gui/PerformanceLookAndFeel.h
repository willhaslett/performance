#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"

// App-wide LookAndFeel that styles JUCE-drawn UI elements (popup menus,
// combo box dropdowns, tooltips, etc.) using Theme tokens so they match the
// current theme. Set once at app startup via setDefaultLookAndFeel.

class PerformanceLookAndFeel : public juce::LookAndFeel_V4 {
public:
    PerformanceLookAndFeel();

    // --- PopupMenu ---
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

    void drawPopupMenuItem(juce::Graphics& g,
                           const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColourToUse) override;

    void drawPopupMenuSectionHeader(juce::Graphics& g,
                                    const juce::Rectangle<int>& area,
                                    const juce::String& sectionName) override;

    int getPopupMenuBorderSize() override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator,
                                   int standardMenuItemHeight,
                                   int& idealWidth, int& idealHeight) override;

    // --- ComboBox dropdown ---
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    // --- DocumentWindow chrome ---
    juce::Button* createDocumentWindowButton(int buttonType) override;
};
