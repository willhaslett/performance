#include "gui/SongMappingsPane.h"
#include "gui/ActionInstanceForm.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"
#include <set>
#include <map>
#include <algorithm>

SongMappingsPane::SongMappingsPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator)
    : state(state), engine(engine), coordinator(coordinator) {

    stateSubscriptionId = state.events().subscribe([this](const StateEvent&) {
        juce::MessageManager::callAsync([this] { refresh(); });
    });

    // Note: MainLayout installs the single global MIDI monitor and dispatches
    // to both SongMappingsPane::handleMidiActivity and
    // ControllersPane::handleMidiActivity. Don't install it here — the
    // monitor is last-writer-wins.

    startTimerHz(10);
    buildRows();
}

SongMappingsPane::~SongMappingsPane() {
    if (stateSubscriptionId >= 0)
        state.events().unsubscribe(stateSubscriptionId);
}

void SongMappingsPane::buildRows() {
    mappingRows.clear();
    scoreRows.clear();

    auto* song = state.currentSong();
    if (!song) return;

    struct ControlKey {
        DeviceId deviceId;
        std::string controlType;
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

    auto& devices = state.allDevices();
    for (int di = 0; di < (int)devices.size(); ++di) {
        auto& dev = devices[di];
        for (int ci = 0; ci < (int)dev.controls.size(); ++ci) {
            auto& ctrl = dev.controls[ci];
            auto it = boundControls.find({ dev.id, ctrl.controlType, ctrl.channel, ctrl.number });
            if (it == boundControls.end()) continue;

            auto* binding = it->second;
            auto* action = binding->actionId.empty() ? nullptr : state.findActionById(binding->actionId);
            std::string label = binding->actionId.empty() ? "" : (action ? (action->label.empty() ? action->name : action->label) : "?");

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

void SongMappingsPane::refresh() { buildRows(); resized(); repaint(); }

bool SongMappingsPane::isDeviceConnected(const std::string& deviceId) const {
    auto* dev = state.findDevice(DeviceId{deviceId});
    if (!dev) return false;
    auto midiDevices = juce::MidiInput::getAvailableDevices();
    for (auto& d : midiDevices)
        if (d.name.toStdString() == dev->midiPortName) return true;
    return false;
}

std::string SongMappingsPane::formatArgs(const std::string& argsJson) const {
    auto args = juce::JSON::parse(juce::String(argsJson));
    if (!args.isArray() || args.size() == 0) return "";
    juce::String result;
    for (int i = 0; i < std::min(args.size(), 2); ++i) {
        if (i > 0) result += ", ";
        auto val = args[i].toString();
        if (val.length() > 20) {
            auto* track = state.findTrack(TrackId{val.toStdString()});
            if (track) val = juce::String(track->name);
        }
        result += val;
    }
    return result.toStdString();
}

void SongMappingsPane::resized() {
    auto area = getLocalBounds();

    int scoreContent = sectionTitleHeight + ((int)scoreRows.size() + 1) * rowHeight;
    int scoreHeight = std::max(scoreMinHeight, scoreContent + 8);
    scoreHeight = std::min(scoreHeight, area.getHeight() / 2);

    scorePanelBounds = area.removeFromBottom(scoreHeight);
    mappingPanelBounds = area;
}

void SongMappingsPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgPanel));

    paintAtemporalSection(g);
    paintScoreSection(g);

    if (dragInProgress && currentDropTarget != DropTarget::None) {
        if (currentDropTarget == DropTarget::Mappings) {
            g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.15f));
            g.fillRect(mappingPanelBounds);
        } else if (currentDropTarget == DropTarget::Score) {
            g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.15f));
            g.fillRect(scorePanelBounds);

            int dropY = scorePanelBounds.getY() + sectionTitleHeight;
            for (int i = 0; i < (int)scoreRows.size(); ++i) {
                if (dropCursor.y < dropY + rowHeight / 2) break;
                dropY += rowHeight;
            }
            g.setColour(Theme::color(Theme::Color::accent));
            g.fillRect(scorePanelBounds.getX() + 4, dropY - 1, scorePanelBounds.getWidth() - 8, 2);
        }
    }
}

void SongMappingsPane::paintAtemporalSection(juce::Graphics& g) {
    auto area = mappingPanelBounds;

    auto* song = state.currentSong();
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeTitle));
    juce::String mainTitle = song ? "Song Mappings: " + juce::String(song->name) : "Song Mappings";
    g.drawText(mainTitle, area.getX() + panelPadding, area.getY() + 10, 400, 22,
               juce::Justification::centredLeft);

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getX(), (float)(area.getY() + rightHeaderHeight),
               (float)area.getRight(), (float)(area.getY() + rightHeaderHeight), 1.0f);

    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeLg));
    g.drawText("Atemporal", area.getX() + panelPadding, area.getY() + rightHeaderHeight + 4, 200, 18,
               juce::Justification::centredLeft);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeMd));
    g.drawText("Mappings active throughout the song", area.getX() + panelPadding,
               area.getY() + rightHeaderHeight + 20, 300, 14, juce::Justification::centredLeft);

    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeTitle));
    g.drawText("+", area.getRight() - 30, area.getY() + rightHeaderHeight + 4, 20, 18, juce::Justification::centred);

    int y = area.getY() + rightHeaderHeight + sectionTitleHeight - mappingScrollOffset;
    auto now = juce::Time::currentTimeMillis();

    if (mappingRows.empty()) {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(Theme::fontSizeLg));
        g.drawText("Drag controls here or click +",
                   area.getX() + 20, y, area.getWidth() - 40, 40, juce::Justification::centred);
    }

    for (int i = 0; i < (int)mappingRows.size(); ++i) {
        if (y > area.getBottom()) break;
        if (y + rowHeight < area.getY() + rightHeaderHeight + sectionTitleHeight) { y += rowHeight; continue; }

        auto& mr = mappingRows[i];
        bool hovered = (i == hoveredMappingRow);

        g.setColour(hovered ? Theme::color(Theme::Color::bgControlHover)
                            : Theme::color(Theme::Color::bgControl));
        g.fillRect(area.getX() + 4, y, area.getWidth() - 8, rowHeight - 1);

        bool active = (mr.lastActivityMs > 0 && now - mr.lastActivityMs < 300);
        g.setColour(active ? Theme::color(Theme::Color::activityOn)
                           : Theme::color(Theme::Color::activityOff));
        g.fillEllipse((float)(area.getX() + 10), (float)(y + rowHeight / 2 - 3), Theme::activityDotSize, Theme::activityDotSize);

        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeLg));
        int sourceWidth = std::min(200, (area.getWidth() - 40) / 2);
        g.drawText(juce::String(mr.controlName), area.getX() + 22, y, sourceWidth, rowHeight,
                   juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.drawText(juce::CharPointer_UTF8("\xe2\x86\x92"),
                   area.getX() + 22 + sourceWidth, y, 20, rowHeight, juce::Justification::centred);

        int actionX = area.getX() + 22 + sourceWidth + 20;
        if (mr.actionLabel.empty()) {
            g.setColour(Theme::color(Theme::Color::textSecondary));
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

void SongMappingsPane::paintScoreSection(juce::Graphics& g) {
    auto area = scorePanelBounds;

    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeLg));
    g.drawText("Score", area.getX() + panelPadding, area.getY() + 4, 200, 18,
               juce::Justification::centredLeft);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeMd));
    g.drawText("Ordered one-time actions performed during this song",
               area.getX() + panelPadding, area.getY() + 20, area.getWidth() - 40, 14,
               juce::Justification::centredLeft);

    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeTitle));
    g.drawText("+", area.getRight() - 30, area.getY() + 4, 20, 18, juce::Justification::centred);

    int y = area.getY() + sectionTitleHeight;
    auto now = juce::Time::currentTimeMillis();

    if (scoreRows.empty()) {
        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.setFont(Theme::font(Theme::fontSizeLg));
        g.drawText("Drag mappings here or click +",
                   area.getX() + 20, y, area.getWidth() - 40, 40, juce::Justification::centred);
    }

    for (int i = 0; i < (int)scoreRows.size(); ++i) {
        if (y > area.getBottom()) break;

        auto& sr = scoreRows[i];
        bool hovered = (i == hoveredScoreRow);

        g.setColour(hovered ? Theme::color(Theme::Color::bgControlHover)
                            : Theme::color(Theme::Color::bgControl));
        g.fillRect(area.getX() + 4, y, area.getWidth() - 8, rowHeight - 1);

        auto badge = juce::Rectangle<int>(area.getX() + 8, y + 4, 22, rowHeight - 8);
        g.setColour(Theme::color(Theme::Color::accent));
        g.fillRoundedRectangle(badge.toFloat(), Theme::cornerRadiusSm);
        g.setColour(Theme::color(Theme::Color::textOnColor));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(juce::String(sr.scorePosition), badge, juce::Justification::centred);

        bool active = (sr.lastActivityMs > 0 && now - sr.lastActivityMs < 300);
        g.setColour(active ? Theme::color(Theme::Color::activityOn)
                           : Theme::color(Theme::Color::activityOff));
        g.fillEllipse((float)(area.getX() + 36), (float)(y + rowHeight / 2 - 3), Theme::activityDotSize, Theme::activityDotSize);

        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeLg));
        int sourceWidth = std::min(200, (area.getWidth() - 60) / 2);
        g.drawText(juce::String(sr.controlName), area.getX() + 48, y, sourceWidth, rowHeight,
                   juce::Justification::centredLeft);

        g.setColour(Theme::color(Theme::Color::textSecondary));
        g.drawText(juce::CharPointer_UTF8("\xe2\x86\x92"),
                   area.getX() + 48 + sourceWidth, y, 20, rowHeight, juce::Justification::centred);

        int actionX = area.getX() + 48 + sourceWidth + 20;
        if (sr.actionLabel.empty()) {
            g.setColour(Theme::color(Theme::Color::textSecondary));
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

void SongMappingsPane::mouseDown(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    if (event.mods.isPopupMenu()) return;

    resetPendingDrag();

    if (mappingPanelBounds.contains(pos)) {
        int y = mappingPanelBounds.getY() + rightHeaderHeight + sectionTitleHeight - mappingScrollOffset;
        for (int i = 0; i < (int)mappingRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                pendingDragMappingRow = i;
                pendingDragStart = pos;
                return;
            }
            y += rowHeight;
        }
    }

    if (scorePanelBounds.contains(pos)) {
        int y = scorePanelBounds.getY() + sectionTitleHeight;
        for (int i = 0; i < (int)scoreRows.size(); ++i) {
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                pendingDragScoreRow = i;
                pendingDragStart = pos;
                return;
            }
            y += rowHeight;
        }
    }
}

void SongMappingsPane::resetPendingDrag() {
    pendingDragMappingRow = -1;
    pendingDragScoreRow = -1;
}

void SongMappingsPane::mouseDrag(const juce::MouseEvent& event) {
    if (dragInProgress) return;
    if (pendingDragMappingRow < 0 && pendingDragScoreRow < 0) return;
    if (pendingDragStart.getDistanceFrom(event.getPosition()) <= 5) return;

    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (!container) { resetPendingDrag(); return; }

    auto obj = std::make_unique<juce::DynamicObject>();
    juce::String dragLabel;
    if (pendingDragMappingRow >= 0 && pendingDragMappingRow < (int)mappingRows.size()) {
        auto& m = mappingRows[pendingDragMappingRow];
        obj->setProperty("kind", "mapping");
        obj->setProperty("bindingId", juce::String(m.bindingId.str()));
        dragLabel = juce::String(m.controlName);
    } else if (pendingDragScoreRow >= 0 && pendingDragScoreRow < (int)scoreRows.size()) {
        auto& s = scoreRows[pendingDragScoreRow];
        obj->setProperty("kind", "score");
        obj->setProperty("bindingId", juce::String(s.bindingId.str()));
        dragLabel = juce::String(s.controlName);
    } else {
        resetPendingDrag();
        return;
    }

    juce::Image img(juce::Image::ARGB, 140, 22, true);
    {
        juce::Graphics g(img);
        g.setColour(Theme::color(Theme::Color::bgPanel).withAlpha(0.9f));
        g.fillRoundedRectangle(img.getBounds().toFloat(), Theme::cornerRadiusSm);
        g.setColour(Theme::color(Theme::Color::textPrimary));
        g.setFont(Theme::font(Theme::fontSizeSm));
        g.drawText(dragLabel, img.getBounds(), juce::Justification::centred);
    }

    juce::var payload(obj.release());
    dragInProgress = true;
    container->startDragging(payload, this, juce::ScaledImage(img));
    resetPendingDrag();
}

bool SongMappingsPane::isInterestedInDragSource(const SourceDetails& details) {
    if (!details.description.isObject()) return false;
    auto* obj = details.description.getDynamicObject();
    if (!obj) return false;
    auto kind = obj->getProperty("kind").toString();
    return kind == "control" || kind == "mapping" || kind == "score";
}

void SongMappingsPane::itemDragEnter(const SourceDetails& details) {
    dragInProgress = true;
    itemDragMove(details);
}

void SongMappingsPane::itemDragMove(const SourceDetails& details) {
    auto pos = details.localPosition;
    dropCursor = pos;

    DropTarget newTarget = DropTarget::None;
    if (mappingPanelBounds.contains(pos))      newTarget = DropTarget::Mappings;
    else if (scorePanelBounds.contains(pos))   newTarget = DropTarget::Score;

    if (newTarget != currentDropTarget) {
        currentDropTarget = newTarget;
        repaint();
    } else if (newTarget == DropTarget::Score) {
        repaint();
    }
}

void SongMappingsPane::itemDragExit(const SourceDetails& /*details*/) {
    currentDropTarget = DropTarget::None;
    dragInProgress = false;
    repaint();
}

void SongMappingsPane::itemDropped(const SourceDetails& details) {
    auto* obj = details.description.getDynamicObject();
    auto finish = [this] {
        currentDropTarget = DropTarget::None;
        dragInProgress = false;
        repaint();
    };
    if (!obj) { finish(); return; }

    auto kind = obj->getProperty("kind").toString();
    auto cursor = details.localPosition;

    DropTarget target = DropTarget::None;
    if (mappingPanelBounds.contains(cursor))    target = DropTarget::Mappings;
    else if (scorePanelBounds.contains(cursor)) target = DropTarget::Score;
    if (target == DropTarget::None) { finish(); return; }

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

    if (kind == "control") {
        DeviceId deviceId{obj->getProperty("deviceId").toString().toStdString()};
        auto controlType = obj->getProperty("controlType").toString().toStdString();
        int channel      = (int)obj->getProperty("channel");
        int number       = (int)obj->getProperty("number");
        auto controlName = obj->getProperty("controlName").toString().toStdString();

        auto* song = state.currentSong();
        if (song) {
            auto bindingId = state.addBinding(song->id, controlType, channel, number,
                                              ActionId{}, "[]", controlName, deviceId);
            if (target == DropTarget::Score) {
                int insertAt = scoreInsertIndex(cursor.y);
                for (int i = (int)scoreRows.size() - 1; i >= insertAt; --i)
                    state.setBindingAsScoreStep(scoreRows[i].bindingId, i + 2);
                state.setBindingAsScoreStep(bindingId, insertAt + 1);
            }
            refresh();
        }
    } else if (kind == "mapping") {
        BindingId bindingId{obj->getProperty("bindingId").toString().toStdString()};
        if (target == DropTarget::Score) {
            int insertAt = scoreInsertIndex(cursor.y);
            for (int i = (int)scoreRows.size() - 1; i >= insertAt; --i)
                state.setBindingAsScoreStep(scoreRows[i].bindingId, i + 2);
            state.setBindingAsScoreStep(bindingId, insertAt + 1);
            refresh();
        }
    } else if (kind == "score") {
        BindingId bindingId{obj->getProperty("bindingId").toString().toStdString()};
        int sourceIndex = -1;
        for (int i = 0; i < (int)scoreRows.size(); ++i) {
            if (scoreRows[i].bindingId == bindingId) { sourceIndex = i; break; }
        }
        if (target == DropTarget::Mappings) {
            state.clearScoreStep(bindingId);
            int p = 1;
            for (int i = 0; i < (int)scoreRows.size(); ++i)
                if (i != sourceIndex)
                    state.setBindingAsScoreStep(scoreRows[i].bindingId, p++);
            refresh();
        } else if (target == DropTarget::Score && sourceIndex >= 0) {
            int insertAt = scoreInsertIndex(cursor.y);
            std::vector<BindingId> ordered;
            for (int i = 0; i < (int)scoreRows.size(); ++i)
                if (i != sourceIndex) ordered.push_back(scoreRows[i].bindingId);
            int ins = std::min(insertAt, (int)ordered.size());
            if (sourceIndex < insertAt) ins = std::max(0, ins - 1);
            ordered.insert(ordered.begin() + ins, bindingId);
            for (int i = 0; i < (int)ordered.size(); ++i)
                state.setBindingAsScoreStep(ordered[i], i + 1);
            refresh();
        }
    }

    finish();
}

void SongMappingsPane::mouseUp(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    resetPendingDrag();

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

    // [+] mappings
    if (!event.mods.isPopupMenu() && mappingPanelBounds.contains(pos)) {
        auto plusBounds = juce::Rectangle<int>(mappingPanelBounds.getRight() - 30,
                                               mappingPanelBounds.getY() + 4, 20, 20);
        if (plusBounds.contains(pos)) {
            showControlPicker(false, event.getScreenPosition());
            return;
        }
    }

    // [+] score
    if (!event.mods.isPopupMenu() && scorePanelBounds.contains(pos)) {
        auto plusBounds = juce::Rectangle<int>(scorePanelBounds.getRight() - 30,
                                               scorePanelBounds.getY() + 4, 20, 20);
        if (plusBounds.contains(pos)) {
            showControlPicker(true, event.getScreenPosition());
            return;
        }
    }

    // Click action on mapping row
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

    // Click action on score row
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

void SongMappingsPane::mouseMove(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    int newMapping = -1, newScore = -1;

    if (mappingPanelBounds.contains(pos)) {
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

    if (newMapping != hoveredMappingRow || newScore != hoveredScoreRow) {
        hoveredMappingRow = newMapping;
        hoveredScoreRow = newScore;
        repaint();
    }
}

void SongMappingsPane::mouseWheelMove(const juce::MouseEvent& event,
                                       const juce::MouseWheelDetails& wheel) {
    auto pos = event.getPosition();
    int delta = (int)(wheel.deltaY * 100);
    if (mappingPanelBounds.contains(pos))
        mappingScrollOffset = std::max(0, mappingScrollOffset - delta);
    repaint();
}

void SongMappingsPane::showActionMenu(const DeviceId& deviceId, const std::string& ctrlType,
                                       int channel, int number, const std::string& controlName,
                                       const BindingId& existingBindingId,
                                       juce::Point<int> screenPos, bool asScoreStep) {
    auto actions = state.allActions();
    auto tracks = state.listTracks();

    juce::PopupMenu menu;
    int baseId = 1;

    for (int ai = 0; ai < (int)actions.size(); ++ai) {
        if (actions[ai].name == "morph") continue;
        bool isTrackParam = !actions[ai].params.empty()
            && actions[ai].params[0].type == ParamType::ChannelRef;
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
                if (ti < (int)tracks.size()) firstArg = juce::String(tracks[ti].id.str());
            }

            auto* song = state.currentSong();
            if (!song) return;
            auto songId = song->id;

            juce::var initialArgs;
            if (firstArg.isNotEmpty()) initialArgs.append(firstArg);
            ActionInstanceForm::launch(state, actions[ai], initialArgs,
                [this, songId, capCtrlType, channel, number, controlName,
                 capBindId, capDevId, asScoreStep, ai, actions](const juce::var& formArgs) {
                    if (formArgs.isVoid()) return;
                    auto argsJson = juce::JSON::toString(formArgs, true).toStdString();
                    if (!capBindId.empty()) state.removeBinding(capBindId);
                    auto bindingId = state.addBinding(songId, capCtrlType, channel, number,
                                                       actions[ai].id, argsJson, controlName, capDevId);
                    if (asScoreStep) {
                        int nextPos = (int)scoreRows.size() + 1;
                        state.setBindingAsScoreStep(bindingId, nextPos);
                    }
                    refresh();
                });
        });
}

void SongMappingsPane::showControlPicker(bool forScore, juce::Point<int> screenPos) {
    juce::PopupMenu menu;
    int itemId = 1;

    // Build a flat list of all unbound controls across devices.
    auto* song = state.currentSong();
    std::set<std::tuple<DeviceId, std::string, int, int>> bound;
    if (song) {
        for (auto& b : song->bindings)
            bound.insert({ b.deviceId, b.controlType, b.channel, b.number });
    }

    struct PickEntry {
        DeviceId deviceId;
        std::string controlType;
        int channel;
        int number;
        std::string controlName;
    };
    std::vector<PickEntry> entries;

    auto& devices = state.allDevices();
    for (auto& dev : devices) {
        bool addedHeader = false;
        std::vector<std::pair<std::string, int>> devCtrlIdxs;  // name, index in dev.controls
        for (int ci = 0; ci < (int)dev.controls.size(); ++ci)
            devCtrlIdxs.push_back({ dev.controls[ci].name, ci });
        std::sort(devCtrlIdxs.begin(), devCtrlIdxs.end(),
            [&dev](const std::pair<std::string,int>& a, const std::pair<std::string,int>& b) {
                auto& ca = dev.controls[a.second];
                auto& cb = dev.controls[b.second];
                if (ca.group != cb.group) return ca.group < cb.group;
                return ca.name < cb.name;
            });

        for (auto& p : devCtrlIdxs) {
            auto& ctrl = dev.controls[p.second];
            if (bound.count({ dev.id, ctrl.controlType, ctrl.channel, ctrl.number })) continue;
            if (!addedHeader) {
                if (itemId > 1) menu.addSeparator();
                menu.addSectionHeader(juce::String(dev.name));
                addedHeader = true;
            }
            menu.addItem(itemId, juce::String(ctrl.name));
            PickEntry e;
            e.deviceId = dev.id;
            e.controlType = ctrl.controlType;
            e.channel = ctrl.channel;
            e.number = ctrl.number;
            e.controlName = ctrl.name;
            entries.push_back(e);
            itemId++;
        }
    }

    if (itemId == 1) {
        menu.addItem(-1, "No unmapped controls", false);
    }

    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetScreenArea(
            juce::Rectangle<int>(screenPos.x, screenPos.y, 1, 1)),
        [this, forScore, entries](int result) {
            if (result <= 0) return;
            int idx = result - 1;
            if (idx < 0 || idx >= (int)entries.size()) return;
            auto& e = entries[idx];
            auto* song = state.currentSong();
            if (!song) return;
            auto bindingId = state.addBinding(song->id, e.controlType, e.channel, e.number,
                                               ActionId{}, "[]", e.controlName, e.deviceId);
            if (forScore) {
                int nextPos = (int)scoreRows.size() + 1;
                state.setBindingAsScoreStep(bindingId, nextPos);
            }
            refresh();
        });
}

void SongMappingsPane::handleMidiActivity(const std::string& type, int channel, int number,
                                          const std::string& deviceId) {
    auto now = juce::Time::currentTimeMillis();
    DeviceId devId{deviceId};
    for (auto& mr : mappingRows)
        if (mr.deviceId == devId && mr.controlType == type
            && mr.channel == channel && mr.number == number)
            mr.lastActivityMs = now;
    for (auto& sr : scoreRows)
        if (sr.deviceId == devId && sr.controlType == type
            && sr.channel == channel && sr.number == number)
            sr.lastActivityMs = now;
}

void SongMappingsPane::timerCallback() {
    repaint();
}
