#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "state/StateModel.h"
#include <string>
#include <vector>
#include <map>

class StateAPI;
class EngineAPI;

class BindingsPane : public juce::Component {
public:
    BindingsPane(StateAPI& state, EngineAPI& engine);
    ~BindingsPane() override;

    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override;

private:
    StateAPI& state;
    EngineAPI& engine;
    int stateSubscriptionId = -1;

    // Flattened row model for display
    struct Row {
        enum Type { DeviceHeader, ControlRow };
        Type rowType;
        // DeviceHeader
        std::string deviceName;
        std::string deviceId;
        // ControlRow
        std::string controlName;
        std::string group;
        std::string controlType;
        int channel = 0;
        int number = 0;
        // Binding (if bound)
        std::string bindingId;      // empty if unbound
        std::string actionName;     // display label
        std::string argsDisplay;    // formatted args
    };
    std::vector<Row> rows;
    int hoveredRow = -1;
    int scrollOffset = 0;

    static constexpr int headerHeight = 44;
    static constexpr int rowHeight = 26;
    static constexpr int deviceRowHeight = 28;

    // Lookup: binding for a given control (type+channel+number+deviceId)
    struct ControlKey {
        std::string type; int channel; int number; std::string deviceId;
        bool operator<(const ControlKey& o) const {
            if (type != o.type) return type < o.type;
            if (channel != o.channel) return channel < o.channel;
            if (number != o.number) return number < o.number;
            return deviceId < o.deviceId;
        }
    };
    std::map<ControlKey, BindingState> bindingMap;

    void buildRows();
    void showActionMenu(int rowIndex, juce::Point<int> screenPos);
    void showArgsPopup(const Row& row, const ActionInfo& action);
    void createOrUpdateBinding(const Row& row, const ActionInfo& action, const juce::var& args);
    void removeBinding(const std::string& bindingId);
    std::string formatArgs(const std::string& argsJson) const;

    juce::Rectangle<int> getRowBounds(int rowIndex) const;
    juce::Rectangle<int> getActionArea(int rowIndex) const;
};
