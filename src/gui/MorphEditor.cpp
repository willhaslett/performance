#include "gui/MorphEditor.h"
#include "gui/ActionInstanceForm.h"
#include "api/StateAPI.h"

MorphEditor::MorphEditor(StateAPI& state) : state(state) {
    addAndMakeVisible(okButton);
    addAndMakeVisible(cancelButton);
    addAndMakeVisible(modeToggle);

    okButton.onClick = [this]() { if (onDone) onDone(); };
    cancelButton.onClick = [this]() { if (onCancel) onCancel(); };
    modeToggle.onClick = [this]() { sequential = modeToggle.getToggleState(); };

    modeToggle.setColour(juce::ToggleButton::textColourId, Theme::color(Theme::Color::textSecondary));
    modeToggle.setColour(juce::ToggleButton::tickColourId, Theme::color(Theme::Color::accent));

    setSize(panelWidth, getDesiredHeight());
}

void MorphEditor::setMorphData(const juce::var& data) {
    slots.clear();
    sequential = (data.getProperty("mode", "parallel").toString() == "sequential");
    modeToggle.setToggleState(sequential, juce::dontSendNotification);

    auto subActions = data.getProperty("actions", juce::var());
    if (subActions.isArray()) {
        for (int i = 0; i < subActions.size(); ++i) {
            auto sub = subActions[i];
            auto name = sub.getProperty("action", "").toString().toStdString();
            auto args = sub.getProperty("args", juce::var());
            auto argsStr = juce::JSON::toString(args, true).toStdString();

            // Look up label
            std::string label = name;
            for (auto& a : state.allActions())
                if (a.name == name) { label = a.label; break; }

            slots.push_back({ name, label, argsStr, summarizeArgs(name, argsStr).toStdString() });
        }
    }
    setSize(panelWidth, getDesiredHeight());
    repaint();
}

juce::var MorphEditor::getMorphData() const {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("mode", sequential ? "sequential" : "parallel");

    juce::var actionsArray;
    for (auto& slot : slots) {
        auto* sub = new juce::DynamicObject();
        sub->setProperty("action", juce::String(slot.actionName));
        sub->setProperty("args", juce::JSON::parse(juce::String(slot.argsJson)));
        actionsArray.append(sub);
    }
    obj->setProperty("actions", actionsArray);
    return obj;
}

juce::String MorphEditor::summarizeArgs(const std::string& actionName, const std::string& argsJson) {
    auto args = juce::JSON::parse(juce::String(argsJson));
    if (!args.isArray() || args.size() == 0) return "";
    juce::String summary;
    for (int i = 0; i < std::min(args.size(), 3); ++i) {
        if (i > 0) summary += ", ";
        summary += args[i].toString();
    }
    return summary;
}

void MorphEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff2a2a2a));
    g.setColour(juce::Colour(0xff555555));
    g.drawRect(getLocalBounds(), 1);

    // Title
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(14.0f));
    g.drawText("Morph Editor", 0, 8, getWidth(), 20, juce::Justification::centred);

    // Slots
    int y = headerHeight;
    for (int i = 0; i < (int)slots.size(); ++i) {
        auto row = juce::Rectangle<int>(12, y, getWidth() - 24, slotHeight - 2);

        // Slot background
        g.setColour(juce::Colour(0xff363636));
        g.fillRoundedRectangle(row.toFloat(), 4.0f);

        // Action label
        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(12.0f));
        g.drawText(juce::String(slots[i].actionLabel), row.reduced(8, 0).removeFromLeft(140),
                   juce::Justification::centredLeft);

        // Args summary
        if (!slots[i].argsSummary.empty()) {
            g.setColour(Theme::color(Theme::Color::textSecondary));
            g.setFont(Theme::font(10.0f));
            g.drawText(juce::String(slots[i].argsSummary),
                       row.reduced(8, 0).withTrimmedLeft(144),
                       juce::Justification::centredLeft);
        }

        // Delete "x"
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(12.0f));
        g.drawText(juce::CharPointer_UTF8("\xc3\x97"), row.removeFromRight(24),
                   juce::Justification::centred);

        y += slotHeight;
    }

    // Empty slot (add new)
    auto addRow = juce::Rectangle<int>(12, y, getWidth() - 24, slotHeight - 2);
    g.setColour(juce::Colour(0xff2e2e2e));
    g.fillRoundedRectangle(addRow.toFloat(), 4.0f);
    g.setColour(juce::Colour(0xff555555));
    g.drawRoundedRectangle(addRow.toFloat(), 4.0f, 1.0f);
    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(11.0f));
    g.drawText("+ Add action...", addRow, juce::Justification::centred);
}

void MorphEditor::resized() {
    int y = getHeight() - 36;
    int btnW = 80;
    int mid = getWidth() / 2;
    okButton.setBounds(mid - btnW - 8, y, btnW, 28);
    cancelButton.setBounds(mid + 8, y, btnW, 28);
    modeToggle.setBounds(12, headerHeight - 28, 140, 22);
}

void MorphEditor::mouseUp(const juce::MouseEvent& event) {
    int y = headerHeight;

    // Check slot clicks
    for (int i = 0; i < (int)slots.size(); ++i) {
        auto row = juce::Rectangle<int>(12, y, getWidth() - 24, slotHeight - 2);
        if (row.contains(event.getPosition())) {
            // Check delete button
            auto deleteArea = row.removeFromRight(24);
            if (deleteArea.contains(event.getPosition())) {
                slots.erase(slots.begin() + i);
                setSize(panelWidth, getDesiredHeight());
                repaint();
                return;
            }
            // Click on slot → reassign action
            showActionPickerForSlot(i, event.getScreenPosition());
            return;
        }
        y += slotHeight;
    }

    // Check "add" slot
    auto addRow = juce::Rectangle<int>(12, y, getWidth() - 24, slotHeight - 2);
    if (addRow.contains(event.getPosition())) {
        slots.push_back({ "", "...", "[]", "" });
        setSize(panelWidth, getDesiredHeight());
        repaint();
        showActionPickerForSlot((int)slots.size() - 1, event.getScreenPosition());
    }
}

void MorphEditor::showActionPickerForSlot(int slotIndex, juce::Point<int> screenPos) {
    auto actions = state.allActions();
    auto tracks = state.listTracks();

    juce::PopupMenu menu;
    int baseId = 1;

    for (int ai = 0; ai < (int)actions.size(); ++ai) {
        if (actions[ai].name == "morph") continue;  // no nested morphs

        if (actions[ai].params.empty()) {
            menu.addItem(baseId + ai * 1000, juce::String(actions[ai].label));
            continue;
        }

        const auto& firstParam = actions[ai].params[0];
        bool isTrackParam = firstParam.type == ParamType::ChannelRef;
        bool isChannelParam = isTrackParam && (firstParam.scope.empty()
            || std::any_of(firstParam.scope.begin(), firstParam.scope.end(),
                           [](const std::string& s) { return s != "track"; }));

        if (isTrackParam) {
            juce::PopupMenu sub;
            for (int ti = 0; ti < (int)tracks.size(); ++ti) {
                auto* ts = state.findTrack(tracks[ti].id);
                if (ts && ts->sourceType == TrackSourceType::Action) continue;
                sub.addItem(baseId + ai * 1000 + ti + 1, juce::String(tracks[ti].name));
            }
            if (isChannelParam) {
                auto busses = state.listBusses();
                if (!busses.empty()) sub.addSeparator();
                for (int bi = 0; bi < (int)busses.size(); ++bi)
                    sub.addItem(baseId + ai * 1000 + 500 + bi, juce::String(busses[bi].name));
                sub.addSeparator();
                sub.addItem(baseId + ai * 1000 + 999, "Main");
            }
            menu.addSubMenu(juce::String(actions[ai].label), sub);
        } else {
            menu.addItem(baseId + ai * 1000, juce::String(actions[ai].label));
        }
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options()
            .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, slotIndex, actions, tracks, baseId](int result) {
            if (slotIndex >= (int)slots.size()) return;
            if (result <= 0) {
                // Cancelled — remove the slot if it's unassigned
                if (slots[slotIndex].actionName.empty() || slots[slotIndex].actionName == "...") {
                    slots.erase(slots.begin() + slotIndex);
                    setSize(panelWidth, getDesiredHeight());
                    repaint();
                }
                return;
            }
            int encoded = result - baseId;
            int ai = encoded / 1000;
            int sub = encoded % 1000;
            if (ai < 0 || ai >= (int)actions.size()) return;

            slots[slotIndex].actionName = actions[ai].name;
            slots[slotIndex].actionLabel = actions[ai].label;

            juce::String firstArg;
            if (sub == 999) firstArg = "Main";
            else if (sub >= 500) {
                auto busses = state.listBusses();
                int bi = sub - 500;
                if (bi < (int)busses.size()) firstArg = juce::String(busses[bi].id.str());
            } else if (sub > 0) {
                int ti = sub - 1;
                if (ti < (int)tracks.size()) firstArg = juce::String(tracks[ti].id.str());
            }

            juce::var initialArgs;
            if (firstArg.isNotEmpty()) initialArgs.append(firstArg);
            ActionInstanceForm::launch(state, actions[ai], initialArgs,
                [this, slotIndex](const juce::var& formArgs) {
                    if (formArgs.isVoid()) return;
                    if (slotIndex >= (int)slots.size()) return;
                    auto argsJson = juce::JSON::toString(formArgs, true).toStdString();
                    slots[slotIndex].argsJson = argsJson;
                    slots[slotIndex].argsSummary = summarizeArgs(
                        slots[slotIndex].actionName, argsJson).toStdString();
                    repaint();
                });
        });
}
