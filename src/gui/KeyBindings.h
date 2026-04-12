#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Default keyboard shortcuts. All require modifier keys — no bare letter keys.
// This is the single source of truth for keyboard bindings.

namespace KeyBindings {
    // View toggles
    inline const auto toggleSidebar = juce::KeyPress('1', juce::ModifierKeys::commandModifier, 0);
    inline const auto toggleMixer   = juce::KeyPress('x', juce::ModifierKeys::commandModifier, 0);

    // Actions
    inline const auto save          = juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0);
    inline const auto settings      = juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0);
    inline const auto closeEditor   = juce::KeyPress(juce::KeyPress::escapeKey);
    inline const auto musicalTyping = juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0);
}
