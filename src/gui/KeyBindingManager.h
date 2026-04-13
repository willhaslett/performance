#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <map>
#include <vector>
#include <functional>

// Manages keyboard shortcuts with defaults + user overrides.
// Single source of truth for "what key does this command use?"

class StateAPI;

struct KeyCommand {
    std::string id;         // e.g. "edit.undo"
    std::string category;   // e.g. "Edit"
    std::string label;      // e.g. "Undo"
    juce::KeyPress defaultKey;
    juce::KeyPress currentKey;  // may differ if user overrode
};

class KeyBindingManager {
public:
    KeyBindingManager();

    // Register a command with its default binding
    void registerCommand(const std::string& id, const std::string& category,
                         const std::string& label, const juce::KeyPress& defaultKey);

    // Load user overrides from state config
    void loadOverrides(StateAPI& state);
    // Save user overrides to state config
    void saveOverrides(StateAPI& state);

    // Lookup
    juce::KeyPress getKey(const std::string& commandId) const;
    std::string getCommandForKey(const juce::KeyPress& key) const;
    bool matches(const std::string& commandId, const juce::KeyPress& key) const;

    // Set a custom binding (empty KeyPress to clear override)
    void setBinding(const std::string& commandId, const juce::KeyPress& key);
    void restoreDefault(const std::string& commandId);
    void restoreAllDefaults();

    // Get all commands (for the editor UI)
    const std::vector<KeyCommand>& allCommands() const { return commands; }

    // Get shortcut display string for a command
    juce::String getShortcutText(const std::string& commandId) const;

    // Convert KeyPress to/from a storable string
    static juce::String keyToString(const juce::KeyPress& key);
    static juce::KeyPress stringToKey(const juce::String& str);

private:
    std::vector<KeyCommand> commands;
    std::map<std::string, int> commandIndex;  // id → index into commands
};
