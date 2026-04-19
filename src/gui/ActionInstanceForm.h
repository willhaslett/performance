#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "gui/Theme.h"
#include "state/StateModel.h"
#include <functional>
#include <memory>
#include <vector>

class StateAPI;

// ActionInstanceForm — schema-driven form for creating or editing an action
// instance (an event on an action track, a MIDI binding's action, or a
// sub-action in a compound morph). Reads ActionInfo.params to build typed
// widgets with validation. Replaces the three ad-hoc creation dialogs
// (ProducePane/MorphEditor/SongMappingsPane + showRemainingParamsDialog).
//
// Widget per ParamType:
//   ChannelRef — ComboBox of tracks / busses / "Main" filtered by scope + sourceTypes
//   PresetRef  — ComboBox of presets grouped by plugin
//   Enum       — ComboBox of enumValues, defaultValue pre-selected
//   Float      — TextEditor with min/max validation
//   Morph      — Button launching MorphEditor; stores the compound JSON blob
//
// OK is enabled iff every required param has a valid value.
class ActionInstanceForm : public juce::Component {
public:
    ActionInstanceForm(StateAPI& state, const ActionInfo& action);

    // Restore widget state from a pre-existing args array (edit flow).
    // Must match the shape of action.params.
    void setInitialArgs(const juce::var& args);

    // Return the validated args array (juce::var array of positional values,
    // types interpreted by ParamType — strings for refs/enums, doubles for
    // floats, object for morph).
    juce::var getArgs() const;
    bool      valid() const;

    std::function<void()> onAccept;
    std::function<void()> onCancel;

    void paint(juce::Graphics& g) override;
    void resized() override;

    int getDesiredHeight() const;
    static constexpr int panelWidth = 420;

    // Convenience: launch the form in a themed floating window with OK/Cancel
    // already wired. onComplete receives the args on accept; receives an
    // undefined var on cancel.
    static void launch(StateAPI& state, const ActionInfo& action,
                       const juce::var& initialArgs,
                       std::function<void(const juce::var&)> onComplete);

private:
    StateAPI& state;
    const ActionInfo& action;

    struct ParamWidget {
        const ParamSchema* schema = nullptr;
        std::unique_ptr<juce::Label>       label;
        std::unique_ptr<juce::ComboBox>    combo;     // ChannelRef / PresetRef / Enum
        std::unique_ptr<juce::TextEditor>  editor;    // Float
        std::unique_ptr<juce::TextButton>  morphBtn;  // Morph
        std::vector<juce::var>             comboValues;  // parallel to combo items (id 1..n)
        juce::var                          value;        // morph blob storage
        bool                               isValid = true;
        bool                               envEmpty = false;  // no candidates in env
    };
    std::vector<ParamWidget> widgets;

    juce::TextButton okButton     { "OK" };
    juce::TextButton cancelButton { "Cancel" };

    static constexpr int rowHeight    = 52;
    static constexpr int headerHeight = 48;
    static constexpr int footerHeight = 56;
    static constexpr int rowLabelW    = 110;

    void buildWidgets();
    void refreshValidation();
    juce::var collectArgs() const;

    void populateChannelRefCombo(ParamWidget& w);
    void populatePresetRefCombo(ParamWidget& w);
    void populateEnumCombo(ParamWidget& w);
    void selectComboValue(ParamWidget& w, const juce::var& v);
};
