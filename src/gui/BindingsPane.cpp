#include "gui/BindingsPane.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

BindingsPane::BindingsPane(StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine) {

    addButton.setButtonText("+");
    addButton.setColour(juce::TextButton::buttonColourId, Theme::color(Theme::Color::accent));
    addButton.setColour(juce::TextButton::textColourOnId, Theme::color(Theme::Color::textWhite));
    addButton.setColour(juce::TextButton::textColourOffId, Theme::color(Theme::Color::textWhite));
    addButton.onClick = [this]() { showAddDialog(); };
    addAndMakeVisible(addButton);

    stateSubscriptionId = state.events().subscribe([this](const StateEvent& event) {
        if (event.entity == StateEvent::Song || event.entity == StateEvent::Config) {
            juce::MessageManager::callAsync([this] { refresh(); });
        }
    });

    refresh();
}

BindingsPane::~BindingsPane() {
    if (stateSubscriptionId >= 0)
        state.events().unsubscribe(stateSubscriptionId);
}

std::string BindingsPane::formatControl(const std::string& type, int channel, int number,
                                         const std::string& deviceId) const {
    // Try to find a named control from the device map
    if (!deviceId.empty()) {
        auto* device = state.findDevice(deviceId);
        if (device) {
            for (auto& ctrl : device->controls) {
                if (ctrl.controlType == type && ctrl.channel == channel && ctrl.number == number)
                    return ctrl.name + " (" + device->name + ")";
            }
        }
    }
    // Fallback: raw MIDI description
    std::string desc = type + " ch" + std::to_string(channel);
    if (type == "cc" || type == "note")
        desc += " #" + std::to_string(number);
    return desc;
}

std::string BindingsPane::formatArgs(const std::string& argsJson) const {
    auto parsed = juce::JSON::parse(juce::String(argsJson));
    if (auto* arr = parsed.getArray()) {
        juce::StringArray parts;
        for (auto& v : *arr)
            parts.add(v.toString());
        return parts.joinIntoString(", ").toStdString();
    }
    return argsJson;
}

void BindingsPane::refresh() {
    rows.clear();
    auto bindings = state.effectiveBindings();
    for (auto& b : bindings) {
        BindingRow row;
        row.id = b.id;
        row.controlDesc = formatControl(b.controlType, b.channel, b.number, b.deviceId);
        auto* action = state.findActionById(b.actionId);
        row.actionLabel = action ? (action->label.empty() ? action->name : action->label) : "(unknown)";
        row.argsDesc = formatArgs(b.args);
        row.description = b.description;
        row.isGlobal = b.songId.empty();
        rows.push_back(row);
    }
    repaint();
}

juce::Rectangle<int> BindingsPane::getRowBounds(int rowIndex) const {
    int y = headerHeight + columnHeaderHeight + rowIndex * rowHeight;
    return juce::Rectangle<int>(0, y, getWidth(), rowHeight);
}

juce::Rectangle<int> BindingsPane::getDeleteButtonBounds(int rowIndex) const {
    auto row = getRowBounds(rowIndex);
    return juce::Rectangle<int>(row.getRight() - 28, row.getY() + 2, 20, rowHeight - 4);
}

void BindingsPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    // Header
    auto headerArea = getLocalBounds().removeFromTop(headerHeight);
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(headerArea);
    g.setColour(Theme::color(Theme::Color::textWhite));
    g.setFont(Theme::font(18.0f));
    g.drawText("Bindings", headerArea.reduced(12, 0), juce::Justification::centredLeft);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)headerHeight, (float)getWidth(), (float)headerHeight, 1.0f);

    // Song context
    auto* song = state.currentSong();
    if (song) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeXs));
        g.drawText(juce::String(song->name), headerArea.reduced(12, 0).withTrimmedTop(24),
                   juce::Justification::centredLeft);
    }

    // Column headers
    int colY = headerHeight;
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRect(0, colY, getWidth(), columnHeaderHeight);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeXs));
    g.drawText("Control", 12, colY, 150, columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("Action",  170, colY, 100, columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("Args",    278, colY, 150, columnHeaderHeight, juce::Justification::centredLeft);

    // Rows
    if (rows.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSize));
        g.drawText("No bindings. Click + to add.",
                   getLocalBounds().withTrimmedTop(headerHeight + columnHeaderHeight),
                   juce::Justification::centred);
    } else {
        for (int i = 0; i < (int)rows.size(); ++i) {
            auto rowBounds = getRowBounds(i);
            if (rowBounds.getBottom() < headerHeight + columnHeaderHeight) continue;
            if (rowBounds.getY() > getHeight()) break;

            if (i % 2 == 1) {
                g.setColour(Theme::color(Theme::Color::bgPanel));
                g.fillRect(rowBounds);
            }
            if (i == hoveredRow) {
                g.setColour(Theme::color(Theme::Color::bgSlotHover));
                g.fillRect(rowBounds);
            }

            auto& row = rows[i];
            g.setFont(Theme::font(Theme::fontSizeSm));

            g.setColour(Theme::color(Theme::Color::textPrimary));
            g.drawText(juce::String(row.controlDesc), 12, rowBounds.getY(), 150, rowHeight,
                       juce::Justification::centredLeft);

            g.setColour(Theme::color(Theme::Color::instrument));
            g.drawText(juce::String(row.actionLabel), 170, rowBounds.getY(), 100, rowHeight,
                       juce::Justification::centredLeft);

            g.setColour(Theme::color(Theme::Color::textSecondary));
            g.drawText(juce::String(row.argsDesc), 278, rowBounds.getY(), 150, rowHeight,
                       juce::Justification::centredLeft);

            // Scope indicator
            if (row.isGlobal) {
                g.setColour(Theme::color(Theme::Color::textDim));
                g.setFont(Theme::font(9.0f));
                g.drawText("G", rowBounds.getRight() - 44, rowBounds.getY(), 12, rowHeight,
                           juce::Justification::centred);
            }

            // Delete button on hover
            if (i == hoveredRow) {
                auto delBounds = getDeleteButtonBounds(i);
                g.setColour(Theme::color(Theme::Color::textDim));
                g.setFont(Theme::font(Theme::fontSizeSm));
                g.drawText("x", delBounds, juce::Justification::centred);
            }
        }
    }
}

void BindingsPane::resized() {
    addButton.setBounds(getWidth() - 36, 10, 24, 24);
}

void BindingsPane::mouseUp(const juce::MouseEvent& event) {
    // Delete button
    for (int i = 0; i < (int)rows.size(); ++i) {
        auto delBounds = getDeleteButtonBounds(i);
        if (delBounds.expanded(4).contains(event.getPosition())) {
            state.removeBinding(rows[i].id);
            refresh();
            return;
        }
    }
}

void BindingsPane::mouseMove(const juce::MouseEvent& event) {
    int newHovered = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        if (getRowBounds(i).contains(event.getPosition())) {
            newHovered = i;
            break;
        }
    }
    if (newHovered != hoveredRow) {
        hoveredRow = newHovered;
        repaint();
    }
}

// --- Add Binding Dialog ---

void BindingsPane::showAddDialog() {
    auto* song = state.currentSong();
    if (!song) return;

    auto dialog = std::make_shared<juce::AlertWindow>("Add Binding", "", juce::MessageBoxIconType::NoIcon);
    dialog->setColour(juce::AlertWindow::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    dialog->setColour(juce::AlertWindow::textColourId, Theme::color(Theme::Color::textPrimary));

    // Build control options from all registered devices
    juce::StringArray controlOptions;
    struct ControlOption { std::string deviceId; std::string type; int channel; int number; std::string name; };
    auto controlData = std::make_shared<std::vector<ControlOption>>();

    for (auto& device : state.allDevices()) {
        for (auto& ctrl : device.controls) {
            controlOptions.add(juce::String(ctrl.name) + " (" + device.name + ")");
            controlData->push_back({ device.id, ctrl.controlType, ctrl.channel, ctrl.number, ctrl.name });
        }
    }

    if (controlOptions.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            "No Controls", "Map some controls on a MIDI device first.");
        return;
    }

    // Build action options
    juce::StringArray actionOptions;
    auto& actions = state.allActions();
    for (auto& action : actions)
        actionOptions.add(juce::String(action.label.empty() ? action.name : action.label));

    dialog->addComboBox("control", controlOptions, "Control");
    dialog->addComboBox("action", actionOptions, "Action");

    // Dynamic arg fields — we'll add text fields for the most common pattern
    // For now: one "Arguments" text field where user types comma-separated values
    // The action's paramSchema tells us what to expect
    dialog->addTextEditor("args", "", "Arguments (comma-separated)");
    dialog->addTextEditor("desc", "", "Description (optional)");

    dialog->addButton("Add", 1);
    dialog->addButton("Cancel", 0);

    auto songId = song->id;
    auto* statePtr = &state;
    auto actionsRef = std::make_shared<std::vector<ActionInfo>>(actions);

    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [dialog, statePtr, controlData, actionsRef, songId](int result) {
            if (result == 0) return;

            int controlIdx = dialog->getComboBoxComponent("control")->getSelectedItemIndex();
            int actionIdx = dialog->getComboBoxComponent("action")->getSelectedItemIndex();
            auto argsText = dialog->getTextEditorContents("args");
            auto descText = dialog->getTextEditorContents("desc");

            if (controlIdx < 0 || actionIdx < 0) return;
            if (controlIdx >= (int)controlData->size()) return;
            if (actionIdx >= (int)actionsRef->size()) return;

            auto& ctrl = (*controlData)[controlIdx];
            auto& action = (*actionsRef)[actionIdx];

            // Build args JSON array from comma-separated text
            juce::var argsArray;
            if (argsText.isNotEmpty()) {
                auto parts = juce::StringArray::fromTokens(argsText, ",", "\"");
                for (auto& part : parts) {
                    auto trimmed = part.trim();
                    // Try to parse as number
                    if (trimmed.containsOnly("0123456789.")) {
                        argsArray.append(trimmed.getFloatValue());
                    } else {
                        argsArray.append(juce::var(trimmed));
                    }
                }
            }
            auto argsJson = juce::JSON::toString(argsArray, true).toStdString();

            auto desc = descText.isEmpty()
                ? ctrl.name + " -> " + action.name
                : descText.toStdString();

            statePtr->addBinding(songId, ctrl.type, ctrl.channel, ctrl.number,
                                  action.id, argsJson, desc, ctrl.deviceId);
        }), true);
}
