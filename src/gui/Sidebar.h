#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "gui/RegistryTree.h"
#include "gui/Theme.h"

class StateAPI;
class EngineAPI;

class Sidebar : public juce::Component, private juce::Timer {
public:
    Sidebar();
    ~Sidebar() override;

    void setStateAPI(StateAPI* s);
    void setEngineAPI(EngineAPI* e);

    // Callback for song loading (coordinator-level operation)
    std::function<void(const std::string& songId)> onLoadSong;

    // Callback for MIDI device selection (deviceId set if registered, portName set if unregistered)
    std::function<void(const std::string& deviceId, const std::string& portName)> onDeviceSelected;

    // Callbacks for audio device selection (output and input independently)
    std::function<void(const std::string& deviceName)> onAudioOutputSelected;
    std::function<void(const std::string& deviceName)> onAudioInputSelected;

    // Callbacks for pane selection
    std::function<void()> onDebugSelected;
    std::function<void()> onLogsSelected;
    std::function<void()> onChatSelected;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshTree();

    StateAPI* state = nullptr;
    EngineAPI* engineAPI = nullptr;
    RegistryTree tree;
    int subscriptionId = -1;
    bool needsRefresh = false;
    std::string lastHighlightedId;
    std::string selectedDeviceId;
    int lastMidiDeviceCount = -1;
    juce::String lastAudioOutputName;
    juce::String lastAudioInputName;
};
