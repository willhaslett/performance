#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "state/StateModel.h"
#include <string>
#include <vector>

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

private:
    StateAPI& state;
    EngineAPI& engine;
    int stateSubscriptionId = -1;

    struct BindingRow {
        std::string id;
        std::string controlDesc;  // "Pad 6 (note ch10 #41)"
        std::string actionLabel;
        std::string argsDesc;
        std::string description;
        bool isGlobal = false;
    };
    std::vector<BindingRow> rows;
    int hoveredRow = -1;

    juce::TextButton addButton;

    void showAddDialog();
    void showArgsDialog(const std::string& deviceId, const std::string& ctrlType,
                         int channel, int number, const std::string& ctrlName,
                         const ActionInfo& action, const std::string& songId);

    static constexpr int headerHeight = 44;
    static constexpr int rowHeight = 24;
    static constexpr int columnHeaderHeight = 24;

    juce::Rectangle<int> getRowBounds(int rowIndex) const;
    juce::Rectangle<int> getDeleteButtonBounds(int rowIndex) const;

    std::string formatControl(const std::string& type, int channel, int number,
                               const std::string& deviceId) const;
    std::string formatArgs(const std::string& argsJson) const;
};
