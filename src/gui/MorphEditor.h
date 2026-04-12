#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "state/StateModel.h"
#include <vector>
#include <functional>

class StateAPI;

// MorphEditor — slot-based editor for compound morph actions.
// Shows a growing list of action slots, each clickable to assign an action.

// Show a dialog for action params after the first (track/channel) has been selected.
void showRemainingParamsDialog(const std::string& paramSchema, const juce::String& firstArg,
                                std::function<void(const std::string&)> onComplete);

class MorphEditor : public juce::Component {
public:
    MorphEditor(StateAPI& state);

    void setMorphData(const juce::var& data);  // load existing morph JSON
    juce::var getMorphData() const;             // build morph JSON from slots

    std::function<void()> onDone;    // called on OK
    std::function<void()> onCancel;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    StateAPI& state;

    struct ActionSlot {
        std::string actionName;   // e.g. "fadeOut"
        std::string actionLabel;  // e.g. "Fade out"
        std::string argsJson;     // e.g. "[\"Piano\", 3.0]"
        std::string argsSummary;  // display string e.g. "Piano, 3.0s"
    };
    std::vector<ActionSlot> slots;
    bool sequential = false;

    void showActionPickerForSlot(int slotIndex, juce::Point<int> screenPos);
    juce::String summarizeArgs(const std::string& actionName, const std::string& argsJson);

    // Layout
    static constexpr int slotHeight = 36;
    static constexpr int headerHeight = 60;
    static constexpr int footerHeight = 56;
    static constexpr int panelWidth = 380;

    int getDesiredHeight() const {
        return headerHeight + std::max(1, (int)slots.size() + 1) * slotHeight + footerHeight + 24;
    }

    juce::TextButton okButton { "OK" };
    juce::TextButton cancelButton { "Cancel" };
    juce::ToggleButton modeToggle { "Sequential" };
};
