#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "gui/RegistryTree.h"
#include "gui/Theme.h"

class StateAPI;
class EngineAPI;
class PerformanceCoordinator;

class Sidebar : public juce::Component, private juce::Timer {
public:
    Sidebar();
    ~Sidebar() override;

    void setStateAPI(StateAPI* s);
    void setEngineAPI(EngineAPI* e);
    void setCoordinator(PerformanceCoordinator* c);

    // Callbacks
    std::function<void(const std::string& songId)> onLoadSong;
    std::function<void(const std::string& songId)> onDeleteSong;
    std::function<void(const std::string& deviceId, const std::string& portName)> onMapSelected;
    std::function<void(const std::string& deviceName)> onAudioOutputSelected;
    std::function<void(const std::string& deviceName)> onAudioInputSelected;

    // Legacy pane callbacks (still used by some paths)
    std::function<void()> onProduceSelected;
    std::function<void()> onDebugSelected;
    std::function<void()> onLogsSelected;
    std::function<void()> onChatSelected;
    std::function<void(const std::string& slot, const std::string& content)> onPaneSelected;
    std::function<std::string(const std::string& slot)> getPaneContent;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum Tab { Songs = 0, Library, Actions, Devices, TabCount };
    Tab activeTab = Songs;

    void timerCallback() override;
    void refreshTree();
    void refreshSongsTab();
    void refreshLibraryTab();
    void refreshActionsTab();
    void refreshDevicesTab();

    StateAPI* state = nullptr;
    EngineAPI* engineAPI = nullptr;
    PerformanceCoordinator* coordinator = nullptr;
    RegistryTree tree;
    int subscriptionId = -1;
    bool needsRefresh = false;
    std::string lastHighlightedId;
    std::string selectedDeviceId;
    int lastMidiDeviceCount = -1;
    juce::String lastAudioOutputName;
    juce::String lastAudioInputName;

    static constexpr int tabBarHeight = 34;
    static constexpr int tabCount = 4;
    juce::Rectangle<int> getTabBounds(int tabIndex) const;
};
