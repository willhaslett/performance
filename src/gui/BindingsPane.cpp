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

// --- Add Binding Dialog (two-step: action menu → args dialog) ---

struct ControlOption { std::string deviceId; std::string type; int channel; int number; std::string name; };

void BindingsPane::showAddDialog() {
    auto* song = state.currentSong();
    if (!song) return;

    // Build control options
    auto controlData = std::make_shared<std::vector<ControlOption>>();
    for (auto& device : state.allDevices())
        for (auto& ctrl : device.controls)
            controlData->push_back({ device.id, ctrl.controlType, ctrl.channel, ctrl.number, ctrl.name });

    if (controlData->empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
            "No Controls", "Map some controls on a MIDI device first.");
        return;
    }

    // Step 1: Pick control via popup
    juce::PopupMenu controlMenu;
    for (int i = 0; i < (int)controlData->size(); ++i) {
        auto& c = (*controlData)[i];
        auto* device = state.findDevice(c.deviceId);
        auto label = c.name + (device ? " (" + device->name + ")" : "");
        controlMenu.addItem(i + 1, juce::String(label));
    }

    auto songId = song->id;
    controlMenu.showMenuAsync(juce::PopupMenu::Options(),
        [this, controlData, songId](int controlResult) {
            if (controlResult == 0) return;
            int controlIdx = controlResult - 1;
            auto ctrl = (*controlData)[controlIdx];

            // Step 2: Pick action via popup
            juce::PopupMenu actionMenu;
            auto& actions = state.allActions();
            for (int i = 0; i < (int)actions.size(); ++i)
                actionMenu.addItem(i + 1, juce::String(
                    actions[i].label.empty() ? actions[i].name : actions[i].label));

            actionMenu.showMenuAsync(juce::PopupMenu::Options(),
                [this, ctrl, songId](int actionResult) {
                    if (actionResult == 0) return;
                    int actionIdx = actionResult - 1;
                    auto& actions = state.allActions();
                    if (actionIdx >= (int)actions.size()) return;
                    auto action = actions[actionIdx];

                    // Step 3: Show dialog with action-specific arg fields
                    showArgsDialog(ctrl.deviceId, ctrl.type, ctrl.channel, ctrl.number,
                                    ctrl.name, action, songId);
                });
        });
}

void BindingsPane::showArgsDialog(const std::string& deviceId, const std::string& ctrlType,
                                    int channel, int number, const std::string& ctrlName,
                                    const ActionInfo& action, const std::string& songId) {
    auto label = action.label.empty() ? action.name : action.label;
    auto dialog = std::make_shared<juce::AlertWindow>(
        juce::String(label) + " — " + ctrlName, "", juce::MessageBoxIconType::NoIcon);
    dialog->setColour(juce::AlertWindow::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    dialog->setColour(juce::AlertWindow::textColourId, Theme::color(Theme::Color::textPrimary));

    // Parse paramSchema to build appropriate fields
    auto schema = juce::JSON::parse(juce::String(action.paramSchema));
    auto* params = schema.getArray();

    // Track names for dropdowns
    juce::StringArray trackNames;
    for (auto& t : state.listTracks())
        trackNames.add(juce::String(t.name));

    // Easing options
    juce::StringArray easingOptions = { "linear", "easein", "easeout", "cosine", "scurve" };

    // Add fields based on schema
    struct FieldInfo { std::string name; std::string type; };
    auto fields = std::make_shared<std::vector<FieldInfo>>();

    if (params) {
        for (auto& param : *params) {
            auto name = param.getProperty("name", "").toString().toStdString();
            auto type = param.getProperty("type", "string").toString().toStdString();
            fields->push_back({ name, type });

            // Track name params → combo box of current tracks
            if (name.find("track") != std::string::npos || name.find("Track") != std::string::npos) {
                dialog->addComboBox(juce::String(name), trackNames, juce::String(name));
            }
            // Easing → combo box
            else if (name == "easing") {
                dialog->addComboBox("easing", easingOptions, "Easing");
                // Default to cosine
                if (auto* cb = dialog->getComboBoxComponent("easing"))
                    cb->setSelectedItemIndex(3);  // cosine
            }
            // Duration → text editor with default
            else if (name == "duration" || type == "float") {
                dialog->addTextEditor(juce::String(name), "3.0", juce::String(name));
            }
            // Fallback → text editor
            else {
                dialog->addTextEditor(juce::String(name), "", juce::String(name));
            }
        }
    }

    dialog->addButton("Add", 1);
    dialog->addButton("Cancel", 0);

    auto* statePtr = &state;
    auto actionCopy = action;

    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [dialog, statePtr, fields, deviceId, ctrlType, channel, number, ctrlName,
         actionCopy, songId](int result) {
            if (result == 0) return;

            // Build args JSON from field values
            auto argsArray = juce::var(juce::Array<juce::var>());
            for (auto& field : *fields) {
                auto jName = juce::String(field.name);
                bool isTrack = (field.name.find("track") != std::string::npos ||
                                field.name.find("Track") != std::string::npos);
                bool isEasing = (field.name == "easing");

                if (isTrack || isEasing) {
                    if (auto* cb = dialog->getComboBoxComponent(jName))
                        argsArray.append(juce::var(cb->getText()));
                } else {
                    auto text = dialog->getTextEditorContents(jName);
                    if (field.type == "float")
                        argsArray.append(text.getFloatValue());
                    else
                        argsArray.append(juce::var(text));
                }
            }

            auto argsJson = juce::JSON::toString(argsArray, true).toStdString();
            auto desc = ctrlName + " -> " + actionCopy.name;

            statePtr->addBinding(songId, ctrlType, channel, number,
                                  actionCopy.id, argsJson, desc, deviceId);
        }), true);
}
