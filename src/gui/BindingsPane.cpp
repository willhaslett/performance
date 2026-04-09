#include "gui/BindingsPane.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

BindingsPane::BindingsPane(StateAPI& state, EngineAPI& engine)
    : state(state), engine(engine) {

    stateSubscriptionId = state.events().subscribe([this](const StateEvent& event) {
        if (event.entity == StateEvent::Song || event.entity == StateEvent::Config
            || event.entity == StateEvent::Binding || event.entity == StateEvent::Device) {
            juce::MessageManager::callAsync([this] { refresh(); });
        }
    });

    refresh();
}

BindingsPane::~BindingsPane() {
    if (stateSubscriptionId >= 0)
        state.events().unsubscribe(stateSubscriptionId);
}

std::string BindingsPane::formatArgs(const std::string& argsJson) const {
    auto parsed = juce::JSON::parse(juce::String(argsJson));
    if (auto* arr = parsed.getArray()) {
        juce::StringArray parts;
        for (auto& v : *arr) {
            auto s = v.toString();
            // If it looks like a UUID, try to resolve to track name for display
            if (s.length() == 32 && s.containsOnly("0123456789abcdef")) {
                auto* track = state.findTrack(s.toStdString());
                if (track) { parts.add(juce::String(track->name)); continue; }
            }
            parts.add(s);
        }
        return parts.joinIntoString(", ").toStdString();
    }
    return "";
}

void BindingsPane::buildRows() {
    rows.clear();

    // Build binding lookup from effective bindings
    bindingMap.clear();
    for (auto& b : state.effectiveBindings())
        bindingMap[{ b.controlType, b.channel, b.number, b.deviceId }] = b;

    // For each registered device, list its controls
    for (auto& device : state.allDevices()) {
        Row header;
        header.rowType = Row::DeviceHeader;
        header.deviceName = device.name;
        header.deviceId = device.id;
        rows.push_back(header);

        for (auto& ctrl : device.controls) {
            Row row;
            row.rowType = Row::ControlRow;
            row.controlName = ctrl.name;
            row.group = ctrl.group;
            row.controlType = ctrl.controlType;
            row.channel = ctrl.channel;
            row.number = ctrl.number;
            row.deviceId = device.id;

            // Check if there's a binding for this control
            auto it = bindingMap.find({ ctrl.controlType, ctrl.channel, ctrl.number, device.id });
            if (it != bindingMap.end()) {
                row.bindingId = it->second.id;
                auto* action = state.findActionById(it->second.actionId);
                row.actionName = action ? (action->label.empty() ? action->name : action->label) : "?";
                row.argsDisplay = formatArgs(it->second.args);
            }

            rows.push_back(row);
        }
    }
}

void BindingsPane::refresh() {
    buildRows();
    repaint();
}

juce::Rectangle<int> BindingsPane::getRowBounds(int rowIndex) const {
    int y = headerHeight;
    for (int i = 0; i < rowIndex && i < (int)rows.size(); ++i)
        y += (rows[i].rowType == Row::DeviceHeader) ? deviceRowHeight : rowHeight;
    y -= scrollOffset;
    int h = (rowIndex < (int)rows.size() && rows[rowIndex].rowType == Row::DeviceHeader)
            ? deviceRowHeight : rowHeight;
    return juce::Rectangle<int>(0, y, getWidth(), h);
}

juce::Rectangle<int> BindingsPane::getActionArea(int rowIndex) const {
    auto row = getRowBounds(rowIndex);
    return row.withLeft(280).withRight(row.getRight() - 8);
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

    auto* song = state.currentSong();
    if (song) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeXs));
        g.drawText(juce::String(song->name), headerArea.reduced(12, 0).withTrimmedTop(24),
                   juce::Justification::centredLeft);
    }

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)headerHeight, (float)getWidth(), (float)headerHeight, 1.0f);

    if (rows.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSize));
        g.drawText("No devices with mapped controls",
                   getLocalBounds().withTrimmedTop(headerHeight), juce::Justification::centred);
        return;
    }

    // Rows
    g.reduceClipRegion(getLocalBounds().withTrimmedTop(headerHeight));

    for (int i = 0; i < (int)rows.size(); ++i) {
        auto bounds = getRowBounds(i);
        if (bounds.getBottom() < headerHeight) continue;
        if (bounds.getY() > getHeight()) break;

        auto& row = rows[i];

        if (row.rowType == Row::DeviceHeader) {
            g.setColour(Theme::color(Theme::Color::bgSlot));
            g.fillRect(bounds);
            g.setColour(Theme::color(Theme::Color::textWhite));
            g.setFont(Theme::font(Theme::fontSizeSm));
            g.drawText(juce::String(row.deviceName), bounds.reduced(12, 0),
                       juce::Justification::centredLeft);
        } else {
            // Alternating background
            if (i % 2 == 0) {
                g.setColour(Theme::color(Theme::Color::bgPanel));
                g.fillRect(bounds);
            }
            if (i == hoveredRow) {
                g.setColour(Theme::color(Theme::Color::bgSlotHover));
                g.fillRect(bounds);
            }

            // Control name + group
            g.setFont(Theme::font(Theme::fontSizeSm));
            g.setColour(Theme::color(Theme::Color::textPrimary));
            g.drawText(juce::String(row.controlName), bounds.getX() + 20, bounds.getY(),
                       160, bounds.getHeight(), juce::Justification::centredLeft);

            if (!row.group.empty()) {
                g.setColour(Theme::color(Theme::Color::textDim));
                g.setFont(Theme::font(Theme::fontSizeXs));
                g.drawText(juce::String(row.group), bounds.getX() + 180, bounds.getY(),
                           90, bounds.getHeight(), juce::Justification::centredLeft);
            }

            // Action area
            auto actionArea = getActionArea(i);
            if (row.bindingId.empty()) {
                // Unbound — show placeholder
                g.setColour(Theme::color(Theme::Color::textDim));
                g.setFont(Theme::font(Theme::fontSizeXs));
                g.drawText("-- assign --", actionArea, juce::Justification::centredLeft);
            } else {
                // Bound — show action + args
                g.setColour(Theme::color(Theme::Color::instrument));
                g.setFont(Theme::font(Theme::fontSizeSm));
                g.drawText(juce::String(row.actionName), actionArea.removeFromLeft(120),
                           juce::Justification::centredLeft);

                if (!row.argsDisplay.empty()) {
                    g.setColour(Theme::color(Theme::Color::textSecondary));
                    g.setFont(Theme::font(Theme::fontSizeXs));
                    g.drawText(juce::String(row.argsDisplay), actionArea,
                               juce::Justification::centredLeft);
                }
            }
        }
    }
}

void BindingsPane::resized() {}

void BindingsPane::mouseUp(const juce::MouseEvent& event) {
    for (int i = 0; i < (int)rows.size(); ++i) {
        auto bounds = getRowBounds(i);
        if (!bounds.contains(event.getPosition())) continue;
        if (rows[i].rowType != Row::ControlRow) continue;

        auto actionArea = getActionArea(i);
        if (actionArea.contains(event.getPosition()) || event.getPosition().getX() > 180) {
            showActionMenu(i, event.getScreenPosition());
            return;
        }
    }
}

void BindingsPane::mouseMove(const juce::MouseEvent& event) {
    int newHovered = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        if (rows[i].rowType == Row::ControlRow && getRowBounds(i).contains(event.getPosition())) {
            newHovered = i;
            break;
        }
    }
    if (newHovered != hoveredRow) {
        hoveredRow = newHovered;
        repaint();
    }
}

void BindingsPane::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    // Calculate total content height
    int totalHeight = 0;
    for (auto& r : rows)
        totalHeight += (r.rowType == Row::DeviceHeader) ? deviceRowHeight : rowHeight;
    int viewHeight = getHeight() - headerHeight;
    int maxScroll = std::max(0, totalHeight - viewHeight);
    scrollOffset = juce::jlimit(0, maxScroll, scrollOffset - (int)(wheel.deltaY * 40));
    repaint();
}

// --- Action menu and binding creation ---

void BindingsPane::showActionMenu(int rowIndex, juce::Point<int> screenPos) {
    auto& row = rows[rowIndex];

    juce::PopupMenu menu;
    auto& actions = state.allActions();

    // "None" to clear binding
    menu.addItem(1, "-- none --", true, row.bindingId.empty());

    for (int i = 0; i < (int)actions.size(); ++i) {
        auto label = actions[i].label.empty() ? actions[i].name : actions[i].label;
        auto* bound = row.bindingId.empty() ? nullptr : state.findActionById(
            bindingMap[{ row.controlType, row.channel, row.number, row.deviceId }].actionId);
        bool isCurrent = (bound && bound->id == actions[i].id);
        menu.addItem(i + 2, juce::String(label), true, isCurrent);
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
        .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, rowIndex](int result) {
            if (result == 0) return;  // dismissed
            auto& row = rows[rowIndex];
            if (result == 1) {
                // Clear binding
                if (!row.bindingId.empty()) {
                    removeBinding(row.bindingId);
                }
                return;
            }

            int actionIdx = result - 2;
            auto& actions = state.allActions();
            if (actionIdx < 0 || actionIdx >= (int)actions.size()) return;
            auto action = actions[actionIdx];

            // If action has no params, create binding immediately
            auto schema = juce::JSON::parse(juce::String(action.paramSchema));
            auto* params = schema.getArray();
            if (!params || params->isEmpty()) {
                auto args = juce::var(juce::Array<juce::var>());
                createOrUpdateBinding(row, action, args);
            } else {
                showArgsPopup(row, action);
            }
        });
}

void BindingsPane::showArgsPopup(const Row& row, const ActionInfo& action) {
    auto label = action.label.empty() ? action.name : action.label;
    auto dialog = std::make_shared<juce::AlertWindow>(
        juce::String(label), "Configure for " + juce::String(row.controlName),
        juce::MessageBoxIconType::NoIcon);
    dialog->setColour(juce::AlertWindow::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    dialog->setColour(juce::AlertWindow::textColourId, Theme::color(Theme::Color::textPrimary));

    auto schema = juce::JSON::parse(juce::String(action.paramSchema));
    auto* params = schema.getArray();

    juce::StringArray trackNames;
    for (auto& t : state.listTracks())
        trackNames.add(juce::String(t.name));

    juce::StringArray easingOptions = { "linear", "easein", "easeout", "cosine", "scurve" };

    struct FieldInfo { std::string name; std::string type; };
    auto fields = std::make_shared<std::vector<FieldInfo>>();

    if (params) {
        for (auto& param : *params) {
            auto name = param.getProperty("name", "").toString().toStdString();
            auto type = param.getProperty("type", "string").toString().toStdString();
            fields->push_back({ name, type });

            if (name.find("track") != std::string::npos || name.find("Track") != std::string::npos) {
                dialog->addComboBox(juce::String(name), trackNames, juce::String(name));
            } else if (name == "easing") {
                dialog->addComboBox("easing", easingOptions, "Easing");
                if (auto* cb = dialog->getComboBoxComponent("easing"))
                    cb->setSelectedItemIndex(3);
            } else if (name == "duration" || type == "float") {
                dialog->addTextEditor(juce::String(name), "3.0", juce::String(name));
            } else {
                dialog->addTextEditor(juce::String(name), "", juce::String(name));
            }
        }
    }

    dialog->addButton("OK", 1);
    dialog->addButton("Cancel", 0);

    auto rowCopy = row;
    auto actionCopy = action;
    auto* statePtr = &state;

    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [dialog, statePtr, fields, rowCopy, actionCopy](int result) {
            if (result == 0) return;

            auto argsArray = juce::var(juce::Array<juce::var>());
            for (auto& field : *fields) {
                auto jName = juce::String(field.name);
                bool isTrack = (field.name.find("track") != std::string::npos ||
                                field.name.find("Track") != std::string::npos);
                bool isEasing = (field.name == "easing");

                if (isTrack) {
                    if (auto* cb = dialog->getComboBoxComponent(jName)) {
                        // Resolve track name to UUID
                        auto trackName = cb->getText();
                        juce::String trackId;
                        for (auto& t : statePtr->listTracks())
                            if (juce::String(t.name) == trackName) { trackId = juce::String(t.id); break; }
                        argsArray.append(juce::var(trackId.isNotEmpty() ? trackId : trackName));
                    }
                } else if (isEasing) {
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

            // Remove existing binding if any
            if (!rowCopy.bindingId.empty())
                statePtr->removeBinding(rowCopy.bindingId);

            auto* song = statePtr->currentSong();
            if (!song) return;

            auto argsJson = juce::JSON::toString(argsArray, true).toStdString();
            auto desc = rowCopy.controlName + " -> " + actionCopy.name;

            statePtr->addBinding(song->id, rowCopy.controlType, rowCopy.channel,
                                  rowCopy.number, actionCopy.id, argsJson, desc,
                                  rowCopy.deviceId);
        }), false);
}

void BindingsPane::createOrUpdateBinding(const Row& row, const ActionInfo& action, const juce::var& args) {
    // Remove existing binding if any
    if (!row.bindingId.empty())
        removeBinding(row.bindingId);

    auto* song = state.currentSong();
    if (!song) return;

    auto argsJson = juce::JSON::toString(args, true).toStdString();
    auto desc = row.controlName + " -> " + action.name;

    state.addBinding(song->id, row.controlType, row.channel, row.number,
                      action.id, argsJson, desc, row.deviceId);
}

void BindingsPane::removeBinding(const std::string& bindingId) {
    state.removeBinding(bindingId);
}
