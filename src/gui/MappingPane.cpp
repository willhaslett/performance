#include "gui/MappingPane.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

MappingPane::MappingPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator)
    : state(state), engine(engine), coordinator(coordinator) {

    deviceNameLabel.setFont(Theme::font(18.0f));
    deviceNameLabel.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textWhite));
    deviceNameLabel.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(deviceNameLabel);

    portNameLabel.setFont(Theme::font(Theme::fontSizeXs));
    portNameLabel.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textDim));
    portNameLabel.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(portNameLabel);

    learnButton.setButtonText("Learn mappings");
    learnButton.setColour(juce::TextButton::buttonColourId, Theme::color(Theme::Color::accent));
    learnButton.setColour(juce::TextButton::textColourOnId, Theme::color(Theme::Color::textWhite));
    learnButton.setColour(juce::TextButton::textColourOffId, Theme::color(Theme::Color::textWhite));
    learnButton.onClick = [this]() { isLearning ? cancelLearn() : startLearn(); };
    addChildComponent(learnButton);

    midiLabelPrefix.setText("Incoming MIDI:", juce::dontSendNotification);
    midiLabelPrefix.setFont(Theme::font(Theme::fontSizeXs));
    midiLabelPrefix.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textDim));
    addChildComponent(midiLabelPrefix);

    midiEventLabel.setFont(Theme::font(Theme::fontSizeXs));
    midiEventLabel.setColour(juce::Label::textColourId, juce::Colour(0xff44ff44));
    addChildComponent(midiEventLabel);

    stateSubscriptionId = state.events().subscribe([this](const StateEvent& event) {
        if (event.entity == StateEvent::Device || event.entity == StateEvent::Binding
            || event.entity == StateEvent::Song) {
            juce::MessageManager::callAsync([this] { refresh(); });
        }
    });

    startTimerHz(10);
}

MappingPane::~MappingPane() {
    if (stateSubscriptionId >= 0)
        state.events().unsubscribe(stateSubscriptionId);
    if (isLearning) cancelLearn();
    coordinator.clearMidiDeviceMonitor();
}

void MappingPane::setDevice(const std::string& deviceId, const std::string& portName) {
    if (isLearning) cancelLearn();
    coordinator.clearMidiDeviceMonitor();

    // Auto-register unregistered devices
    if (deviceId.empty() && !portName.empty()) {
        currentDeviceId = state.registerDevice(portName, portName);
        coordinator.refreshMidiDevices();
    } else {
        currentDeviceId = deviceId;
    }

    auto* device = state.findDevice(currentDeviceId);
    if (device) {
        auto* song = state.currentSong();
        auto title = device->name + (song ? " — " + song->name : "");
        deviceNameLabel.setText(juce::String(title), juce::dontSendNotification);
        portNameLabel.setText(juce::String(device->midiPortName), juce::dontSendNotification);
        deviceNameLabel.setVisible(true);
        portNameLabel.setVisible(true);
        learnButton.setVisible(true);
        midiLabelPrefix.setVisible(true);
        midiEventLabel.setVisible(true);
    }

    // Set up MIDI monitor for this device
    coordinator.setMidiDeviceMonitor(currentDeviceId,
        [this](const std::string& desc, const std::string& type, int ch, int num) {
            juce::MessageManager::callAsync([this, desc, type, ch, num] {
                onMidiEvent(desc, type, ch, num);
            });
        });

    refresh();
    resized();
}

void MappingPane::onMidiEvent(const std::string& description,
                               const std::string& type, int channel, int number) {
    lastEvent2 = lastEvent1;
    lastEvent1 = description;
    auto display = lastEvent1 + (lastEvent2.empty() ? "" : "  |  " + lastEvent2);
    midiEventLabel.setText(juce::String(display), juce::dontSendNotification);

    // Update per-control activity
    auto now = juce::Time::currentTimeMillis();
    for (auto& row : rows) {
        if (row.section != Row::GlobalControl && row.section != Row::SongControl) continue;
        if (row.controlType == type && row.channel == channel && row.number == number)
            row.lastActivityMs = now;
    }
}

// --- Build rows ---

std::string MappingPane::formatArgs(const std::string& argsJson) const {
    auto parsed = juce::JSON::parse(juce::String(argsJson));
    if (auto* arr = parsed.getArray()) {
        juce::StringArray parts;
        for (auto& v : *arr) {
            auto s = v.toString();
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

std::set<std::string> MappingPane::getExistingGroups() const {
    std::set<std::string> groups;
    auto* device = state.findDevice(currentDeviceId);
    if (device)
        for (auto& ctrl : device->controls)
            if (!ctrl.group.empty()) groups.insert(ctrl.group);
    return groups;
}

void MappingPane::buildRows() {
    rows.clear();
    auto* device = state.findDevice(currentDeviceId);
    if (!device) return;

    // Build binding lookup (song-scoped only)
    songBindingMap.clear();
    auto* song = state.currentSong();
    if (song) {
        for (auto& b : song->bindings)
            if (b.deviceId == currentDeviceId)
                songBindingMap[{ b.controlType, b.channel, b.number, b.deviceId }] = b;
    }

    // Song section — ALL bindings (score steps get an indicator, not removed)
    if (song) {
        Row header;
        header.section = Row::SongHeader;
        rows.push_back(header);

        for (int i = 0; i < (int)device->controls.size(); ++i) {
            auto& ctrl = device->controls[i];
            Row row;
            row.section = Row::SongControl;
            row.controlName = ctrl.name;
            row.group = ctrl.group;
            row.controlType = ctrl.controlType;
            row.channel = ctrl.channel;
            row.number = ctrl.number;
            row.controlIndex = i;

            auto it = songBindingMap.find({ ctrl.controlType, ctrl.channel, ctrl.number, currentDeviceId });
            if (it != songBindingMap.end()) {
                row.bindingId = it->second.id;
                auto* action = state.findActionById(it->second.actionId);
                row.actionName = action ? (action->label.empty() ? action->name : action->label) : "?";
                row.argsDisplay = formatArgs(it->second.args);
                row.scorePosition = it->second.isScoreStep ? it->second.scorePosition : -1;
            }
            rows.push_back(row);
        }

        // Score section — read-only ordered view of score steps
        std::vector<std::pair<int, Row>> scoreRows;
        for (auto& r : rows) {
            if (r.section == Row::SongControl && r.scorePosition >= 0) {
                Row scoreRow = r;
                scoreRow.section = Row::ScoreControl;
                scoreRows.push_back({ r.scorePosition, scoreRow });
            }
        }

        std::sort(scoreRows.begin(), scoreRows.end(),
                  [](auto& a, auto& b) { return a.first < b.first; });

        Row scoreHeader;
        scoreHeader.section = Row::ScoreHeader;
        rows.push_back(scoreHeader);

        for (auto& [pos, row] : scoreRows)
            rows.push_back(row);
    }
}

void MappingPane::refresh() {
    buildRows();
    repaint();
}

// --- Layout ---

static bool isSectionHeader(MappingPane::Row::Section s) {
    return s == MappingPane::Row::GlobalHeader || s == MappingPane::Row::SongHeader
           || s == MappingPane::Row::ScoreHeader;
}

int MappingPane::getRowY(int rowIndex) const {
    int y = headerHeight;
    bool passedScoreHeader = false;
    for (int i = 0; i < rowIndex && i < (int)rows.size(); ++i) {
        if (rows[i].section == Row::ScoreHeader && !passedScoreHeader) {
            y += scoreSectionGap;  // gap before score header
            passedScoreHeader = true;
        }
        y += isSectionHeader(rows[i].section) ? sectionHeaderHeight : rowHeight;
    }
    // Also add gap if we're AT the score header
    if (rowIndex < (int)rows.size() && rows[rowIndex].section == Row::ScoreHeader && !passedScoreHeader)
        y += scoreSectionGap;
    return y - scrollOffset;
}

juce::Rectangle<int> MappingPane::getRowBounds(int rowIndex) const {
    int y = getRowY(rowIndex);
    int h = (rowIndex < (int)rows.size() && isSectionHeader(rows[rowIndex].section))
            ? sectionHeaderHeight : rowHeight;
    return juce::Rectangle<int>(0, y, getWidth(), h);
}

int MappingPane::getTotalHeight() const {
    int h = headerHeight;
    for (auto& r : rows) {
        if (r.section == Row::ScoreHeader)
            h += scoreSectionGap;  // gap before score header
        h += isSectionHeader(r.section) ? sectionHeaderHeight : rowHeight;
    }
    return h;
}

// --- Paint ---

void MappingPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    if (currentDeviceId.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSize));
        g.drawText("Select a device from Maps", getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Header background
    auto headerArea = getLocalBounds().removeFromTop(headerHeight);
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(headerArea);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)headerHeight, (float)getWidth(), (float)headerHeight, 1.0f);

    // Clip to content area
    g.reduceClipRegion(getLocalBounds().withTrimmedTop(headerHeight));

    auto now = juce::Time::currentTimeMillis();

    for (int i = 0; i < (int)rows.size(); ++i) {
        auto bounds = getRowBounds(i);
        if (bounds.getBottom() < headerHeight) continue;
        if (bounds.getY() > getHeight()) break;

        auto& row = rows[i];

        // Section headers
        if (isSectionHeader(row.section)) {
            g.setColour(Theme::color(Theme::Color::bgSlot));
            g.fillRect(bounds);
            g.setColour(Theme::color(Theme::Color::textWhite));
            g.setFont(Theme::font(Theme::fontSizeSm));
            if (row.section == Row::GlobalHeader)
                g.drawText("Global Bindings", bounds.reduced(12, 0), juce::Justification::centredLeft);
            else if (row.section == Row::SongHeader) {
                g.drawText("Mappings", bounds.reduced(12, 0), juce::Justification::centredLeft);
                // Column headers
                g.setColour(Theme::color(Theme::Color::textDim));
                g.setFont(Theme::font(Theme::fontSizeXs));
                g.drawText("Name", colName, bounds.getY(), colGroup - colName, sectionHeaderHeight, juce::Justification::centredLeft);
                g.drawText("Group", colGroup, bounds.getY(), colType - colGroup, sectionHeaderHeight, juce::Justification::centredLeft);
                g.drawText("Type", colType, bounds.getY(), colCh - colType, sectionHeaderHeight, juce::Justification::centredLeft);
                g.drawText("Ch", colCh, bounds.getY(), colNum - colCh, sectionHeaderHeight, juce::Justification::centredLeft);
                g.drawText("#", colNum, bounds.getY(), colAction - colNum, sectionHeaderHeight, juce::Justification::centredLeft);
                g.drawText("Action", colAction, bounds.getY(), 80, sectionHeaderHeight, juce::Justification::centredLeft);
                g.drawText("In Score", colScore - 8, bounds.getY(), 50, sectionHeaderHeight, juce::Justification::centred);
            } else {
                // Score header — same style as Mappings header
                g.drawText("Score", bounds.reduced(12, 0), juce::Justification::centredLeft);
            }
            continue;
        }

        // Control rows — score rows get a distinct tint
        if (row.section == Row::ScoreControl) {
            g.setColour(juce::Colour(0xff141820));  // subtle dark blue tint
            g.fillRect(bounds);
        } else if (i % 2 == 0) {
            g.setColour(Theme::color(Theme::Color::bgPanel));
            g.fillRect(bounds);
        }
        if (i == hoveredRow && row.section != Row::ScoreControl) {
            g.setColour(Theme::color(Theme::Color::bgSlotHover));
            g.fillRect(bounds);
        }

        bool isScore = (row.section == Row::ScoreControl);
        auto textCol = isScore ? Theme::color(Theme::Color::textDim) : Theme::color(Theme::Color::textPrimary);
        auto dimCol = Theme::color(Theme::Color::textDim);
        auto secCol = isScore ? dimCol : Theme::color(Theme::Color::textSecondary);
        auto actionCol = isScore ? dimCol : Theme::color(Theme::Color::instrument);

        // Activity light (not for score rows)
        if (!isScore) {
            bool active = (row.lastActivityMs > 0 && now - row.lastActivityMs < 300);
            g.setColour(active ? juce::Colour(0xff44cc44) : juce::Colour(0xff1a3a1a));
            g.fillEllipse((float)colActivity, (float)(bounds.getY() + (rowHeight - 6) / 2), 6.0f, 6.0f);
        }

        g.setFont(Theme::font(Theme::fontSizeSm));

        // Score position prefix
        if (isScore) {
            g.setColour(secCol);
            g.drawText(juce::String(row.scorePosition) + ".", colActivity, bounds.getY(), 20, rowHeight,
                       juce::Justification::centredRight);
        }

        // Name
        g.setColour(textCol);
        g.drawText(juce::String(row.controlName), colName, bounds.getY(), colGroup - colName, rowHeight,
                   juce::Justification::centredLeft);

        // Group
        g.setColour(dimCol);
        g.setFont(Theme::font(Theme::fontSizeXs));
        g.drawText(juce::String(row.group.empty() ? "Default" : row.group),
                   colGroup, bounds.getY(), colType - colGroup, rowHeight,
                   juce::Justification::centredLeft);

        // Type, Ch, #
        g.setColour(secCol);
        g.drawText(juce::String(row.controlType), colType, bounds.getY(), colCh - colType, rowHeight,
                   juce::Justification::centredLeft);
        g.drawText(juce::String(row.channel), colCh, bounds.getY(), colNum - colCh, rowHeight,
                   juce::Justification::centredLeft);
        g.drawText(juce::String(row.number), colNum, bounds.getY(), colAction - colNum, rowHeight,
                   juce::Justification::centredLeft);

        // Action
        g.setFont(Theme::font(Theme::fontSizeSm));
        if (row.bindingId.empty() && !isScore) {
            g.setColour(dimCol);
            g.drawText("-- assign --", colAction, bounds.getY(), 140, rowHeight,
                       juce::Justification::centredLeft);
        } else if (!row.bindingId.empty()) {
            g.setColour(actionCol);
            g.drawText(juce::String(row.actionName), colAction, bounds.getY(), 80, rowHeight,
                       juce::Justification::centredLeft);
            g.setColour(secCol);
            g.setFont(Theme::font(Theme::fontSizeXs));
            g.drawText(juce::String(row.argsDisplay), colAction + 84, bounds.getY(), 80, rowHeight,
                       juce::Justification::centredLeft);
        }

        // Score toggle (song rows with a binding)
        if (row.section == Row::SongControl && !row.bindingId.empty()) {
            bool inScore = (row.scorePosition >= 0);
            // Filled circle = in score, empty circle = not in score
            float cx = (float)colScore + 8.0f;
            float cy = (float)bounds.getCentreY();
            if (inScore) {
                g.setColour(Theme::color(Theme::Color::accent));
                g.fillEllipse(cx - 5.0f, cy - 5.0f, 10.0f, 10.0f);
                // Show position number next to it
                g.setFont(Theme::font(Theme::fontSizeXs));
                g.drawText(juce::String(row.scorePosition), (int)cx + 8, bounds.getY(), 20, rowHeight,
                           juce::Justification::centredLeft);
            } else {
                g.setColour(Theme::color(Theme::Color::textDim));
                g.drawEllipse(cx - 4.0f, cy - 4.0f, 8.0f, 8.0f, 1.0f);
            }
        }

    }
}

void MappingPane::resized() {
    if (currentDeviceId.empty()) return;
    int learnW = 120, learnH = 24;
    learnButton.setBounds(getWidth() - learnW - 12, 10, learnW, learnH);
    deviceNameLabel.setBounds(12, 8, getWidth() - learnW - 36, 24);
    portNameLabel.setBounds(12, 34, 200, 16);
    midiLabelPrefix.setBounds(220, 34, 100, 16);
    midiEventLabel.setBounds(320, 34, getWidth() - 332, 16);
}

void MappingPane::timerCallback() {
    // Repaint for activity light decay
    repaint();
}

// --- Mouse interaction ---

void MappingPane::mouseUp(const juce::MouseEvent& event) {
    for (int i = 0; i < (int)rows.size(); ++i) {
        auto bounds = getRowBounds(i);
        if (!bounds.contains(event.getPosition())) continue;
        auto& row = rows[i];
        if (row.section != Row::GlobalControl && row.section != Row::SongControl
            && row.section != Row::ScoreControl) continue;

        // Right-click context menu
        if (event.mods.isPopupMenu()) {
            juce::PopupMenu menu;
            if (!row.bindingId.empty())
                menu.addItem(1, "Clear Binding");
            if (row.controlIndex >= 0)
                menu.addItem(2, "Delete Control");

            menu.showMenuAsync(juce::PopupMenu::Options()
                .withTargetScreenArea(juce::Rectangle<int>(
                    event.getScreenPosition().x, event.getScreenPosition().y, 1, 1)),
                [this, bindingId = row.bindingId, ctrlIdx = row.controlIndex](int result) {
                    if (result == 1 && !bindingId.empty()) {
                        state.removeBinding(bindingId);
                        refresh();
                    } else if (result == 2 && ctrlIdx >= 0) {
                        state.removeDeviceControl(currentDeviceId, ctrlIdx);
                        refresh();
                    }
                });
            return;
        }

        int x = event.getPosition().getX();

        // Action column click (not for score rows — they already have an action)
        if (x >= colAction && x < colScore && row.section != Row::ScoreControl) {
            showActionMenu(i, event.getScreenPosition());
            return;
        }

        // Score toggle: click circle to add/remove from score
        if (row.section == Row::SongControl && !row.bindingId.empty() && x >= colScore) {
            if (row.scorePosition >= 0) {
                // Remove from score
                state.clearScoreStep(row.bindingId);
            } else {
                // Add to score — find next available position
                int maxPos = 0;
                for (auto& r : rows)
                    if (r.section == Row::SongControl && r.scorePosition > maxPos)
                        maxPos = r.scorePosition;
                state.setBindingAsScoreStep(row.bindingId, maxPos + 1);
            }
            refresh();
            return;
        }
    }
}

void MappingPane::mouseMove(const juce::MouseEvent& event) {
    int newHovered = -1;
    for (int i = 0; i < (int)rows.size(); ++i) {
        if ((rows[i].section == Row::GlobalControl || rows[i].section == Row::SongControl
             || rows[i].section == Row::ScoreControl)
            && getRowBounds(i).contains(event.getPosition())) {
            newHovered = i;
            break;
        }
    }
    if (newHovered != hoveredRow) {
        hoveredRow = newHovered;
        repaint();
    }
}

void MappingPane::mouseDoubleClick(const juce::MouseEvent& event) {
    if (currentDeviceId.empty()) return;
    for (int i = 0; i < (int)rows.size(); ++i) {
        auto bounds = getRowBounds(i);
        if (!bounds.contains(event.getPosition())) continue;
        auto& row = rows[i];
        if (row.section != Row::GlobalControl && row.section != Row::SongControl) continue;
        int x = event.getPosition().getX();

        // Double-click name → rename
        if (x >= colName && x < colGroup) {
            auto nameBounds = juce::Rectangle<int>(colName, bounds.getY(), colGroup - colName, rowHeight);
            inlineEditor.onCommit = [this, idx = row.controlIndex](const juce::String& newText) {
                state.renameDeviceControl(currentDeviceId, idx, newText.toStdString());
                refresh();
            };
            inlineEditor.onCancel = nullptr;
            inlineEditor.show(*this, nameBounds, juce::String(row.controlName));
            return;
        }

        // Double-click group → group menu
        if (x >= colGroup && x < colType) {
            showGroupMenu(i, event.getScreenPosition());
            return;
        }
    }
}

void MappingPane::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) {
    int maxScroll = std::max(0, getTotalHeight() - (getHeight() - headerHeight));
    scrollOffset = juce::jlimit(0, maxScroll, scrollOffset - (int)(wheel.deltaY * 40));
    repaint();
}

bool MappingPane::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey && isLearning) {
        cancelLearn();
        return true;
    }
    return false;
}

// --- Learn mode ---

void MappingPane::startLearn() {
    isLearning = true;
    learnButton.setButtonText("Stop learning");
    armLearnCapture();
}

void MappingPane::cancelLearn() {
    isLearning = false;
    learnButton.setButtonText("Learn mappings");
    coordinator.cancelMidiLearn();
}

void MappingPane::armLearnCapture() {
    coordinator.startMidiLearn(currentDeviceId,
        [this](const std::string& type, int ch, int num) {
            juce::MessageManager::callAsync([this, type, ch, num] {
                onLearnCapture(type, ch, num);
            });
        });
}

void MappingPane::onLearnCapture(const std::string& type, int channel, int number) {
    if (!isLearning) return;

    // Check if this control already exists
    auto* device = state.findDevice(currentDeviceId);
    if (device) {
        for (int i = 0; i < (int)device->controls.size(); ++i) {
            auto& ctrl = device->controls[i];
            if (ctrl.controlType == type && ctrl.channel == channel && ctrl.number == number) {
                // Already mapped — re-arm
                armLearnCapture();
                return;
            }
        }
    }

    // Add new control
    state.addDeviceControl(currentDeviceId, "", type, channel, number);
    refresh();

    // Open inline editor for naming
    int newIdx = (int)state.findDevice(currentDeviceId)->controls.size() - 1;
    // Find the row for this control in the global section
    for (int i = 0; i < (int)rows.size(); ++i) {
        if (rows[i].section == Row::GlobalControl && rows[i].controlIndex == newIdx) {
            auto bounds = getRowBounds(i);
            auto nameBounds = juce::Rectangle<int>(colName, bounds.getY(), colGroup - colName, rowHeight);
            pendingLearnControlIndex = newIdx;
            inlineEditor.onCommit = [this, newIdx](const juce::String& newText) {
                state.renameDeviceControl(currentDeviceId, newIdx, newText.toStdString());
                pendingLearnControlIndex = -1;
                refresh();
                if (isLearning) armLearnCapture();
            };
            inlineEditor.onCancel = [this]() {
                pendingLearnControlIndex = -1;
                if (isLearning) armLearnCapture();
            };
            inlineEditor.show(*this, nameBounds, "");
            break;
        }
    }
}

// --- Action menu ---

void MappingPane::showActionMenu(int rowIndex, juce::Point<int> screenPos) {
    auto& row = rows[rowIndex];

    juce::PopupMenu menu;
    auto& actions = state.allActions();
    menu.addItem(1, "-- none --", true, row.bindingId.empty());
    for (int i = 0; i < (int)actions.size(); ++i) {
        auto label = actions[i].label.empty() ? actions[i].name : actions[i].label;
        menu.addItem(i + 2, juce::String(label));
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
        .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, rowIndex](int result) {
            if (result == 0) return;
            auto& row = rows[rowIndex];

            if (result == 1) {
                if (!row.bindingId.empty()) {
                    state.removeBinding(row.bindingId);
                    refresh();
                }
                return;
            }

            int actionIdx = result - 2;
            auto& actions = state.allActions();
            if (actionIdx < 0 || actionIdx >= (int)actions.size()) return;
            auto action = actions[actionIdx];

            auto schema = juce::JSON::parse(juce::String(action.paramSchema));
            auto* params = schema.getArray();
            if (!params || params->isEmpty()) {
                // No params — create binding immediately
                if (!row.bindingId.empty()) state.removeBinding(row.bindingId);
                auto args = juce::var(juce::Array<juce::var>());
                auto argsJson = juce::JSON::toString(args, true).toStdString();
                auto desc = row.controlName + " -> " + action.name;
                auto* song = state.currentSong();
                if (song)
                    state.addBinding(song->id, row.controlType, row.channel, row.number,
                                      action.id, argsJson, desc, currentDeviceId);
            } else {
                showArgsPopup(row, action);
            }
        });
}

void MappingPane::showArgsPopup(const Row& row, const ActionInfo& action) {
    auto label = action.label.empty() ? action.name : action.label;
    auto dialog = std::make_shared<juce::AlertWindow>(
        juce::String(label), "Configure for " + juce::String(row.controlName),
        juce::MessageBoxIconType::NoIcon);
    dialog->setColour(juce::AlertWindow::backgroundColourId, Theme::color(Theme::Color::bgPanel));
    dialog->setColour(juce::AlertWindow::textColourId, Theme::color(Theme::Color::textPrimary));

    auto schema = juce::JSON::parse(juce::String(action.paramSchema));
    auto* params = schema.getArray();

    juce::StringArray trackNames;
    for (auto& t : state.listTracks()) trackNames.add(juce::String(t.name));
    juce::StringArray easingOptions = { "linear", "easein", "easeout", "cosine", "scurve" };

    struct FieldInfo { std::string name; std::string type; };
    auto fields = std::make_shared<std::vector<FieldInfo>>();

    if (params) {
        for (auto& param : *params) {
            auto name = param.getProperty("name", "").toString().toStdString();
            auto type = param.getProperty("type", "string").toString().toStdString();
            fields->push_back({ name, type });

            if (name.find("track") != std::string::npos || name.find("Track") != std::string::npos)
                dialog->addComboBox(juce::String(name), trackNames, juce::String(name));
            else if (name == "easing") {
                dialog->addComboBox("easing", easingOptions, "Easing");
                if (auto* cb = dialog->getComboBoxComponent("easing")) cb->setSelectedItemIndex(3);
            }
            else if (name == "duration" || type == "float")
                dialog->addTextEditor(juce::String(name), "3.0", juce::String(name));
            else
                dialog->addTextEditor(juce::String(name), "", juce::String(name));
        }
    }

    dialog->addButton("OK", 1);
    dialog->addButton("Cancel", 0);

    auto rowCopy = row;
    auto actionCopy = action;
    auto* statePtr = &state;
    auto devId = currentDeviceId;

    dialog->enterModalState(true, juce::ModalCallbackFunction::create(
        [dialog, statePtr, fields, rowCopy, actionCopy, devId](int result) {
            if (result == 0) return;

            auto argsArray = juce::var(juce::Array<juce::var>());
            auto schema = juce::JSON::parse(juce::String(actionCopy.paramSchema));
            auto* params = schema.getArray();

            for (int fi = 0; fi < (int)fields->size(); ++fi) {
                auto& field = (*fields)[fi];
                auto jName = juce::String(field.name);
                bool isTrack = (field.name.find("track") != std::string::npos ||
                                field.name.find("Track") != std::string::npos);

                if (isTrack) {
                    if (auto* cb = dialog->getComboBoxComponent(jName)) {
                        auto trackName = cb->getText();
                        juce::String trackId;
                        for (auto& t : statePtr->listTracks())
                            if (juce::String(t.name) == trackName) { trackId = juce::String(t.id); break; }
                        argsArray.append(juce::var(trackId.isNotEmpty() ? trackId : trackName));
                    }
                } else if (field.name == "easing") {
                    if (auto* cb = dialog->getComboBoxComponent(jName))
                        argsArray.append(juce::var(cb->getText()));
                } else {
                    auto text = dialog->getTextEditorContents(jName);
                    if (field.type == "float") argsArray.append(text.getFloatValue());
                    else argsArray.append(juce::var(text));
                }
            }

            if (!rowCopy.bindingId.empty()) statePtr->removeBinding(rowCopy.bindingId);

            auto argsJson = juce::JSON::toString(argsArray, true).toStdString();
            auto desc = rowCopy.controlName + " -> " + actionCopy.name;

            auto* song = statePtr->currentSong();
            if (song)
                statePtr->addBinding(song->id, rowCopy.controlType, rowCopy.channel, rowCopy.number,
                                      actionCopy.id, argsJson, desc, devId);
        }), false);
}

void MappingPane::showScoreMenu(int rowIndex, juce::Point<int> screenPos) {
    auto& row = rows[rowIndex];
    if (row.bindingId.empty()) return;  // must have a binding to be a score step

    juce::PopupMenu menu;
    menu.addItem(1, "-- none --", true, row.scorePosition < 0);
    for (int i = 1; i <= 20; ++i)
        menu.addItem(i + 1, juce::String(i), true, row.scorePosition == i);

    menu.showMenuAsync(juce::PopupMenu::Options()
        .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, rowIndex](int result) {
            if (result == 0) return;
            auto& row = rows[rowIndex];
            if (result == 1)
                state.clearScoreStep(row.bindingId);
            else
                state.setBindingAsScoreStep(row.bindingId, result - 1);
            refresh();
        });
}

void MappingPane::showGroupMenu(int rowIndex, juce::Point<int> screenPos) {
    auto& row = rows[rowIndex];
    auto groups = getExistingGroups();

    juce::PopupMenu menu;
    menu.addItem(1, "Default", true, row.group.empty());
    int itemId = 2;
    std::vector<std::string> groupList(groups.begin(), groups.end());
    for (auto& g : groupList) {
        if (g == "Default") continue;
        menu.addItem(itemId++, juce::String(g), true, row.group == g);
    }
    menu.addSeparator();
    int newGroupId = itemId;
    menu.addItem(newGroupId, "New Group...");

    menu.showMenuAsync(juce::PopupMenu::Options()
        .withTargetScreenArea(juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, rowIndex, groupList, newGroupId](int result) {
            if (result == 0) return;
            auto& row = rows[rowIndex];
            if (result == newGroupId) {
                auto bounds = juce::Rectangle<int>(colGroup, getRowBounds(rowIndex).getY(),
                                                    colType - colGroup, rowHeight);
                inlineEditor.onCommit = [this, idx = row.controlIndex](const juce::String& newText) {
                    state.setDeviceControlGroup(currentDeviceId, idx, newText.toStdString());
                    refresh();
                };
                inlineEditor.onCancel = nullptr;
                inlineEditor.show(*this, bounds, "");
            } else if (result == 1) {
                state.setDeviceControlGroup(currentDeviceId, row.controlIndex, "");
                refresh();
            } else {
                std::vector<std::string> filtered;
                for (auto& g : groupList) if (g != "Default") filtered.push_back(g);
                int idx = result - 2;
                if (idx >= 0 && idx < (int)filtered.size()) {
                    state.setDeviceControlGroup(currentDeviceId, row.controlIndex, filtered[idx]);
                    refresh();
                }
            }
        });
}
