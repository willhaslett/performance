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

    // True iff every required param of `a` has at least one valid candidate
    // in the current state (e.g. fadeOut requires at least one track).
    static bool actionCanInstantiate(const ActionInfo& a, const StateAPI& state);

    // Convert a camelCase or snake_case identifier to Title Case for display.
    // "fromTrack" -> "From Track", "presetA" -> "Preset A", "easing" -> "Easing".
    static juce::String humanizeLabel(const std::string& s);

private:
    StateAPI& state;
    ActionInfo action;  // by value — outlives any caller-side vector

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

    static constexpr int rowHeight       = 52;
    static constexpr int headerHeight    = 48;
    static constexpr int footerHeight    = 56;
    static constexpr int paramsBottomPad = 16;  // breathing room between last row and footer divider
    static constexpr int rowLabelW       = 110;

    void buildWidgets();
    void refreshValidation();
    juce::var collectArgs() const;

    void populateChannelRefCombo(ParamWidget& w);
    void populatePresetRefCombo(ParamWidget& w);
    void populateEnumCombo(ParamWidget& w);
    void selectComboValue(ParamWidget& w, const juce::var& v);
};
