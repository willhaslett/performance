#include "gui/ActionInstanceForm.h"
#include "gui/MorphEditor.h"
#include "api/StateAPI.h"
#include <algorithm>

ActionInstanceForm::ActionInstanceForm(StateAPI& s, const ActionInfo& a)
    : state(s), action(a) {
    addAndMakeVisible(okButton);
    addAndMakeVisible(cancelButton);

    okButton.onClick     = [this]() { if (onAccept) onAccept(); };
    cancelButton.onClick = [this]() { if (onCancel) onCancel(); };

    buildWidgets();
    refreshValidation();
    setSize(panelWidth, getDesiredHeight());
}

void ActionInstanceForm::buildWidgets() {
    widgets.clear();
    widgets.reserve(action.params.size());

    for (const auto& p : action.params) {
        ParamWidget w;
        w.schema = &p;

        w.label = std::make_unique<juce::Label>();
        w.label->setText(juce::String(p.name) + (p.required ? "" : " (optional)"),
                         juce::dontSendNotification);
        w.label->setColour(juce::Label::textColourId,
                           Theme::color(Theme::Color::textSecondary));
        w.label->setFont(Theme::font(Theme::fontSizeSm));
        addAndMakeVisible(*w.label);

        switch (p.type) {
        case ParamType::ChannelRef:
        case ParamType::PresetRef:
        case ParamType::Enum: {
            w.combo = std::make_unique<juce::ComboBox>();
            w.combo->onChange = [this]() { refreshValidation(); };
            addAndMakeVisible(*w.combo);

            if (p.type == ParamType::ChannelRef)      populateChannelRefCombo(w);
            else if (p.type == ParamType::PresetRef)  populatePresetRefCombo(w);
            else                                       populateEnumCombo(w);
            break;
        }
        case ParamType::Float: {
            w.editor = std::make_unique<juce::TextEditor>();
            w.editor->setInputRestrictions(0, "0123456789.-");
            w.editor->setText(p.defaultValue.empty() ? "" : juce::String(p.defaultValue),
                              juce::dontSendNotification);
            w.editor->onTextChange = [this]() { refreshValidation(); };
            addAndMakeVisible(*w.editor);
            break;
        }
        case ParamType::Morph: {
            w.morphBtn = std::make_unique<juce::TextButton>();
            w.morphBtn->setButtonText("Edit morph...");
            w.morphBtn->onClick = [this, idx = (int)widgets.size()]() {
                auto& wRef = widgets[(size_t)idx];
                auto* editor = new MorphEditor(state);
                if (!wRef.value.isVoid())
                    editor->setMorphData(wRef.value);

                struct MorphWindow : public juce::DocumentWindow {
                    std::function<void()> onClose;
                    MorphWindow() : DocumentWindow("Morph",
                                                    Theme::color(Theme::Color::bgOverlay),
                                                    closeButton) {}
                    void closeButtonPressed() override { if (onClose) onClose(); }
                };
                auto* window = new MorphWindow();
                window->setContentOwned(editor, true);
                window->centreWithSize(editor->getWidth(), editor->getHeight());
                window->setUsingNativeTitleBar(false);
                window->setVisible(true);
                window->setAlwaysOnTop(true);

                editor->onDone = [this, window, editor, idx]() {
                    widgets[(size_t)idx].value = editor->getMorphData();
                    refreshValidation();
                    delete window;
                };
                editor->onCancel = [window]() { delete window; };
                window->onClose  = [window]() { delete window; };
            };
            addAndMakeVisible(*w.morphBtn);
            break;
        }
        }

        widgets.push_back(std::move(w));
    }
}

void ActionInstanceForm::populateChannelRefCombo(ParamWidget& w) {
    auto& p = *w.schema;
    bool any = p.scope.empty();
    auto allows = [&](const std::string& s) {
        return any || std::find(p.scope.begin(), p.scope.end(), s) != p.scope.end();
    };
    bool allowTracks  = allows("track");
    bool allowBusses  = allows("bus");
    bool allowMaster  = allows("master");

    w.combo->clear(juce::dontSendNotification);
    w.comboValues.clear();

    auto addItem = [&](const juce::String& label, juce::var val) {
        int id = (int)w.comboValues.size() + 1;
        w.combo->addItem(label, id);
        w.comboValues.push_back(std::move(val));
    };

    if (allowTracks) {
        for (auto& t : state.listTracks()) {
            auto* ts = state.findTrack(t.id);
            if (!ts || ts->sourceType == TrackSourceType::Action) continue;
            if (!p.sourceTypes.empty()) {
                std::string st;
                switch (ts->sourceType) {
                    case TrackSourceType::Instrument: st = "Instrument"; break;
                    case TrackSourceType::AudioInput: st = "AudioInput"; break;
                    case TrackSourceType::Action:     st = "Action"; break;
                }
                if (std::find(p.sourceTypes.begin(), p.sourceTypes.end(), st)
                    == p.sourceTypes.end()) continue;
            }
            addItem(juce::String(t.name), juce::var(juce::String(t.id.str())));
        }
    }
    if (allowBusses) {
        if (w.combo->getNumItems() > 0) w.combo->addSeparator();
        for (auto& b : state.listBusses())
            addItem(juce::String(b.name), juce::var(juce::String(b.id.str())));
    }
    if (allowMaster) {
        if (w.combo->getNumItems() > 0) w.combo->addSeparator();
        addItem("Main", juce::var("Main"));
    }

    if (w.combo->getNumItems() == 0) {
        w.combo->addItem("(no compatible channels)", -1);
        w.combo->setItemEnabled(-1, false);
        w.envEmpty = true;
    }
}

void ActionInstanceForm::populatePresetRefCombo(ParamWidget& w) {
    w.combo->clear(juce::dontSendNotification);
    w.comboValues.clear();

    bool addedAny = false;
    for (auto& plugin : state.allPlugins()) {
        auto presets = state.presetsForPlugin(plugin.id);
        if (presets.empty()) continue;
        w.combo->addSectionHeading(juce::String(plugin.name));
        for (auto* preset : presets) {
            int id = (int)w.comboValues.size() + 1;
            w.combo->addItem("  " + juce::String(preset->name), id);
            w.comboValues.push_back(juce::var(juce::String(preset->id.str())));
            addedAny = true;
        }
    }
    if (!addedAny) {
        w.combo->addItem("(no presets saved)", -1);
        w.combo->setItemEnabled(-1, false);
        w.envEmpty = true;
    }
}

void ActionInstanceForm::populateEnumCombo(ParamWidget& w) {
    auto& p = *w.schema;
    w.combo->clear(juce::dontSendNotification);
    w.comboValues.clear();

    for (auto& v : p.enumValues) {
        int id = (int)w.comboValues.size() + 1;
        w.combo->addItem(juce::String(v), id);
        w.comboValues.push_back(juce::var(juce::String(v)));
    }
    if (!p.defaultValue.empty()) {
        for (size_t i = 0; i < w.comboValues.size(); ++i) {
            if (w.comboValues[i].toString() == juce::String(p.defaultValue)) {
                w.combo->setSelectedId((int)i + 1, juce::dontSendNotification);
                break;
            }
        }
    }
}

void ActionInstanceForm::selectComboValue(ParamWidget& w, const juce::var& v) {
    for (size_t i = 0; i < w.comboValues.size(); ++i) {
        if (w.comboValues[i].toString() == v.toString()) {
            w.combo->setSelectedId((int)i + 1, juce::dontSendNotification);
            return;
        }
    }
}

void ActionInstanceForm::setInitialArgs(const juce::var& args) {
    if (!args.isArray()) return;
    int n = std::min((int)args.size(), (int)widgets.size());
    for (int i = 0; i < n; ++i) {
        auto& w = widgets[(size_t)i];
        const auto& v = args[i];
        switch (w.schema->type) {
        case ParamType::ChannelRef:
        case ParamType::PresetRef:
        case ParamType::Enum:
            selectComboValue(w, v);
            break;
        case ParamType::Float:
            w.editor->setText(v.toString(), juce::dontSendNotification);
            break;
        case ParamType::Morph:
            w.value = v;
            break;
        }
    }
    refreshValidation();
}

void ActionInstanceForm::refreshValidation() {
    bool allValid = true;
    for (auto& w : widgets) {
        bool ok = true;
        switch (w.schema->type) {
        case ParamType::ChannelRef:
        case ParamType::PresetRef:
        case ParamType::Enum: {
            int id = w.combo->getSelectedId();
            ok = id > 0 && id <= (int)w.comboValues.size();
            break;
        }
        case ParamType::Float: {
            auto txt = w.editor->getText().trim();
            if (txt.isEmpty()) {
                ok = !w.schema->required;
            } else {
                // Strict parse: the whole string must consume cleanly as a number.
                char* end = nullptr;
                double v = std::strtod(txt.toRawUTF8(), &end);
                bool parsedAll = (end != nullptr && *end == '\0');
                if (!parsedAll) ok = false;
                else if (w.schema->minValue && v < *w.schema->minValue) ok = false;
                else if (w.schema->maxValue && v > *w.schema->maxValue) ok = false;
            }
            w.editor->setColour(juce::TextEditor::outlineColourId,
                                ok ? Theme::color(Theme::Color::border)
                                   : Theme::color(Theme::Color::statusError));
            break;
        }
        case ParamType::Morph:
            ok = !w.schema->required || !w.value.isVoid();
            break;
        }
        w.isValid = ok;
        if (w.schema->required && !ok) allValid = false;
    }
    okButton.setEnabled(allValid);
}

juce::var ActionInstanceForm::collectArgs() const {
    juce::var arr;
    for (auto& w : widgets) {
        switch (w.schema->type) {
        case ParamType::ChannelRef:
        case ParamType::PresetRef:
        case ParamType::Enum: {
            int id = w.combo->getSelectedId();
            if (id > 0 && id <= (int)w.comboValues.size())
                arr.append(w.comboValues[(size_t)id - 1]);
            else
                arr.append(juce::var(juce::String()));
            break;
        }
        case ParamType::Float:
            arr.append(juce::var(w.editor->getText().getDoubleValue()));
            break;
        case ParamType::Morph:
            arr.append(w.value);
            break;
        }
    }
    return arr;
}

juce::var ActionInstanceForm::getArgs() const { return collectArgs(); }

bool ActionInstanceForm::valid() const {
    for (auto& w : widgets)
        if (w.schema->required && !w.isValid) return false;
    return true;
}

int ActionInstanceForm::getDesiredHeight() const {
    return headerHeight + (int)widgets.size() * rowHeight + footerHeight;
}

void ActionInstanceForm::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgOverlay));
    g.setColour(Theme::color(Theme::Color::border));
    g.drawRect(getLocalBounds(), 1);

    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeLg));
    auto headerBounds = juce::Rectangle<int>(0, 0, getWidth(), headerHeight);
    g.drawText(juce::String(action.label.empty() ? action.name : action.label),
               headerBounds.reduced(Theme::spacingL, 0),
               juce::Justification::centredLeft);

    g.setColour(Theme::color(Theme::Color::borderSubtle));
    g.drawLine(0.0f, (float)headerHeight, (float)getWidth(), (float)headerHeight, 1.0f);
    int footerY = getHeight() - footerHeight;
    g.drawLine(0.0f, (float)footerY, (float)getWidth(), (float)footerY, 1.0f);
}

void ActionInstanceForm::resized() {
    int y = headerHeight;
    int leftPad = Theme::spacingL;
    int widgetX = leftPad + rowLabelW;
    int widgetW = getWidth() - widgetX - Theme::spacingL;

    for (auto& w : widgets) {
        if (w.label) w.label->setBounds(leftPad, y + (rowHeight - 22) / 2, rowLabelW - 8, 22);
        int wY = y + (rowHeight - 28) / 2;
        if (w.combo)    w.combo->setBounds(widgetX, wY, widgetW, 28);
        if (w.editor)   w.editor->setBounds(widgetX, wY, 120, 28);
        if (w.morphBtn) w.morphBtn->setBounds(widgetX, wY, 140, 28);
        y += rowHeight;
    }

    int btnW = 90;
    int btnY = getHeight() - footerHeight + (footerHeight - 32) / 2;
    cancelButton.setBounds(getWidth() - Theme::spacingL - btnW, btnY, btnW, 32);
    okButton.setBounds(cancelButton.getX() - Theme::spacingS - btnW, btnY, btnW, 32);
}

void ActionInstanceForm::launch(StateAPI& state, const ActionInfo& action,
                                 const juce::var& initialArgs,
                                 std::function<void(const juce::var&)> onComplete) {
    auto* form = new ActionInstanceForm(state, action);
    if (!initialArgs.isVoid()) form->setInitialArgs(initialArgs);

    struct FormWindow : public juce::DocumentWindow {
        std::function<void()> onClose;
        FormWindow() : DocumentWindow("Action",
                                       Theme::color(Theme::Color::bgOverlay),
                                       closeButton) {}
        void closeButtonPressed() override { if (onClose) onClose(); }
    };
    auto* window = new FormWindow();
    window->setContentOwned(form, true);
    window->centreWithSize(form->getWidth(), form->getHeight());
    window->setUsingNativeTitleBar(false);
    window->setVisible(true);
    window->setAlwaysOnTop(true);

    form->onAccept = [form, window, onComplete]() {
        auto args = form->getArgs();
        delete window;
        if (onComplete) onComplete(args);
    };
    form->onCancel = [window, onComplete]() {
        delete window;
        if (onComplete) onComplete(juce::var());
    };
    window->onClose = [window, onComplete]() {
        delete window;
        if (onComplete) onComplete(juce::var());
    };
}
