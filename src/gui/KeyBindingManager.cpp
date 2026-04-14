#include "gui/KeyBindingManager.h"
#include "api/StateAPI.h"

KeyBindingManager::KeyBindingManager() {}

void KeyBindingManager::registerCommand(const std::string& id, const std::string& category,
                                         const std::string& label, const juce::KeyPress& defaultKey) {
    KeyCommand cmd;
    cmd.id = id;
    cmd.category = category;
    cmd.label = label;
    cmd.defaultKey = defaultKey;
    cmd.currentKey = defaultKey;
    commandIndex[id] = (int)commands.size();
    commands.push_back(std::move(cmd));
}

void KeyBindingManager::loadOverrides(StateAPI& state) {
    for (auto& cmd : commands) {
        auto val = state.getConfig("keybinding_" + cmd.id);
        if (!val.empty())
            cmd.currentKey = stringToKey(juce::String(val));
    }
}

void KeyBindingManager::saveOverrides(StateAPI& state) {
    for (auto& cmd : commands) {
        std::string key = "keybinding_" + cmd.id;
        if (cmd.currentKey == cmd.defaultKey)
            state.setConfig(key, "");  // no override needed
        else
            state.setConfig(key, keyToString(cmd.currentKey).toStdString());
    }
}

juce::KeyPress KeyBindingManager::getKey(const std::string& commandId) const {
    auto it = commandIndex.find(commandId);
    if (it == commandIndex.end()) return {};
    return commands[it->second].currentKey;
}

std::string KeyBindingManager::getCommandForKey(const juce::KeyPress& key) const {
    for (auto& cmd : commands)
        if (cmd.currentKey == key) return cmd.id;
    return {};
}

bool KeyBindingManager::matches(const std::string& commandId, const juce::KeyPress& key) const {
    auto it = commandIndex.find(commandId);
    if (it == commandIndex.end()) return false;
    return commands[it->second].currentKey == key;
}

void KeyBindingManager::setBinding(const std::string& commandId, const juce::KeyPress& key) {
    auto it = commandIndex.find(commandId);
    if (it == commandIndex.end()) return;
    commands[it->second].currentKey = key;
}

void KeyBindingManager::restoreDefault(const std::string& commandId) {
    auto it = commandIndex.find(commandId);
    if (it == commandIndex.end()) return;
    commands[it->second].currentKey = commands[it->second].defaultKey;
}

void KeyBindingManager::restoreAllDefaults() {
    for (auto& cmd : commands)
        cmd.currentKey = cmd.defaultKey;
}

juce::String KeyBindingManager::getShortcutText(const std::string& commandId) const {
    auto key = getKey(commandId);
    if (!key.isValid()) return {};
    return keyToString(key);
}

juce::String KeyBindingManager::keyToString(const juce::KeyPress& key) {
    if (!key.isValid()) return {};
    juce::String result;
    auto mods = key.getModifiers();
    if (mods.isCommandDown()) result += juce::String(juce::CharPointer_UTF8("\xe2\x8c\x98"));
    if (mods.isShiftDown())   result += juce::String(juce::CharPointer_UTF8("\xe2\x87\xa7"));
    if (mods.isAltDown())     result += juce::String(juce::CharPointer_UTF8("\xe2\x8c\xa5"));
    if (mods.isCtrlDown())    result += juce::String(juce::CharPointer_UTF8("\xe2\x8c\x83"));

    int code = key.getKeyCode();
    if (code == juce::KeyPress::spaceKey)       result += "Space";
    else if (code == juce::KeyPress::returnKey)  result += "Return";
    else if (code == juce::KeyPress::escapeKey)  result += "Esc";
    else if (code == juce::KeyPress::backspaceKey) result += juce::String(juce::CharPointer_UTF8("\xe2\x8c\xab"));
    else if (code == juce::KeyPress::deleteKey)  result += juce::String(juce::CharPointer_UTF8("\xe2\x8c\xa6"));
    else if (code == juce::KeyPress::tabKey)     result += "Tab";
    else if (code == juce::KeyPress::leftKey)    result += juce::String(juce::CharPointer_UTF8("\xe2\x86\x90"));
    else if (code == juce::KeyPress::rightKey)   result += juce::String(juce::CharPointer_UTF8("\xe2\x86\x92"));
    else if (code == juce::KeyPress::upKey)      result += juce::String(juce::CharPointer_UTF8("\xe2\x86\x91"));
    else if (code == juce::KeyPress::downKey)    result += juce::String(juce::CharPointer_UTF8("\xe2\x86\x93"));
    else {
        auto c = (char)std::toupper(code);
        result += juce::String::charToString(c);
    }
    return result;
}

juce::KeyPress KeyBindingManager::stringToKey(const juce::String& str) {
    // Parse modifier symbols and key name back to KeyPress
    int mods = 0;
    int i = 0;
    auto chars = str.toUTF8();
    juce::String remaining = str;

    if (remaining.startsWith(juce::CharPointer_UTF8("\xe2\x8c\x98"))) {
        mods |= juce::ModifierKeys::commandModifier;
        remaining = remaining.substring(1);  // 1 Unicode character
    }
    if (remaining.startsWith(juce::CharPointer_UTF8("\xe2\x87\xa7"))) {
        mods |= juce::ModifierKeys::shiftModifier;
        remaining = remaining.substring(1);
    }
    if (remaining.startsWith(juce::CharPointer_UTF8("\xe2\x8c\xa5"))) {
        mods |= juce::ModifierKeys::altModifier;
        remaining = remaining.substring(1);
    }
    if (remaining.startsWith(juce::CharPointer_UTF8("\xe2\x8c\x83"))) {
        mods |= juce::ModifierKeys::ctrlModifier;
        remaining = remaining.substring(1);
    }

    int keyCode = 0;
    if (remaining == "Space")        keyCode = juce::KeyPress::spaceKey;
    else if (remaining == "Return")  keyCode = juce::KeyPress::returnKey;
    else if (remaining == "Esc")     keyCode = juce::KeyPress::escapeKey;
    else if (remaining == "Tab")     keyCode = juce::KeyPress::tabKey;
    else if (remaining.length() == 1)
        keyCode = std::tolower(remaining[0]);
    else if (remaining.startsWith(juce::CharPointer_UTF8("\xe2\x8c\xab")))
        keyCode = juce::KeyPress::backspaceKey;

    return juce::KeyPress(keyCode, juce::ModifierKeys(mods), 0);
}
