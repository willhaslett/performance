#include "gui/MappingPane.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"
#include <set>

MappingPane::MappingPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator)
    : state(state), engine(engine), coordinator(coordinator) {
    addAndMakeVisible(learnButton);
    learnButton.setButtonText("Learn");
    learnButton.onClick = [this]() {
        if (isLearning) cancelLearn(); else startLearn();
    };

    stateSubscriptionId = state.events().subscribe([this](const StateEvent&) {
        juce::MessageManager::callAsync([this] { refresh(); });
    });

    startTimerHz(10);
    buildRows();
}

MappingPane::~MappingPane() {
    if (stateSubscriptionId >= 0)
        state.events().unsubscribe(stateSubscriptionId);
    if (isLearning) cancelLearn();
}

// --- Build ---

void MappingPane::buildRows() {
    leftEntries.clear();
    mappingRows.clear();
    scoreRows.clear();

    auto* song = state.currentSong();
    if (!song) return;

    struct ControlKey {
        std::string deviceId, controlType;
        int channel, number;
        bool operator<(const ControlKey& o) const {
            if (deviceId != o.deviceId) return deviceId < o.deviceId;
            if (controlType != o.controlType) return controlType < o.controlType;
            if (channel != o.channel) return channel < o.channel;
            return number < o.number;
        }
    };
    std::map<ControlKey, const BindingState*> boundControls;
    for (auto& b : song->bindings)
        boundControls[{ b.deviceId, b.controlType, b.channel, b.number }] = &b;

    auto midiDevices = juce::MidiInput::getAvailableDevices();
    std::set<std::string> connectedPorts;
    for (auto& d : midiDevices) connectedPorts.insert(d.name.toStdString());

    auto& devices = state.allDevices();
    for (int di = 0; di < (int)devices.size(); ++di) {
        auto& dev = devices[di];
        bool connected = connectedPorts.count(dev.midiPortName) > 0;

        std::vector<int> unmappedIndices;
        for (int ci = 0; ci < (int)dev.controls.size(); ++ci) {
            auto& ctrl = dev.controls[ci];
            if (boundControls.find({ dev.id, ctrl.controlType, ctrl.channel, ctrl.number }) == boundControls.end())
                unmappedIndices.push_back(ci);
        }

        if (!unmappedIndices.empty()) {
            LeftPanelEntry header;
            header.type = LeftPanelEntry::DeviceHeader;
            header.deviceId = dev.id;
            header.deviceName = dev.name;
            header.deviceConnected = connected;
            leftEntries.push_back(header);

            for (int ci : unmappedIndices) {
                auto& ctrl = dev.controls[ci];
                LeftPanelEntry entry;
                entry.type = LeftPanelEntry::Control;
                entry.deviceId = dev.id;
                entry.deviceName = dev.name;
                entry.deviceConnected = connected;
                entry.controlName = ctrl.name;
                entry.group = ctrl.group;
                entry.controlType = ctrl.controlType;
                entry.channel = ctrl.channel;
                entry.number = ctrl.number;
                entry.controlIndex = ci;
                leftEntries.push_back(entry);
            }
        }

        for (int ci = 0; ci < (int)dev.controls.size(); ++ci) {
            auto& ctrl = dev.controls[ci];
            auto it = boundControls.find({ dev.id, ctrl.controlType, ctrl.channel, ctrl.number });
            if (it == boundControls.end()) continue;

            auto* binding = it->second;
            auto* action = state.findActionById(binding->actionId);
            std::string label = action ? (action->label.empty() ? action->name : action->label) : "?";

            if (binding->isScoreStep) {
                ScoreRow sr;
                sr.deviceId = dev.id;
                sr.deviceName = dev.name;
                sr.controlName = ctrl.name;
                sr.controlType = ctrl.controlType;
                sr.channel = ctrl.channel;
                sr.number = ctrl.number;
                sr.bindingId = binding->id;
                sr.actionLabel = label;
                sr.argsDisplay = formatArgs(binding->args);
                sr.scorePosition = binding->scorePosition;
                scoreRows.push_back(sr);
            } else {
                MappingRow mr;
                mr.deviceId = dev.id;
                mr.deviceName = dev.name;
                mr.controlName = ctrl.name;
                mr.group = ctrl.group;
                mr.controlType = ctrl.controlType;
                mr.channel = ctrl.channel;
                mr.number = ctrl.number;
                mr.controlIndex = ci;
                mr.bindingId = binding->id;
                mr.actionLabel = label;
                mr.argsDisplay = formatArgs(binding->args);
                mappingRows.push_back(mr);
            }
        }
    }

    std::sort(mappingRows.begin(), mappingRows.end(),
        [&devices](const MappingRow& a, const MappingRow& b) {
            int ai = -1, bi = -1;
            for (int i = 0; i < (int)devices.size(); ++i) {
                if (devices[i].id == a.deviceId) ai = i;
                if (devices[i].id == b.deviceId) bi = i;
            }
            if (ai != bi) return ai < bi;
            if (a.group != b.group) return a.group < b.group;
            return a.controlName < b.controlName;
        });

    std::sort(scoreRows.begin(), scoreRows.end(),
        [](const ScoreRow& a, const ScoreRow& b) { return a.scorePosition < b.scorePosition; });
}

void MappingPane::refresh() { buildRows(); repaint(); }

// --- Utility ---

juce::Colour MappingPane::getDeviceColor(const std::string& deviceId) const {
    auto& devices = state.allDevices();
    for (int i = 0; i < (int)devices.size(); ++i)
        if (devices[i].id == deviceId)
            return juce::Colour(Theme::Color::deviceColors[i % Theme::Color::deviceColorCount]);
    return Theme::color(Theme::Color::bgPanel);
}

bool MappingPane::isDeviceConnected(const std::string& deviceId) const {
    auto* dev = state.findDevice(deviceId);
    if (!dev) return false;
    auto midiDevices = juce::MidiInput::getAvailableDevices();
    for (auto& d : midiDevices)
        if (d.name.toStdString() == dev->midiPortName) return true;
    return false;
}

std::string MappingPane::formatArgs(const std::string& argsJson) const {
    auto args = juce::JSON::parse(juce::String(argsJson));
    if (!args.isArray() || args.size() == 0) return "";
    juce::String result;
    for (int i = 0; i < std::min(args.size(), 2); ++i) {
        if (i > 0) result += ", ";
        auto val = args[i].toString();
        if (val.length() > 20) {
            auto* track = state.findTrack(val.toStdString());
            if (track) val = juce::String(track->name);
        }
        result += val;
    }
    return result.toStdString();
}

// --- Layout ---

void MappingPane::resized() {
    auto area = getLocalBounds();
    learnButton.setBounds(area.getWidth() - 80, 10, 70, 24);

    auto contentArea = area.withTrimmedTop(headerHeight);
    leftPanelBounds = contentArea.removeFromLeft(leftPanelWidth);

    int scoreHeight = std::max(120, sectionTitleHeight + (int)scoreRows.size() * scoreRowHeight + 20);
    scoreHeight = std::min(scoreHeight, contentArea.getHeight() / 2);

    mappingPanelBounds = contentArea.withTrimmedBottom(scoreHeight);
    scorePanelBounds = contentArea.withTrimmedTop(contentArea.getHeight() - scoreHeight);
}

// --- Paint ---

void MappingPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));
    paintHeader(g);
    paintLeftPanel(g);
    paintMappingPanel(g);
    paintScorePanel(g);
}

void MappingPane::paintHeader(juce::Graphics& g) {
    auto area = getLocalBounds().removeFromTop(headerHeight);
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area);

    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(16.0f));
    g.drawText("Performance Map", 12, 8, 200, 28, juce::Justification::centredLeft);

    auto* song = state.currentSong();
    if (song) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(11.0f));
        g.drawText(juce::String(song->name), 180, 14, 200, 16, juce::Justification::centredLeft);
    }

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0, (float)headerHeight, (float)getWidth(), (float)headerHeight, 1.0f);
}

void MappingPane::paintLeftPanel(juce::Graphics& g) {
    auto area = leftPanelBounds;
    g.setColour(juce::Colour(0xff222222));
    g.fillRect(area);

    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(11.0f));
    g.drawText("Available Controls", area.getX() + panelPadding, area.getY() + 4,
               area.getWidth() - panelPadding * 2, 20, juce::Justification::centredLeft);

    int y = area.getY() + sectionTitleHeight - leftScrollOffset;
    for (int i = 0; i < (int)leftEntries.size(); ++i) {
        if (y > area.getBottom()) break;
        auto& entry = leftEntries[i];

        if (entry.type == LeftPanelEntry::DeviceHeader) {
            if (y + rowHeight >= area.getY() + sectionTitleHeight) {
                auto devCol = getDeviceColor(entry.deviceId);
                g.setColour(devCol.darker(0.3f));
                g.fillRect(area.getX(), y, area.getWidth(), rowHeight);

                float dotY = (float)(y + rowHeight / 2 - 3);
                g.setColour(entry.deviceConnected ? juce::Colour(0xff44cc44) : juce::Colour(0xff555555));
                g.fillEllipse((float)(area.getX() + 8), dotY, 6.0f, 6.0f);

                g.setColour(Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(12.0f));
                g.drawText(juce::String(entry.deviceName), area.getX() + 20, y,
                           area.getWidth() - 24, rowHeight, juce::Justification::centredLeft);
            }
        } else {
            if (y + rowHeight >= area.getY() + sectionTitleHeight) {
                auto devCol = getDeviceColor(entry.deviceId);
                bool hovered = (i == hoveredLeftRow);
                g.setColour(hovered ? devCol.brighter(0.15f) : devCol.withAlpha(0.3f));
                g.fillRect(area.getX() + 4, y, area.getWidth() - 8, rowHeight - 1);

                auto now = juce::Time::currentTimeMillis();
                bool active = (entry.lastActivityMs > 0 && now - entry.lastActivityMs < 300);
                g.setColour(active ? juce::Colour(0xff44cc44) : juce::Colour(0xff1a3a1a));
                g.fillEllipse((float)(area.getX() + 12), (float)(y + rowHeight / 2 - 3), 6.0f, 6.0f);

                g.setColour(Theme::color(Theme::Color::textSecondary));
                g.setFont(Theme::font(Theme::fontSizeSm));
                g.drawText(juce::String(entry.controlName), area.getX() + 24, y,
                           area.getWidth() - 28, rowHeight, juce::Justification::centredLeft);
            }
        }
        y += rowHeight;
    }

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getRight(), (float)area.getY(),
               (float)area.getRight(), (float)area.getBottom(), 1.0f);
}

void MappingPane::paintMappingPanel(juce::Graphics& g) {
    auto area = mappingPanelBounds;

    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(11.0f));
    g.drawText("Mappings", area.getX() + panelPadding, area.getY() + 4, 200, 20,
               juce::Justification::centredLeft);

    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(16.0f));
    g.drawText("+", area.getRight() - 30, area.getY() + 4, 20, 20, juce::Justification::centred);

    int y = area.getY() + sectionTitleHeight - mappingScrollOffset;
    auto now = juce::Time::currentTimeMillis();

    if (mappingRows.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(11.0f));
        g.drawText("Drag controls here or click +",
                   area.getX() + 20, y, area.getWidth() - 40, 40, juce::Justification::centred);
    }

    for (int i = 0; i < (int)mappingRows.size(); ++i) {
        if (y > area.getBottom()) break;
        if (y + rowHeight < area.getY() + sectionTitleHeight) { y += rowHeight; continue; }

        auto& mr = mappingRows[i];
        auto devCol = getDeviceColor(mr.deviceId);
        bool hovered = (i == hoveredMappingRow);

        g.setColour(hovered ? devCol.brighter(0.1f) : devCol.withAlpha(0.4f));
        g.fillRect(area.getX() + 4, y, area.getWidth() - 8, rowHeight - 1);

        bool active = (mr.lastActivityMs > 0 && now - mr.lastActivityMs < 300);
        g.setColour(active ? juce::Colour(0xff44cc44) : juce::Colour(0xff1a3a1a));
        g.fillEllipse((float)(area.getX() + 10), (float)(y + rowHeight / 2 - 3), 6.0f, 6.0f);

        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(juce::String(mr.controlName), area.getX() + 22, y, 120, rowHeight,
                   juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::textDim));
        g.drawText(juce::CharPointer_UTF8("\xe2\x86\x92"), area.getX() + 142, y, 20, rowHeight,
                   juce::Justification::centred);

        g.setColour(Theme::color(Theme::Color::textPrimary));
        auto actionText = juce::String(mr.actionLabel);
        if (!mr.argsDisplay.empty()) actionText += ": " + juce::String(mr.argsDisplay);
        g.drawText(actionText, area.getX() + 166, y, area.getWidth() - 180, rowHeight,
                   juce::Justification::centredLeft);

        y += rowHeight;
    }

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getX(), (float)area.getBottom(),
               (float)area.getRight(), (float)area.getBottom(), 1.0f);
}

void MappingPane::paintScorePanel(juce::Graphics& g) {
    auto area = scorePanelBounds;

    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(11.0f));
    g.drawText("Score", area.getX() + panelPadding, area.getY() + 4, 200, 20,
               juce::Justification::centredLeft);

    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(16.0f));
    g.drawText("+", area.getRight() - 30, area.getY() + 4, 20, 20, juce::Justification::centred);

    int y = area.getY() + sectionTitleHeight - scoreScrollOffset;

    if (scoreRows.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(11.0f));
        g.drawText("Drag controls here or click +",
                   area.getX() + 20, y, area.getWidth() - 40, 40, juce::Justification::centred);
    }

    for (int i = 0; i < (int)scoreRows.size(); ++i) {
        if (y > area.getBottom()) break;
        if (y + scoreRowHeight < area.getY() + sectionTitleHeight) { y += scoreRowHeight; continue; }

        auto& sr = scoreRows[i];
        auto devCol = getDeviceColor(sr.deviceId);

        g.setColour(devCol.withAlpha(0.4f));
        g.fillRect(area.getX() + 4, y, area.getWidth() - 8, scoreRowHeight - 1);

        auto badge = juce::Rectangle<int>(area.getX() + 8, y + 3, 22, scoreRowHeight - 6);
        g.setColour(Theme::color(Theme::Color::accent));
        g.fillRoundedRectangle(badge.toFloat(), 4.0f);
        g.setColour(Theme::color(Theme::Color::textWhite));
        g.setFont(Theme::font(11.0f));
        g.drawText(juce::String(sr.scorePosition), badge, juce::Justification::centred);

        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        auto actionText = juce::String(sr.actionLabel);
        if (!sr.argsDisplay.empty()) actionText += ": " + juce::String(sr.argsDisplay);
        g.drawText(actionText, area.getX() + 36, y, area.getWidth() - 180, scoreRowHeight,
                   juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(10.0f));
        auto srcText = juce::String(sr.controlName) + " (" + juce::String(sr.deviceName) + ")";
        g.drawText(srcText, area.getX(), y, area.getWidth() - 12, scoreRowHeight,
                   juce::Justification::centredRight);

        y += scoreRowHeight;
    }
}

// --- Interactions ---

void MappingPane::mouseDown(const juce::MouseEvent&) {}
void MappingPane::mouseDrag(const juce::MouseEvent&) {}

void MappingPane::mouseUp(const juce::MouseEvent& event) {
    auto pos = event.getPosition();

    // Right-click mapping row → remove
    if (event.mods.isPopupMenu() && mappingPanelBounds.contains(pos)) {
        int y = mappingPanelBounds.getY() + sectionTitleHeight - mappingScrollOffset;
        for (int i = 0; i < (int)mappingRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                auto bindingId = mappingRows[i].bindingId;
                juce::PopupMenu menu;
                menu.addItem(1, "Remove Mapping");
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                    juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
                    [this, bindingId](int r) { if (r == 1) { state.removeBinding(bindingId); refresh(); } });
                return;
            }
            y += rowHeight;
        }
    }

    // Right-click score row → remove
    if (event.mods.isPopupMenu() && scorePanelBounds.contains(pos)) {
        int y = scorePanelBounds.getY() + sectionTitleHeight - scoreScrollOffset;
        for (int i = 0; i < (int)scoreRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + scoreRowHeight) {
                auto bindingId = scoreRows[i].bindingId;
                juce::PopupMenu menu;
                menu.addItem(1, "Remove from Score");
                menu.addItem(2, "Remove Mapping");
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
                    juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1)),
                    [this, bindingId](int r) {
                        if (r == 1) state.clearScoreStep(bindingId);
                        else if (r == 2) state.removeBinding(bindingId);
                        refresh();
                    });
                return;
            }
            y += scoreRowHeight;
        }
    }

    // Click left panel control → action menu to create mapping
    if (!event.mods.isPopupMenu() && leftPanelBounds.contains(pos)) {
        int y = leftPanelBounds.getY() + sectionTitleHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                auto& e = leftEntries[i];
                showActionMenu(e.deviceId, e.controlType, e.channel, e.number,
                               e.controlName, "", event.getScreenPosition(), false);
                return;
            }
            y += rowHeight;
        }
    }

    // Click action on mapping row → reassign
    if (!event.mods.isPopupMenu() && mappingPanelBounds.contains(pos)) {
        int y = mappingPanelBounds.getY() + sectionTitleHeight - mappingScrollOffset;
        for (int i = 0; i < (int)mappingRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight && pos.getX() > mappingPanelBounds.getX() + 140) {
                auto& mr = mappingRows[i];
                showActionMenu(mr.deviceId, mr.controlType, mr.channel, mr.number,
                               mr.controlName, mr.bindingId, event.getScreenPosition(), false);
                return;
            }
            y += rowHeight;
        }
    }
}

void MappingPane::mouseMove(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    int newLeft = -1, newMapping = -1;

    if (leftPanelBounds.contains(pos)) {
        int y = leftPanelBounds.getY() + sectionTitleHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) { newLeft = i; break; }
            y += rowHeight;
        }
    } else if (mappingPanelBounds.contains(pos)) {
        int y = mappingPanelBounds.getY() + sectionTitleHeight - mappingScrollOffset;
        for (int i = 0; i < (int)mappingRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) { newMapping = i; break; }
            y += rowHeight;
        }
    }

    if (newLeft != hoveredLeftRow || newMapping != hoveredMappingRow) {
        hoveredLeftRow = newLeft;
        hoveredMappingRow = newMapping;
        repaint();
    }
}

void MappingPane::mouseWheelMove(const juce::MouseEvent& event,
                                   const juce::MouseWheelDetails& wheel) {
    auto pos = event.getPosition();
    int delta = (int)(wheel.deltaY * 100);
    if (leftPanelBounds.contains(pos))
        leftScrollOffset = std::max(0, leftScrollOffset - delta);
    else if (mappingPanelBounds.contains(pos))
        mappingScrollOffset = std::max(0, mappingScrollOffset - delta);
    else if (scorePanelBounds.contains(pos))
        scoreScrollOffset = std::max(0, scoreScrollOffset - delta);
    repaint();
}

void MappingPane::scrollToDevice(const std::string& deviceId) {
    int y = 0;
    for (auto& entry : leftEntries) {
        if (entry.type == LeftPanelEntry::DeviceHeader && entry.deviceId == deviceId) {
            leftScrollOffset = y;
            repaint();
            return;
        }
        y += rowHeight;
    }
}

// --- Action Menu ---

void MappingPane::showActionMenu(const std::string& deviceId, const std::string& ctrlType,
                                  int channel, int number, const std::string& controlName,
                                  const std::string& existingBindingId,
                                  juce::Point<int> screenPos, bool asScoreStep) {
    auto actions = state.allActions();
    auto tracks = state.listTracks();

    juce::PopupMenu menu;
    int baseId = 1;

    for (int ai = 0; ai < (int)actions.size(); ++ai) {
        if (actions[ai].name == "morph") continue;
        auto schema = juce::JSON::parse(juce::String(actions[ai].paramSchema));
        bool isTrackParam = false;
        if (schema.isArray() && schema.size() > 0) {
            auto pn = schema[0].getProperty("name", "").toString();
            auto pt = schema[0].getProperty("type", "").toString();
            isTrackParam = (pn.containsIgnoreCase("track") || pn == "channel")
                            && (pt == "string" || pt == "channel");
        }
        if (isTrackParam) {
            juce::PopupMenu sub;
            for (int ti = 0; ti < (int)tracks.size(); ++ti) {
                auto* ts = state.findTrack(tracks[ti].id);
                if (ts && ts->sourceType == TrackSourceType::Action) continue;
                sub.addItem(baseId + ai * 1000 + ti + 1, juce::String(tracks[ti].name));
            }
            menu.addSubMenu(juce::String(actions[ai].label), sub);
        } else {
            menu.addItem(baseId + ai * 1000, juce::String(actions[ai].label));
        }
    }

    auto capDevId = deviceId;
    auto capCtrlType = ctrlType;
    auto capBindId = existingBindingId;
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, capDevId, capCtrlType, channel, number, controlName,
         capBindId, asScoreStep, actions, tracks, baseId](int result) {
            if (result <= 0) return;
            int ai = (result - baseId) / 1000;
            int sub = (result - baseId) % 1000;
            if (ai < 0 || ai >= (int)actions.size()) return;

            juce::String firstArg;
            if (sub > 0) {
                int ti = sub - 1;
                if (ti < (int)tracks.size()) firstArg = juce::String(tracks[ti].id);
            }

            juce::var args;
            if (firstArg.isNotEmpty()) args.append(firstArg);
            auto argsJson = juce::JSON::toString(args, true).toStdString();

            auto* song = state.currentSong();
            if (!song) return;

            if (!capBindId.empty()) state.removeBinding(capBindId);

            auto bindingId = state.addBinding(song->id, capCtrlType, channel, number,
                                               actions[ai].id, argsJson, controlName, capDevId);
            if (asScoreStep) {
                int nextPos = (int)scoreRows.size() + 1;
                state.setBindingAsScoreStep(bindingId, nextPos);
            }
            refresh();
        });
}

void MappingPane::showControlPicker(bool forScore, juce::Point<int> screenPos) {
    // TODO: device→control submenu for [+] buttons
}

// --- Learn Mode ---

void MappingPane::startLearn() {
    isLearning = true;
    learnButton.setButtonText("Stop");
    armLearnCapture();
}

void MappingPane::cancelLearn() {
    isLearning = false;
    learnButton.setButtonText("Learn");
    coordinator.cancelMidiLearn();
}

void MappingPane::armLearnCapture() {
    coordinator.startMidiLearn("",
        [this](const std::string& type, int ch, int num) {
            juce::MessageManager::callAsync([this, type, ch, num] {
                onLearnCapture(type, ch, num, "");
            });
        });
}

void MappingPane::onLearnCapture(const std::string& type, int channel, int number,
                                  const std::string& portName) {
    if (!isLearning) return;
    auto& devices = state.allDevices();
    if (devices.empty()) { if (isLearning) armLearnCapture(); return; }

    auto& targetDevice = devices[0];
    for (auto& ctrl : targetDevice.controls)
        if (ctrl.controlType == type && ctrl.channel == channel && ctrl.number == number) {
            if (isLearning) armLearnCapture();
            return;
        }

    juce::String defaultName;
    if (type == "cc") defaultName = "CC " + juce::String(number);
    else if (type == "note") defaultName = "Note " + juce::String(number);
    else defaultName = "Control";

    state.addDeviceControl(targetDevice.id, defaultName.toStdString(), type, channel, number);
    refresh();
    if (isLearning) armLearnCapture();
}

// --- Activity ---

void MappingPane::onMidiEvent(const std::string& type, int channel, int number,
                                const std::string& deviceId) {
    auto now = juce::Time::currentTimeMillis();
    for (auto& e : leftEntries)
        if (e.type == LeftPanelEntry::Control && e.deviceId == deviceId
            && e.controlType == type && e.channel == channel && e.number == number)
            e.lastActivityMs = now;
    for (auto& mr : mappingRows)
        if (mr.deviceId == deviceId && mr.controlType == type
            && mr.channel == channel && mr.number == number)
            mr.lastActivityMs = now;
    for (auto& sr : scoreRows)
        if (sr.deviceId == deviceId && sr.controlType == type
            && sr.channel == channel && sr.number == number)
            sr.lastActivityMs = now;
}

void MappingPane::timerCallback() { repaint(); }
