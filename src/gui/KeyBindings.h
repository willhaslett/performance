#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Default keyboard shortcuts. All require modifier keys — no bare letter keys.
// This is the single source of truth for keyboard bindings.

namespace KeyBindings {
    // View toggles. ⌘Y/U/I/O/P map left-to-right across the keyboard
    // for the five pane toggles in the sidebar; ⌘S took over Sidebar
    // toggle when Save lost its accelerator (autosave makes it useless).
    inline const auto toggleSidebar = juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0);
    inline const auto toggleMixer   = juce::KeyPress('o', juce::ModifierKeys::commandModifier, 0);
    inline const auto toggleLooper  = juce::KeyPress('p', juce::ModifierKeys::commandModifier, 0);

    // Actions
    inline const auto settings      = juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0);
    inline const auto closeEditor   = juce::KeyPress(juce::KeyPress::escapeKey);
    inline const auto musicalTyping = juce::KeyPress('k', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
    inline const auto undo          = juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0);
    inline const auto redo          = juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
}
