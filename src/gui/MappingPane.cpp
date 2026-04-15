#include "gui/MappingPane.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"
#include <set>

MappingPane::MappingPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator)
    : state(state), engine(engine), coordinator(coordinator) {

    stateSubscriptionId = state.events().subscribe([this](const StateEvent&) {
        juce::MessageManager::callAsync([this] { refresh(); });
    });

    // Global MIDI monitor for activity dots
    coordinator.setGlobalMidiMonitor(
        [this](const std::string& deviceName, const std::string&,
               const std::string& evType, int ch, int num, int) {
            auto* dev = this->state.findDeviceByPortName(deviceName);
            if (dev) {
                auto devId = dev->id;
                // Normalize event types to match ControlDef types
                std::string ctrlType;
                if (evType == "CC") ctrlType = "cc";
                else if (evType == "NoteOn" || evType == "NoteOff") ctrlType = "note";
                else if (evType == "Pitch") ctrlType = "pitchbend";
                else if (evType == "Pressure") ctrlType = "pressure";
                if (!ctrlType.empty()) {
                    juce::MessageManager::callAsync([this, ctrlType, ch, num, devId] {
                        onMidiEvent(ctrlType, ch, num, devId);
                    });
                }
            }
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
        bool collapsed = collapsedDevices.count(dev.id) > 0;

        // Device header — always shown
        LeftPanelEntry header;
        header.type = LeftPanelEntry::DeviceHeader;
        header.deviceId = dev.id;
        header.deviceName = dev.name;
        header.deviceConnected = connected;
        leftEntries.push_back(header);

        // All controls — shown when not collapsed, sorted by group then name
        if (!collapsed) {
            std::vector<LeftPanelEntry> deviceControls;
            for (int ci = 0; ci < (int)dev.controls.size(); ++ci) {
                auto& ctrl = dev.controls[ci];
                bool bound = boundControls.find({ dev.id, ctrl.controlType, ctrl.channel, ctrl.number }) != boundControls.end();

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
                entry.isBound = bound;
                deviceControls.push_back(entry);
            }
            std::sort(deviceControls.begin(), deviceControls.end(),
                [](const LeftPanelEntry& a, const LeftPanelEntry& b) {
                    if (a.group != b.group) return a.group < b.group;
                    return a.controlName < b.controlName;
                });
            for (auto& e : deviceControls)
                leftEntries.push_back(std::move(e));
        }

        // Build mapping/score rows from bound controls
        for (int ci = 0; ci < (int)dev.controls.size(); ++ci) {
            auto& ctrl = dev.controls[ci];
            auto it = boundControls.find({ dev.id, ctrl.controlType, ctrl.channel, ctrl.number });
            if (it == boundControls.end()) continue;

            auto* binding = it->second;
            auto* action = binding->actionId.empty() ? nullptr : state.findActionById(binding->actionId);
            std::string label = binding->actionId.empty() ? "" : (action ? (action->label.empty() ? action->name : action->label) : "?");

            // Build source label: "Device Group Control" or "Device Control"
            std::string srcLabel = dev.name;
            if (!ctrl.group.empty()) srcLabel += " " + ctrl.group;
            srcLabel += " " + ctrl.name;

            if (binding->isScoreStep) {
                ScoreRow sr;
                sr.deviceId = dev.id;
                sr.deviceName = dev.name;
                sr.controlName = srcLabel;
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
                mr.controlName = srcLabel;
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

void MappingPane::refresh() { buildRows(); resized(); repaint(); }

// --- Utility ---

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
    leftPanelBounds = area.removeFromLeft(leftPanelWidth);

    // Score grows to fit content + one empty slot, up to half the available height
    int scoreContent = sectionTitleHeight + ((int)scoreRows.size() + 1) * rowHeight;
    int scoreHeight = std::max(scoreMinHeight, scoreContent + 8);
    scoreHeight = std::min(scoreHeight, area.getHeight() / 2);

    scorePanelBounds = area.removeFromBottom(scoreHeight);
    mappingPanelBounds = area;  // takes the rest
}

// --- Paint ---

void MappingPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)getWidth() - 1, 0.0f, (float)getWidth() - 1, (float)getHeight(), 1.0f);

    paintLeftPanel(g);
    paintMappingPanel(g);
    paintScorePanel(g);

    // Drag ghost + drop target highlight
    bool draggingLeft = isDragging && dragSourceLeftRow >= 0 && dragSourceLeftRow < (int)leftEntries.size();
    bool draggingMapping = isDragging && dragSourceMappingRow >= 0 && dragSourceMappingRow < (int)mappingRows.size();
    bool draggingScore = isDragging && dragSourceScoreRow >= 0 && dragSourceScoreRow < (int)scoreRows.size();

    if (draggingLeft || draggingMapping || draggingScore) {
        if (currentDropTarget == DropTarget::Mappings && (draggingLeft || draggingScore)) {
            g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.15f));
            g.fillRect(mappingPanelBounds);
        }
        if (currentDropTarget == DropTarget::Score) {
            g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.15f));
            g.fillRect(scorePanelBounds);
        }
        // Score insertion line for any drop into score
        if (currentDropTarget == DropTarget::Score) {
            int dropY = scorePanelBounds.getY() + sectionTitleHeight;
            for (int i = 0; i < (int)scoreRows.size(); ++i) {
                if (dragCurrent.y < dropY + rowHeight / 2) break;
                dropY += rowHeight;
            }
            g.setColour(Theme::color(Theme::Color::accent));
            g.fillRect(scorePanelBounds.getX() + 4, dropY - 1, scorePanelBounds.getWidth() - 8, 2);
        }

        juce::String ghostLabel;
        if (draggingLeft) ghostLabel = juce::String(leftEntries[dragSourceLeftRow].controlName);
        else if (draggingMapping) ghostLabel = juce::String(mappingRows[dragSourceMappingRow].controlName);
        else ghostLabel = juce::String(scoreRows[dragSourceScoreRow].controlName);

        auto ghostBounds = juce::Rectangle<int>(dragCurrent.x - 60, dragCurrent.y - 10, 120, 20);
        g.setColour(Theme::color(Theme::Color::bgPanel).withAlpha(0.9f));
        g.fillRoundedRectangle(ghostBounds.toFloat(), 4.0f);
        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(ghostLabel, ghostBounds, juce::Justification::centred);
    }
}

void MappingPane::paintLeftPanel(juce::Graphics& g) {
    auto area = leftPanelBounds;
    g.setColour(Theme::color(Theme::Color::bgApp));
    g.fillRect(area);

    // Header background
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), leftHeaderHeight);

    // Title
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(16.0f));
    g.drawText("Controllers", area.getX() + panelPadding, area.getY() + 10,
               160, 22, juce::Justification::centredLeft);

    // Learn button
    int learnW = isLearning ? 42 : 126;
    auto learnBounds = juce::Rectangle<int>(area.getRight() - learnW - 8, area.getY() + 10, learnW, 22);
    g.setColour(isLearning ? Theme::color(Theme::Color::accent) : Theme::color(Theme::Color::textDim));
    g.drawRoundedRectangle(learnBounds.toFloat(), 3.0f, 1.0f);
    g.setFont(Theme::font(12.0f));
    g.drawText(isLearning ? "Stop" : "Learn new controls", learnBounds, juce::Justification::centred);

    // "In Song" column header — sits in the device-dark band just below the header
    int inSongHeaderY = area.getY() + leftHeaderHeight;
    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(Theme::fontSizeXs));
    g.drawText("In Song", area.getRight() - 58, inSongHeaderY, 50, 16, juce::Justification::centredRight);

    int contentTop = area.getY() + leftHeaderHeight;
    int y = contentTop - leftScrollOffset;
    for (int i = 0; i < (int)leftEntries.size(); ++i) {
        if (y > area.getBottom()) break;
        auto& entry = leftEntries[i];

        if (entry.type == LeftPanelEntry::DeviceHeader) {
            if (y + rowHeight >= contentTop) {
                bool collapsed = collapsedDevices.count(entry.deviceId) > 0;

                // Disclosure triangle
                g.setColour(Theme::color(Theme::Color::textDim));
                g.setFont(Theme::font(Theme::fontSizeXs));
                g.drawText(collapsed ? juce::CharPointer_UTF8("\xe2\x96\xb6")
                                     : juce::CharPointer_UTF8("\xe2\x96\xbc"),
                           area.getX() + 6, y, 12, rowHeight, juce::Justification::centred);

                // Connection dot
                float dotY = (float)(y + rowHeight / 2 - 3);
                g.setColour(entry.deviceConnected ? juce::Colour(0xff44cc44) : juce::Colour(0xff555555));
                g.fillEllipse((float)(area.getX() + 20), dotY, 6.0f, 6.0f);

                g.setColour(Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(12.0f));
                g.drawText(juce::String(entry.deviceName), area.getX() + 30, y,
                           area.getWidth() - 34, rowHeight, juce::Justification::centredLeft);
            }
        } else {
            if (y + rowHeight >= contentTop) {
                bool hovered = (i == hoveredLeftRow);

                auto now = juce::Time::currentTimeMillis();
                bool flashing = (entry.flashMs > 0 && now - entry.flashMs < 600);
                if (flashing) {
                    float t = (float)(now - entry.flashMs) / 600.0f;
                    auto flashCol = Theme::color(Theme::Color::accent).withAlpha(0.3f * (1.0f - t));
                    g.setColour(flashCol);
                    g.fillRect(area.getX(), y, area.getWidth(), rowHeight);
                } else if (hovered) {
                    g.setColour(Theme::color(Theme::Color::bgApp).brighter(0.08f));
                    g.fillRect(area.getX(), y, area.getWidth(), rowHeight);
                }

                // Activity dot
                bool active = (entry.lastActivityMs > 0 && now - entry.lastActivityMs < 300);
                g.setColour(active ? juce::Colour(0xff44cc44) : juce::Colour(0xff1a3a1a));
                g.fillEllipse((float)(area.getX() + 32), (float)(y + rowHeight / 2 - 3), 6.0f, 6.0f);

                // Layout: [dot 42] [name ... groupFieldX] [group field ... checkX] [check 40]
                int checkX = area.getRight() - 40;
                int groupFieldW = 60;
                int groupFieldX = checkX - groupFieldW;
                int nameX = area.getX() + 42;

                // Control name
                g.setColour(entry.isBound ? Theme::color(Theme::Color::textDim)
                                          : Theme::color(Theme::Color::textSecondary));
                g.setFont(Theme::font(Theme::fontSizeSm));
                g.drawText(juce::String(entry.controlName), nameX, y,
                           groupFieldX - nameX - 4, rowHeight, juce::Justification::centredLeft);

                // Group field (clickable)
                if (!entry.group.empty()) {
                    g.setColour(Theme::color(Theme::Color::textDim));
                    g.setFont(Theme::font(Theme::fontSizeXs));
                    g.drawText(juce::String(entry.group), groupFieldX, y,
                               groupFieldW, rowHeight, juce::Justification::centredRight);
                } else {
                    g.setColour(Theme::color(Theme::Color::textDim).withAlpha(0.3f));
                    g.setFont(Theme::font(Theme::fontSizeXs));
                    g.drawText("group", groupFieldX, y,
                               groupFieldW, rowHeight, juce::Justification::centredRight);
                }

                // "In Song" checkmark
                if (entry.isBound) {
                    g.setColour(Theme::color(Theme::Color::accent));
                    g.setFont(Theme::font(12.0f));
                    g.drawText(juce::CharPointer_UTF8("\xe2\x9c\x93"),
                               checkX, y, 30, rowHeight, juce::Justification::centred);
                }
            }
        }
        y += rowHeight;
    }

    // Redraw header over scrolled content
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), leftHeaderHeight);
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(16.0f));
    g.drawText("Controllers", area.getX() + panelPadding, area.getY() + 10,
               160, 22, juce::Justification::centredLeft);
    g.setColour(isLearning ? Theme::color(Theme::Color::accent) : Theme::color(Theme::Color::textDim));
    g.drawRoundedRectangle(learnBounds.toFloat(), 3.0f, 1.0f);
    g.setFont(Theme::font(12.0f));
    g.drawText(isLearning ? "Stop" : "Learn new controls", learnBounds, juce::Justification::centred);

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getRight(), (float)area.getY(),
               (float)area.getRight(), (float)area.getBottom(), 1.0f);
}

void MappingPane::paintMappingPanel(juce::Graphics& g) {
    auto area = mappingPanelBounds;

    // Main header: "Song Mappings"
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), rightHeaderHeight);
    auto* song = state.currentSong();
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(16.0f));
    juce::String mainTitle = song ? "Song Mappings: " + juce::String(song->name) : "Song Mappings";
    g.drawText(mainTitle, area.getX() + panelPadding, area.getY() + 10, 300, 22,
               juce::Justification::centredLeft);

    // Subsection: "Atemporal"
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(14.0f));
    g.drawText("Atemporal", area.getX() + panelPadding, area.getY() + rightHeaderHeight + 2, 200, 18,
               juce::Justification::centredLeft);
    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(11.0f));
    g.drawText("Mappings active throughout the song", area.getX() + panelPadding,
               area.getY() + rightHeaderHeight + 18, 300, 14, juce::Justification::centredLeft);

    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(16.0f));
    g.drawText("+", area.getRight() - 30, area.getY() + rightHeaderHeight + 2, 20, 18, juce::Justification::centred);

    int y = area.getY() + rightHeaderHeight + sectionTitleHeight - mappingScrollOffset;
    auto now = juce::Time::currentTimeMillis();

    if (mappingRows.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(11.0f));
        g.drawText("Drag controls here or click +",
                   area.getX() + 20, y, area.getWidth() - 40, 40, juce::Justification::centred);
    }

    for (int i = 0; i < (int)mappingRows.size(); ++i) {
        if (y > area.getBottom()) break;
        if (y + rowHeight < area.getY() + rightHeaderHeight + sectionTitleHeight) { y += rowHeight; continue; }

        auto& mr = mappingRows[i];
        bool hovered = (i == hoveredMappingRow);

        g.setColour(hovered ? juce::Colour(0xff3a3a3a) : juce::Colour(0xff2e2e2e));
        g.fillRect(area.getX() + 4, y, area.getWidth() - 8, rowHeight - 1);

        bool active = (mr.lastActivityMs > 0 && now - mr.lastActivityMs < 300);
        g.setColour(active ? juce::Colour(0xff44cc44) : juce::Colour(0xff1a3a1a));
        g.fillEllipse((float)(area.getX() + 10), (float)(y + rowHeight / 2 - 3), 6.0f, 6.0f);

        // Source: "Device Group Control"
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        int sourceWidth = std::min(180, (area.getWidth() - 40) / 2);
        g.drawText(juce::String(mr.controlName), area.getX() + 22, y, sourceWidth, rowHeight,
                   juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::textDim));
        g.drawText(juce::CharPointer_UTF8("\xe2\x86\x92"),
                   area.getX() + 22 + sourceWidth, y, 20, rowHeight, juce::Justification::centred);

        int actionX = area.getX() + 22 + sourceWidth + 20;
        if (mr.actionLabel.empty()) {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.drawText("Click to assign", actionX, y, area.getRight() - actionX - 8, rowHeight,
                       juce::Justification::centredLeft);
        } else {
            g.setColour(Theme::color(Theme::Color::textPrimary));
            auto actionText = juce::String(mr.actionLabel);
            if (!mr.argsDisplay.empty()) actionText += ": " + juce::String(mr.argsDisplay);
            g.drawText(actionText, actionX, y, area.getRight() - actionX - 8, rowHeight,
                       juce::Justification::centredLeft);
        }

        y += rowHeight;
    }

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getX(), (float)area.getBottom(),
               (float)area.getRight(), (float)area.getBottom(), 1.0f);
}

void MappingPane::paintScorePanel(juce::Graphics& g) {
    auto area = scorePanelBounds;

    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(14.0f));
    g.drawText("Score", area.getX() + panelPadding, area.getY() + 4, 200, 18,
               juce::Justification::centredLeft);
    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(11.0f));
    g.drawText("Ordered one-time actions performed during this song",
               area.getX() + panelPadding, area.getY() + 20, area.getWidth() - 40, 14,
               juce::Justification::centredLeft);

    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(16.0f));
    g.drawText("+", area.getRight() - 30, area.getY() + 4, 20, 18, juce::Justification::centred);

    int y = area.getY() + sectionTitleHeight;
    auto now = juce::Time::currentTimeMillis();

    if (scoreRows.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(11.0f));
        g.drawText("Drag mappings here or click +",
                   area.getX() + 20, y, area.getWidth() - 40, 40, juce::Justification::centred);
    }

    for (int i = 0; i < (int)scoreRows.size(); ++i) {
        if (y > area.getBottom()) break;

        auto& sr = scoreRows[i];
        bool hovered = (i == hoveredScoreRow);

        g.setColour(hovered ? juce::Colour(0xff3a3a3a) : juce::Colour(0xff2e2e2e));
        g.fillRect(area.getX() + 4, y, area.getWidth() - 8, rowHeight - 1);

        // Score position badge
        auto badge = juce::Rectangle<int>(area.getX() + 8, y + 4, 20, rowHeight - 8);
        g.setColour(Theme::color(Theme::Color::accent));
        g.fillRoundedRectangle(badge.toFloat(), 4.0f);
        g.setColour(Theme::color(Theme::Color::textOnColor));
        g.setFont(Theme::font(Theme::fontSizeXs));
        g.drawText(juce::String(sr.scorePosition), badge, juce::Justification::centred);

        // Activity dot
        bool active = (sr.lastActivityMs > 0 && now - sr.lastActivityMs < 300);
        g.setColour(active ? juce::Colour(0xff44cc44) : juce::Colour(0xff1a3a1a));
        g.fillEllipse((float)(area.getX() + 34), (float)(y + rowHeight / 2 - 3), 6.0f, 6.0f);

        // Source: "Device Group Control"
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        int sourceWidth = std::min(180, (area.getWidth() - 60) / 2);
        g.drawText(juce::String(sr.controlName), area.getX() + 46, y, sourceWidth, rowHeight,
                   juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::textDim));
        g.drawText(juce::CharPointer_UTF8("\xe2\x86\x92"),
                   area.getX() + 46 + sourceWidth, y, 20, rowHeight, juce::Justification::centred);

        int actionX = area.getX() + 46 + sourceWidth + 20;
        if (sr.actionLabel.empty()) {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.drawText("Click to assign", actionX, y, area.getRight() - actionX - 8, rowHeight,
                       juce::Justification::centredLeft);
        } else {
            g.setColour(Theme::color(Theme::Color::textPrimary));
            auto actionText = juce::String(sr.actionLabel);
            if (!sr.argsDisplay.empty()) actionText += ": " + juce::String(sr.argsDisplay);
            g.drawText(actionText, actionX, y, area.getRight() - actionX - 8, rowHeight,
                       juce::Justification::centredLeft);
        }

        y += rowHeight;
    }
}

// --- Interactions ---

void MappingPane::mouseDown(const juce::MouseEvent& event) {
    // Clicking outside inline editor dismisses it
    if (editingLeftRow >= 0 && !inlineEditor.getBounds().contains(event.getPosition())) {
        inlineEditor.commit();
        return;
    }

    auto pos = event.getPosition();
    if (event.mods.isPopupMenu()) return;

    // Start drag from left panel control (only unbound controls)
    if (leftPanelBounds.contains(pos)) {
        int y = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
            if (pos.getY() >= y && pos.getY() < y + rowHeight && !leftEntries[i].isBound) {
                dragSourceLeftRow = i;
                dragStart = pos;
                return;
            }
            y += rowHeight;
        }
    }

    // Start drag from mapping row (to score)
    if (mappingPanelBounds.contains(pos)) {
        int y = mappingPanelBounds.getY() + rightHeaderHeight + sectionTitleHeight - mappingScrollOffset;
        for (int i = 0; i < (int)mappingRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                dragSourceMappingRow = i;
                dragStart = pos;
                return;
            }
            y += rowHeight;
        }
    }

    // Start drag from score row (reorder or move to any-time)
    if (scorePanelBounds.contains(pos)) {
        int y = scorePanelBounds.getY() + sectionTitleHeight;
        for (int i = 0; i < (int)scoreRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                dragSourceScoreRow = i;
                dragStart = pos;
                return;
            }
            y += rowHeight;
        }
    }

    dragSourceLeftRow = -1;
    dragSourceMappingRow = -1;
    dragSourceScoreRow = -1;
}

void MappingPane::mouseDrag(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    if (dragSourceLeftRow < 0 && dragSourceMappingRow < 0 && dragSourceScoreRow < 0) return;

    if (!isDragging && dragStart.getDistanceFrom(pos) > 5)
        isDragging = true;
    if (!isDragging) return;

    dragCurrent = pos;

    if (mappingPanelBounds.contains(pos))
        currentDropTarget = DropTarget::Mappings;
    else if (scorePanelBounds.contains(pos))
        currentDropTarget = DropTarget::Score;
    else
        currentDropTarget = DropTarget::None;

    repaint();
}

void MappingPane::mouseUp(const juce::MouseEvent& event) {
    auto pos = event.getPosition();

    // Helper: compute score insertion index from cursor Y
    auto scoreInsertIndex = [this](int cursorY) {
        int dropY = scorePanelBounds.getY() + sectionTitleHeight;
        int insertAt = 0;
        for (int i = 0; i < (int)scoreRows.size(); ++i) {
            if (cursorY < dropY + rowHeight / 2) break;
            insertAt = i + 1;
            dropY += rowHeight;
        }
        return insertAt;
    };

    // Handle drag drop from left panel
    if (isDragging && dragSourceLeftRow >= 0 && dragSourceLeftRow < (int)leftEntries.size()) {
        auto& entry = leftEntries[dragSourceLeftRow];
        if (currentDropTarget != DropTarget::None) {
            auto* song = state.currentSong();
            if (song) {
                auto bindingId = state.addBinding(song->id, entry.controlType, entry.channel,
                                                   entry.number, "", "[]", entry.controlName,
                                                   entry.deviceId);
                if (currentDropTarget == DropTarget::Score) {
                    // Insert at cursor position, bump existing
                    int insertAt = scoreInsertIndex(pos.getY());
                    for (int i = (int)scoreRows.size() - 1; i >= insertAt; --i)
                        state.setBindingAsScoreStep(scoreRows[i].bindingId, i + 2);
                    state.setBindingAsScoreStep(bindingId, insertAt + 1);
                }
                refresh();
            }
        }
        isDragging = false;
        dragSourceLeftRow = -1;
        dragSourceMappingRow = -1;
        dragSourceScoreRow = -1;
        currentDropTarget = DropTarget::None;
        repaint();
        return;
    }

    // Handle drag drop from mapping row to score
    if (isDragging && dragSourceMappingRow >= 0 && dragSourceMappingRow < (int)mappingRows.size()) {
        if (currentDropTarget == DropTarget::Score) {
            auto& mr = mappingRows[dragSourceMappingRow];
            int insertAt = scoreInsertIndex(pos.getY());
            for (int i = (int)scoreRows.size() - 1; i >= insertAt; --i)
                state.setBindingAsScoreStep(scoreRows[i].bindingId, i + 2);
            state.setBindingAsScoreStep(mr.bindingId, insertAt + 1);
            refresh();
        }
        isDragging = false;
        dragSourceLeftRow = -1;
        dragSourceMappingRow = -1;
        dragSourceScoreRow = -1;
        currentDropTarget = DropTarget::None;
        repaint();
        return;
    }

    // Handle drag from score row (reorder or move to any-time)
    if (isDragging && dragSourceScoreRow >= 0 && dragSourceScoreRow < (int)scoreRows.size()) {
        auto& sr = scoreRows[dragSourceScoreRow];
        if (currentDropTarget == DropTarget::Mappings) {
            state.clearScoreStep(sr.bindingId);
            // Renumber remaining
            int p = 1;
            for (int i = 0; i < (int)scoreRows.size(); ++i)
                if (i != dragSourceScoreRow)
                    state.setBindingAsScoreStep(scoreRows[i].bindingId, p++);
            refresh();
        } else if (currentDropTarget == DropTarget::Score) {
            int insertAt = scoreInsertIndex(pos.getY());
            // Build new order: remove dragged, insert at position
            std::vector<std::string> ordered;
            for (int i = 0; i < (int)scoreRows.size(); ++i)
                if (i != dragSourceScoreRow) ordered.push_back(scoreRows[i].bindingId);
            int ins = std::min(insertAt, (int)ordered.size());
            if (dragSourceScoreRow < insertAt) ins = std::max(0, ins - 1);
            ordered.insert(ordered.begin() + ins, sr.bindingId);
            for (int i = 0; i < (int)ordered.size(); ++i)
                state.setBindingAsScoreStep(ordered[i], i + 1);
            refresh();
        }
        isDragging = false;
        dragSourceLeftRow = -1;
        dragSourceMappingRow = -1;
        dragSourceScoreRow = -1;
        currentDropTarget = DropTarget::None;
        repaint();
        return;
    }

    isDragging = false;
    dragSourceLeftRow = -1;
    dragSourceMappingRow = -1;
    dragSourceScoreRow = -1;
    currentDropTarget = DropTarget::None;

    // Click device header → toggle collapse
    if (!event.mods.isPopupMenu() && leftPanelBounds.contains(pos)) {
        int y = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (leftEntries[i].type == LeftPanelEntry::DeviceHeader) {
                if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                    auto& devId = leftEntries[i].deviceId;
                    if (collapsedDevices.count(devId))
                        collapsedDevices.erase(devId);
                    else
                        collapsedDevices.insert(devId);
                    buildRows();
                    repaint();
                    return;
                }
            }
            y += rowHeight;
        }
    }

    // Click group field on left panel control
    if (!event.mods.isPopupMenu() && leftPanelBounds.contains(pos)) {
        int checkX = leftPanelBounds.getRight() - 40;
        int groupFieldW = 60;
        int groupFieldX = checkX - groupFieldW;

        int y = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
            if (pos.getY() >= y && pos.getY() < y + rowHeight && pos.getX() >= groupFieldX && pos.getX() < checkX) {
                auto devId = leftEntries[i].deviceId;
                auto ctrlIdx = leftEntries[i].controlIndex;
                auto entryIdx = i;

                std::set<std::string> groups;
                auto* dev = state.findDevice(devId);
                if (dev) {
                    for (auto& c : dev->controls)
                        if (!c.group.empty()) groups.insert(c.group);
                }

                juce::PopupMenu menu;
                menu.addItem(100, "New Group...");
                if (!leftEntries[i].group.empty())
                    menu.addItem(101, "No Group");
                if (!groups.empty()) menu.addSeparator();
                int gid = 200;
                std::vector<std::string> groupList(groups.begin(), groups.end());
                for (auto& g : groupList)
                    menu.addItem(gid++, juce::String(g));

                auto screenArea = juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea),
                    [this, devId, ctrlIdx, entryIdx, groupList](int r) {
                        if (r == 100) {
                            int ey = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
                            for (int j = 0; j < entryIdx; ++j) ey += rowHeight;
                            int checkXl = leftPanelBounds.getRight() - 40;
                            int gfW = 60;
                            auto bounds = juce::Rectangle<int>(checkXl - gfW, ey, gfW, rowHeight);
                            inlineEditor.onCommit = [this, devId, ctrlIdx](const juce::String& text) {
                                state.setDeviceControlGroup(devId, ctrlIdx, text.toStdString());
                                editingLeftRow = -1;
                                refresh();
                            };
                            inlineEditor.onCancel = [this] { editingLeftRow = -1; repaint(); };
                            inlineEditor.onCommitNext = nullptr;
                            inlineEditor.onCommitPrev = nullptr;
                            editingLeftRow = entryIdx;
                            inlineEditor.show(*this, bounds, "");
                        } else if (r == 101) {
                            state.setDeviceControlGroup(devId, ctrlIdx, "");
                            refresh();
                        } else if (r >= 200 && r - 200 < (int)groupList.size()) {
                            state.setDeviceControlGroup(devId, ctrlIdx, groupList[r - 200]);
                            refresh();
                        }
                    });
                return;
            }
            y += rowHeight;
        }
    }

    // Learn button in left panel header
    if (!event.mods.isPopupMenu() && leftPanelBounds.contains(pos)) {
        int learnW = isLearning ? 42 : 126;
        auto learnBounds = juce::Rectangle<int>(leftPanelBounds.getRight() - learnW - 8,
                                                 leftPanelBounds.getY() + 10, learnW, 22);
        if (learnBounds.contains(pos)) {
            if (isLearning) cancelLearn(); else startLearn();
            repaint();
            return;
        }
    }

    // Right-click left panel control → set group / delete
    if (event.mods.isPopupMenu() && leftPanelBounds.contains(pos)) {
        int y = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                auto devId = leftEntries[i].deviceId;
                auto ctrlIdx = leftEntries[i].controlIndex;
                auto entryIdx = i;

                // Collect existing groups for this device
                std::set<std::string> groups;
                auto* dev = state.findDevice(devId);
                if (dev) {
                    for (auto& c : dev->controls)
                        if (!c.group.empty()) groups.insert(c.group);
                }

                juce::PopupMenu groupMenu;
                groupMenu.addItem(100, "New Group...");
                if (!leftEntries[i].group.empty())
                    groupMenu.addItem(101, "Remove Group");
                if (!groups.empty()) groupMenu.addSeparator();
                int gid = 200;
                std::vector<std::string> groupList(groups.begin(), groups.end());
                for (auto& g : groupList)
                    groupMenu.addItem(gid++, juce::String(g));

                juce::PopupMenu menu;
                menu.addSubMenu("Set Group", groupMenu);
                menu.addItem(1, "Delete Control");

                auto screenArea = juce::Rectangle<int>(event.getScreenX(), event.getScreenY(), 1, 1);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(screenArea),
                    [this, devId, ctrlIdx, entryIdx, groupList](int r) {
                        if (r == 1) {
                            state.removeDeviceControl(devId, ctrlIdx);
                            refresh();
                        } else if (r == 100) {
                            // New Group — open inline editor for group name
                            int ey = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
                            for (int j = 0; j < entryIdx; ++j) ey += rowHeight;
                            auto bounds = juce::Rectangle<int>(leftPanelBounds.getX() + 30, ey,
                                                                leftPanelBounds.getWidth() - 70, rowHeight);
                            inlineEditor.onCommit = [this, devId, ctrlIdx](const juce::String& text) {
                                state.setDeviceControlGroup(devId, ctrlIdx, text.toStdString());
                                editingLeftRow = -1;
                                refresh();
                            };
                            inlineEditor.onCancel = [this] { editingLeftRow = -1; repaint(); };
                            inlineEditor.onCommitNext = nullptr;
                            inlineEditor.onCommitPrev = nullptr;
                            editingLeftRow = entryIdx;
                            inlineEditor.show(*this, bounds, "");
                        } else if (r == 101) {
                            state.setDeviceControlGroup(devId, ctrlIdx, "");
                            refresh();
                        } else if (r >= 200 && r - 200 < (int)groupList.size()) {
                            state.setDeviceControlGroup(devId, ctrlIdx, groupList[r - 200]);
                            refresh();
                        }
                    });
                return;
            }
            y += rowHeight;
        }
    }

    // Right-click mapping row → remove
    if (event.mods.isPopupMenu() && mappingPanelBounds.contains(pos)) {
        int y = mappingPanelBounds.getY() + rightHeaderHeight + sectionTitleHeight - mappingScrollOffset;
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
        int y = scorePanelBounds.getY() + sectionTitleHeight;
        for (int i = 0; i < (int)scoreRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                auto bindingId = scoreRows[i].bindingId;
                juce::PopupMenu menu;
                menu.addItem(1, "Move to Atemporal");
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
            y += rowHeight;
        }
    }

    // [+] button on mappings panel
    if (!event.mods.isPopupMenu() && mappingPanelBounds.contains(pos)) {
        auto plusBounds = juce::Rectangle<int>(mappingPanelBounds.getRight() - 30,
                                               mappingPanelBounds.getY() + 4, 20, 20);
        if (plusBounds.contains(pos)) {
            showControlPicker(false, event.getScreenPosition());
            return;
        }
    }

    // [+] button on score panel
    if (!event.mods.isPopupMenu() && scorePanelBounds.contains(pos)) {
        auto plusBounds = juce::Rectangle<int>(scorePanelBounds.getRight() - 30,
                                               scorePanelBounds.getY() + 4, 20, 20);
        if (plusBounds.contains(pos)) {
            showControlPicker(true, event.getScreenPosition());
            return;
        }
    }

    // Click action on mapping row → assign/reassign
    if (!event.mods.isPopupMenu() && mappingPanelBounds.contains(pos)) {
        int y = mappingPanelBounds.getY() + rightHeaderHeight + sectionTitleHeight - mappingScrollOffset;
        for (int i = 0; i < (int)mappingRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                auto& mr = mappingRows[i];
                int sourceWidth = std::min(180, (mappingPanelBounds.getWidth() - 40) / 2);
                int actionX = mappingPanelBounds.getX() + 22 + sourceWidth + 20;
                if (mr.actionLabel.empty() || pos.getX() > actionX) {
                    showActionMenu(mr.deviceId, mr.controlType, mr.channel, mr.number,
                                   mr.controlName, mr.bindingId, event.getScreenPosition(), false);
                    return;
                }
            }
            y += rowHeight;
        }
    }

    // Click action on score row → assign/reassign
    if (!event.mods.isPopupMenu() && scorePanelBounds.contains(pos)) {
        int y = scorePanelBounds.getY() + sectionTitleHeight;
        for (int i = 0; i < (int)scoreRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                auto& sr = scoreRows[i];
                int sourceWidth = std::min(180, (scorePanelBounds.getWidth() - 60) / 2);
                int actionX = scorePanelBounds.getX() + 46 + sourceWidth + 20;
                if (sr.actionLabel.empty() || pos.getX() > actionX) {
                    showActionMenu(sr.deviceId, sr.controlType, sr.channel, sr.number,
                                   sr.controlName, sr.bindingId, event.getScreenPosition(), true);
                    return;
                }
            }
            y += rowHeight;
        }
    }
}

void MappingPane::mouseDoubleClick(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    if (!leftPanelBounds.contains(pos)) return;

    // Only open name editor when double-clicking the name area (not group field)
    int groupFieldX = leftPanelBounds.getRight() - 40 - 60;

    int y = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
    for (int i = 0; i < (int)leftEntries.size(); ++i) {
        if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
        if (pos.getY() >= y && pos.getY() < y + rowHeight && pos.getX() < groupFieldX) {
            openLeftEditor(i);
            return;
        }
        y += rowHeight;
    }
}

void MappingPane::openLeftEditor(int entryIndex) {
    if (entryIndex < 0 || entryIndex >= (int)leftEntries.size()) return;
    if (leftEntries[entryIndex].type != LeftPanelEntry::Control) return;

    editingLeftRow = entryIndex;
    auto& entry = leftEntries[entryIndex];

    // Compute Y position for this entry
    int y = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
    for (int i = 0; i < entryIndex; ++i) y += rowHeight;

    auto bounds = juce::Rectangle<int>(leftPanelBounds.getX() + 30, y,
                                        leftPanelBounds.getWidth() - 70, rowHeight);

    auto commitRename = [this](int idx, const juce::String& newText) {
        if (idx >= 0 && idx < (int)leftEntries.size()) {
            auto& e = leftEntries[idx];
            state.renameDeviceControl(e.deviceId, e.controlIndex, newText.toStdString());
        }
        editingLeftRow = -1;
        refresh();
    };

    inlineEditor.onCommit = [this, entryIndex, commitRename](const juce::String& newText) {
        commitRename(entryIndex, newText);
    };
    inlineEditor.onCancel = [this] { editingLeftRow = -1; repaint(); };

    // Arrow navigation: commit current, open next/prev control in same device
    auto deviceId = entry.deviceId;
    inlineEditor.onCommitNext = [this, entryIndex, deviceId, commitRename](const juce::String& newText) {
        commitRename(entryIndex, newText);
        // Find next control in same device
        for (int i = entryIndex + 1; i < (int)leftEntries.size(); ++i) {
            if (leftEntries[i].type == LeftPanelEntry::DeviceHeader) break;
            if (leftEntries[i].type == LeftPanelEntry::Control && leftEntries[i].deviceId == deviceId) {
                openLeftEditor(i);
                return;
            }
        }
    };
    inlineEditor.onCommitPrev = [this, entryIndex, deviceId, commitRename](const juce::String& newText) {
        commitRename(entryIndex, newText);
        // Find prev control in same device
        for (int i = entryIndex - 1; i >= 0; --i) {
            if (leftEntries[i].type == LeftPanelEntry::DeviceHeader) break;
            if (leftEntries[i].type == LeftPanelEntry::Control && leftEntries[i].deviceId == deviceId) {
                openLeftEditor(i);
                return;
            }
        }
    };

    inlineEditor.show(*this, bounds, juce::String(entry.controlName));
}

void MappingPane::mouseMove(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    int newLeft = -1, newMapping = -1, newScore = -1;

    if (leftPanelBounds.contains(pos)) {
        int y = leftPanelBounds.getY() + leftHeaderHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) { newLeft = i; break; }
            y += rowHeight;
        }
    } else if (mappingPanelBounds.contains(pos)) {
        int y = mappingPanelBounds.getY() + rightHeaderHeight + sectionTitleHeight - mappingScrollOffset;
        for (int i = 0; i < (int)mappingRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) { newMapping = i; break; }
            y += rowHeight;
        }
    } else if (scorePanelBounds.contains(pos)) {
        int y = scorePanelBounds.getY() + sectionTitleHeight;
        for (int i = 0; i < (int)scoreRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) { newScore = i; break; }
            y += rowHeight;
        }
    }

    if (newLeft != hoveredLeftRow || newMapping != hoveredMappingRow || newScore != hoveredScoreRow) {
        hoveredLeftRow = newLeft;
        hoveredMappingRow = newMapping;
        hoveredScoreRow = newScore;
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
    repaint();
}

void MappingPane::scrollToDevice(const std::string& deviceId) {
    // Ensure uncollapsed
    collapsedDevices.erase(deviceId);
    buildRows();

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
    juce::PopupMenu menu;
    int itemId = 1;

    // Flat list grouped by device, only unbound controls
    for (auto& entry : leftEntries) {
        if (entry.type == LeftPanelEntry::DeviceHeader) {
            if (itemId > 1) menu.addSeparator();
            menu.addSectionHeader(juce::String(entry.deviceName));
        } else if (entry.type == LeftPanelEntry::Control && !entry.isBound) {
            menu.addItem(itemId, juce::String(entry.controlName));
            itemId++;
        }
    }

    if (itemId == 1) {
        menu.addItem(-1, "No unmapped controls", false);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, forScore](int result) {
            if (result <= 0) return;
            int idx = 0;
            for (auto& entry : leftEntries) {
                if (entry.type != LeftPanelEntry::Control || entry.isBound) continue;
                idx++;
                if (idx == result) {
                    auto* song = state.currentSong();
                    if (!song) return;
                    auto bindingId = state.addBinding(song->id, entry.controlType, entry.channel,
                                                       entry.number, "", "[]", entry.controlName,
                                                       entry.deviceId);
                    if (forScore) {
                        int nextPos = (int)scoreRows.size() + 1;
                        state.setBindingAsScoreStep(bindingId, nextPos);
                    }
                    refresh();
                    return;
                }
            }
        });
}

// --- Learn Mode ---

void MappingPane::startLearn() {
    isLearning = true;
    armLearnCapture();
    repaint();
}

void MappingPane::cancelLearn() {
    isLearning = false;
    coordinator.cancelMidiLearn();
    repaint();
}

void MappingPane::armLearnCapture() {
    coordinator.startMidiLearn("",
        [this](const std::string& type, int ch, int num, const std::string& portName) {
            juce::MessageManager::callAsync([this, type, ch, num, portName] {
                onLearnCapture(type, ch, num, portName);
            });
        });
}

void MappingPane::onLearnCapture(const std::string& type, int channel, int number,
                                  const std::string& portName) {
    if (!isLearning) return;

    std::string targetDeviceId;
    if (!portName.empty()) {
        // Skip virtual MIDI buses
        if (juce::String(portName).containsIgnoreCase("IAC Driver")) {
            if (isLearning) armLearnCapture();
            return;
        }
        auto* dev = state.findDeviceByPortName(portName);
        if (dev) {
            targetDeviceId = dev->id;
        } else {
            targetDeviceId = state.registerDevice(portName, portName);
        }
    }

    if (targetDeviceId.empty()) {
        if (isLearning) armLearnCapture();
        return;
    }

    auto* dev = state.findDevice(targetDeviceId);
    if (dev) {
        for (auto& ctrl : dev->controls) {
            if (ctrl.controlType == type && ctrl.channel == channel && ctrl.number == number) {
                // Already exists — flash the row
                auto now = juce::Time::currentTimeMillis();
                for (auto& entry : leftEntries)
                    if (entry.type == LeftPanelEntry::Control && entry.deviceId == targetDeviceId
                        && entry.controlType == type && entry.channel == channel && entry.number == number)
                        entry.flashMs = now;
                repaint();
                if (isLearning) armLearnCapture();
                return;
            }
        }
    }

    juce::String defaultName;
    if (type == "cc") defaultName = "CC " + juce::String(number);
    else if (type == "note") defaultName = "Note " + juce::String(number);
    else defaultName = "Control";

    state.addDeviceControl(targetDeviceId, defaultName.toStdString(), type, channel, number);
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

void MappingPane::timerCallback() {
    auto midiDevices = juce::MidiInput::getAvailableDevices();
    int count = (int)midiDevices.size();
    if (count != lastMidiDeviceCount) {
        lastMidiDeviceCount = count;
        buildRows();
    }
    repaint();
}
