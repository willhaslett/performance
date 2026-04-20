#include "gui/ControllersPane.h"
#include "gui/UiTerms.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"
#include <set>
#include <map>

ControllersPane::ControllersPane(StateAPI& state, EngineAPI& engine, PerformanceCoordinator& coordinator)
    : state(state), engine(engine), coordinator(coordinator) {

    stateSubscriptionId = state.events().subscribe([this](const StateEvent&) {
        juce::MessageManager::callAsync([this] { refresh(); });
    });

    startTimerHz(10);
    buildRows();
}

ControllersPane::~ControllersPane() {
    if (stateSubscriptionId >= 0)
        state.events().unsubscribe(stateSubscriptionId);
    if (isLearning) cancelLearn();
}

void ControllersPane::buildRows() {
    leftEntries.clear();

    auto* song = state.currentSong();

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
    if (song) {
        for (auto& b : song->bindings)
            boundControls[{ b.deviceId, b.controlType, b.channel, b.number }] = &b;
    }

    auto midiDevices = juce::MidiInput::getAvailableDevices();
    std::set<std::string> connectedPorts;
    for (auto& d : midiDevices) connectedPorts.insert(d.name.toStdString());

    auto& devices = state.allDevices();
    for (int di = 0; di < (int)devices.size(); ++di) {
        auto& dev = devices[di];
        bool connected = connectedPorts.count(dev.midiPortName) > 0;
        bool collapsed = collapsedDevices.count(dev.id.str()) > 0;

        LeftPanelEntry header;
        header.type = LeftPanelEntry::DeviceHeader;
        header.deviceId = dev.id.str();
        header.deviceName = dev.name;
        header.deviceConnected = connected;
        leftEntries.push_back(header);

        if (!collapsed) {
            std::vector<LeftPanelEntry> deviceControls;
            for (int ci = 0; ci < (int)dev.controls.size(); ++ci) {
                auto& ctrl = dev.controls[ci];
                bool bound = boundControls.find({ dev.id, ctrl.controlType, ctrl.channel, ctrl.number }) != boundControls.end();

                LeftPanelEntry entry;
                entry.type = LeftPanelEntry::Control;
                entry.deviceId = dev.id.str();
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
    }
}

void ControllersPane::refresh() { buildRows(); resized(); repaint(); }

bool ControllersPane::isDeviceConnected(const std::string& deviceId) const {
    auto* dev = state.findDevice(DeviceId{deviceId});
    if (!dev) return false;
    auto midiDevices = juce::MidiInput::getAvailableDevices();
    for (auto& d : midiDevices)
        if (d.name.toStdString() == dev->midiPortName) return true;
    return false;
}

void ControllersPane::resized() {
    // Single-panel layout — nothing to partition.
}

void ControllersPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgPanel));

    auto area = getLocalBounds();

    // Title
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeTitle));
    g.drawText("Controllers", area.getX() + panelPadding, area.getY() + 10,
               160, 22, juce::Justification::centredLeft);

    // Learn button
    int learnW = isLearning ? 42 : 126;
    auto learnBounds = juce::Rectangle<int>(area.getRight() - learnW - 8, area.getY() + 10, learnW, 22);
    g.setColour(isLearning ? Theme::color(Theme::Color::accent) : Theme::color(Theme::Color::textSecondary));
    g.drawRoundedRectangle(learnBounds.toFloat(), Theme::cornerRadiusXs, 1.0f);
    g.setFont(Theme::font(Theme::fontSizeMd));
    g.drawText(isLearning ? "Stop" : "Learn new controls", learnBounds, juce::Justification::centred);

    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getX(), (float)(area.getY() + leftHeaderHeight),
               (float)area.getRight(), (float)(area.getY() + leftHeaderHeight), 1.0f);

    int inSongHeaderY = area.getY() + leftHeaderHeight + 2;
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeSm));
    g.drawText("In " + UiTerms::docSingular, area.getRight() - 58, inSongHeaderY, 50, 16, juce::Justification::centredRight);

    int contentTop = area.getY() + leftHeaderHeight;

    g.setColour(Theme::color(Theme::Color::bgSurface));
    g.fillRect(area.getX(), contentTop, area.getWidth(), area.getBottom() - contentTop);

    int y = contentTop - leftScrollOffset;
    for (int i = 0; i < (int)leftEntries.size(); ++i) {
        if (y > area.getBottom()) break;
        auto& entry = leftEntries[i];

        if (entry.type == LeftPanelEntry::DeviceHeader) {
            if (y + rowHeight >= contentTop) {
                bool collapsed = collapsedDevices.count(entry.deviceId) > 0;

                g.setColour(Theme::color(Theme::Color::textSecondary));
                g.setFont(Theme::font(Theme::fontSizeSm));
                g.drawText(collapsed ? juce::CharPointer_UTF8("\xe2\x96\xb6")
                                     : juce::CharPointer_UTF8("\xe2\x96\xbc"),
                           area.getX() + 6, y, 12, rowHeight, juce::Justification::centred);

                float dotY = (float)(y + rowHeight / 2 - 3);
                g.setColour(entry.deviceConnected ? Theme::color(Theme::Color::activityOn)
                                                  : Theme::color(Theme::Color::textDim));
                g.fillEllipse((float)(area.getX() + 20), dotY, Theme::activityDotSize, Theme::activityDotSize);

                g.setColour(Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(Theme::fontSizeLg));
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
                    g.setColour(Theme::color(Theme::Color::bgControlHover));
                    g.fillRect(area.getX(), y, area.getWidth(), rowHeight);
                }

                bool active = (entry.lastActivityMs > 0 && now - entry.lastActivityMs < 300);
                g.setColour(active ? Theme::color(Theme::Color::activityOn)
                                   : Theme::color(Theme::Color::activityOff));
                g.fillEllipse((float)(area.getX() + 32), (float)(y + rowHeight / 2 - 3), Theme::activityDotSize, Theme::activityDotSize);

                int checkX = area.getRight() - 40;
                int groupFieldW = 60;
                int groupFieldX = checkX - groupFieldW;
                int nameX = area.getX() + 42;

                g.setColour(entry.isBound ? Theme::color(Theme::Color::textDim)
                                          : Theme::color(Theme::Color::textPrimary));
                g.setFont(Theme::font(Theme::fontSizeLg));
                g.drawText(juce::String(entry.controlName), nameX, y,
                           groupFieldX - nameX - 4, rowHeight, juce::Justification::centredLeft);

                if (!entry.group.empty()) {
                    g.setColour(Theme::color(Theme::Color::textSecondary));
                    g.setFont(Theme::font(Theme::fontSizeLg));
                    g.drawText(juce::String(entry.group), groupFieldX, y,
                               groupFieldW, rowHeight, juce::Justification::centredRight);
                } else {
                    g.setColour(Theme::color(Theme::Color::textDim));
                    g.setFont(Theme::font(Theme::fontSizeLg));
                    g.drawText("group", groupFieldX, y,
                               groupFieldW, rowHeight, juce::Justification::centredRight);
                }

                if (entry.isBound) {
                    g.setColour(Theme::color(Theme::Color::accent));
                    g.setFont(Theme::font(Theme::fontSizeLg));
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
    g.setFont(Theme::font(Theme::fontSizeTitle));
    g.drawText("Controllers", area.getX() + panelPadding, area.getY() + 10,
               160, 22, juce::Justification::centredLeft);
    g.setColour(isLearning ? Theme::color(Theme::Color::accent) : Theme::color(Theme::Color::textSecondary));
    g.drawRoundedRectangle(learnBounds.toFloat(), Theme::cornerRadiusXs, 1.0f);
    g.setFont(Theme::font(Theme::fontSizeMd));
    g.drawText(isLearning ? "Stop" : "Learn new controls", learnBounds, juce::Justification::centred);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)area.getX(), (float)(area.getY() + leftHeaderHeight),
               (float)area.getRight(), (float)(area.getY() + leftHeaderHeight), 1.0f);
}

void ControllersPane::mouseDown(const juce::MouseEvent& event) {
    if (editingLeftRow >= 0 && !inlineEditor.getBounds().contains(event.getPosition())) {
        inlineEditor.commit();
        return;
    }

    auto pos = event.getPosition();
    if (event.mods.isPopupMenu()) return;

    resetPendingDrag();

    int y = leftHeaderHeight - leftScrollOffset;
    for (int i = 0; i < (int)leftEntries.size(); ++i) {
        if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
        if (pos.getY() >= y && pos.getY() < y + rowHeight && !leftEntries[i].isBound) {
            pendingDragLeftRow = i;
            pendingDragStart = pos;
            return;
        }
        y += rowHeight;
    }
}

void ControllersPane::resetPendingDrag() {
    pendingDragLeftRow = -1;
}

void ControllersPane::mouseDrag(const juce::MouseEvent& event) {
    if (dragInProgress) return;
    if (pendingDragLeftRow < 0) return;
    if (pendingDragStart.getDistanceFrom(event.getPosition()) <= 5) return;

    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (!container) {
        resetPendingDrag();
        return;
    }

    auto obj = std::make_unique<juce::DynamicObject>();
    juce::String dragLabel;
    if (pendingDragLeftRow < (int)leftEntries.size()) {
        auto& e = leftEntries[pendingDragLeftRow];
        obj->setProperty("kind", "control");
        obj->setProperty("deviceId", juce::String(e.deviceId));
        obj->setProperty("controlType", juce::String(e.controlType));
        obj->setProperty("channel", e.channel);
        obj->setProperty("number", e.number);
        obj->setProperty("controlName", juce::String(e.controlName));
        dragLabel = juce::String(e.controlName);
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
    // dragInProgress doesn't get cleared on this pane — it will be on the next mouseDown.
    dragInProgress = false;
}

void ControllersPane::mouseUp(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    resetPendingDrag();

    auto area = getLocalBounds();

    // Click device header → toggle collapse
    if (!event.mods.isPopupMenu()) {
        int y = leftHeaderHeight - leftScrollOffset;
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
    if (!event.mods.isPopupMenu()) {
        int checkX = area.getRight() - 40;
        int groupFieldW = 60;
        int groupFieldX = checkX - groupFieldW;

        int y = leftHeaderHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
            if (pos.getY() >= y && pos.getY() < y + rowHeight && pos.getX() >= groupFieldX && pos.getX() < checkX) {
                auto devId = leftEntries[i].deviceId;
                auto ctrlIdx = leftEntries[i].controlIndex;
                auto entryIdx = i;

                std::set<std::string> groups;
                auto* dev = state.findDevice(DeviceId{devId});
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
                            int ey = leftHeaderHeight - leftScrollOffset;
                            for (int j = 0; j < entryIdx; ++j) ey += rowHeight;
                            int checkXl = getLocalBounds().getRight() - 40;
                            int gfW = 60;
                            auto bounds = juce::Rectangle<int>(checkXl - gfW, ey, gfW, rowHeight);
                            inlineEditor.onCommit = [this, devId, ctrlIdx](const juce::String& text) {
                                state.setDeviceControlGroup(DeviceId{devId}, ctrlIdx, text.toStdString());
                                editingLeftRow = -1;
                                refresh();
                            };
                            inlineEditor.onCancel = [this] { editingLeftRow = -1; repaint(); };
                            inlineEditor.onCommitNext = nullptr;
                            inlineEditor.onCommitPrev = nullptr;
                            editingLeftRow = entryIdx;
                            inlineEditor.show(*this, bounds, "");
                        } else if (r == 101) {
                            state.setDeviceControlGroup(DeviceId{devId}, ctrlIdx, "");
                            refresh();
                        } else if (r >= 200 && r - 200 < (int)groupList.size()) {
                            state.setDeviceControlGroup(DeviceId{devId}, ctrlIdx, groupList[r - 200]);
                            refresh();
                        }
                    });
                return;
            }
            y += rowHeight;
        }
    }

    // Learn button
    if (!event.mods.isPopupMenu()) {
        int learnW = isLearning ? 42 : 126;
        auto learnBounds = juce::Rectangle<int>(area.getRight() - learnW - 8,
                                                 area.getY() + 10, learnW, 22);
        if (learnBounds.contains(pos)) {
            if (isLearning) cancelLearn(); else startLearn();
            repaint();
            return;
        }
    }

    // Right-click left panel control → set group / delete
    if (event.mods.isPopupMenu()) {
        int y = leftHeaderHeight - leftScrollOffset;
        for (int i = 0; i < (int)leftEntries.size(); ++i) {
            if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
            if (pos.getY() >= y && pos.getY() < y + rowHeight) {
                auto devId = leftEntries[i].deviceId;
                auto ctrlIdx = leftEntries[i].controlIndex;
                auto entryIdx = i;

                std::set<std::string> groups;
                auto* dev = state.findDevice(DeviceId{devId});
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
                            state.removeDeviceControl(DeviceId{devId}, ctrlIdx);
                            refresh();
                        } else if (r == 100) {
                            int ey = leftHeaderHeight - leftScrollOffset;
                            for (int j = 0; j < entryIdx; ++j) ey += rowHeight;
                            auto bounds = juce::Rectangle<int>(getLocalBounds().getX() + 30, ey,
                                                                getLocalBounds().getWidth() - 70, rowHeight);
                            inlineEditor.onCommit = [this, devId, ctrlIdx](const juce::String& text) {
                                state.setDeviceControlGroup(DeviceId{devId}, ctrlIdx, text.toStdString());
                                editingLeftRow = -1;
                                refresh();
                            };
                            inlineEditor.onCancel = [this] { editingLeftRow = -1; repaint(); };
                            inlineEditor.onCommitNext = nullptr;
                            inlineEditor.onCommitPrev = nullptr;
                            editingLeftRow = entryIdx;
                            inlineEditor.show(*this, bounds, "");
                        } else if (r == 101) {
                            state.setDeviceControlGroup(DeviceId{devId}, ctrlIdx, "");
                            refresh();
                        } else if (r >= 200 && r - 200 < (int)groupList.size()) {
                            state.setDeviceControlGroup(DeviceId{devId}, ctrlIdx, groupList[r - 200]);
                            refresh();
                        }
                    });
                return;
            }
            y += rowHeight;
        }
    }
}

void ControllersPane::mouseDoubleClick(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    int groupFieldX = getLocalBounds().getRight() - 40 - 60;

    int y = leftHeaderHeight - leftScrollOffset;
    for (int i = 0; i < (int)leftEntries.size(); ++i) {
        if (leftEntries[i].type != LeftPanelEntry::Control) { y += rowHeight; continue; }
        if (pos.getY() >= y && pos.getY() < y + rowHeight && pos.getX() < groupFieldX) {
            openLeftEditor(i);
            return;
        }
        y += rowHeight;
    }
}

void ControllersPane::openLeftEditor(int entryIndex) {
    if (entryIndex < 0 || entryIndex >= (int)leftEntries.size()) return;
    if (leftEntries[entryIndex].type != LeftPanelEntry::Control) return;

    editingLeftRow = entryIndex;
    auto& entry = leftEntries[entryIndex];

    int y = leftHeaderHeight - leftScrollOffset;
    for (int i = 0; i < entryIndex; ++i) y += rowHeight;

    auto bounds = juce::Rectangle<int>(getLocalBounds().getX() + 30, y,
                                        getLocalBounds().getWidth() - 70, rowHeight);

    auto commitRename = [this](int idx, const juce::String& newText) {
        if (idx >= 0 && idx < (int)leftEntries.size()) {
            auto& e = leftEntries[idx];
            state.renameDeviceControl(DeviceId{e.deviceId}, e.controlIndex, newText.toStdString());
        }
        editingLeftRow = -1;
        refresh();
    };

    inlineEditor.onCommit = [this, entryIndex, commitRename](const juce::String& newText) {
        commitRename(entryIndex, newText);
    };
    inlineEditor.onCancel = [this] { editingLeftRow = -1; repaint(); };

    auto deviceId = entry.deviceId;
    inlineEditor.onCommitNext = [this, entryIndex, deviceId, commitRename](const juce::String& newText) {
        commitRename(entryIndex, newText);
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

void ControllersPane::mouseMove(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    int newLeft = -1;

    int y = leftHeaderHeight - leftScrollOffset;
    for (int i = 0; i < (int)leftEntries.size(); ++i) {
        if (pos.getY() >= y && pos.getY() < y + rowHeight) { newLeft = i; break; }
        y += rowHeight;
    }

    if (newLeft != hoveredLeftRow) {
        hoveredLeftRow = newLeft;
        repaint();
    }
}

void ControllersPane::mouseWheelMove(const juce::MouseEvent&,
                                      const juce::MouseWheelDetails& wheel) {
    int delta = (int)(wheel.deltaY * 100);
    leftScrollOffset = std::max(0, leftScrollOffset - delta);
    repaint();
}

void ControllersPane::scrollToDevice(const std::string& deviceId) {
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

void ControllersPane::startLearn() {
    isLearning = true;
    armLearnCapture();
    repaint();
}

void ControllersPane::cancelLearn() {
    isLearning = false;
    coordinator.cancelMidiLearn();
    repaint();
}

void ControllersPane::armLearnCapture() {
    coordinator.startMidiLearn("",
        [this](const std::string& type, int ch, int num, const std::string& portName) {
            juce::MessageManager::callAsync([this, type, ch, num, portName] {
                onLearnCapture(type, ch, num, portName);
            });
        });
}

void ControllersPane::onLearnCapture(const std::string& type, int channel, int number,
                                      const std::string& portName) {
    if (!isLearning) return;

    DeviceId targetDeviceId;
    if (!portName.empty()) {
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
                auto now = juce::Time::currentTimeMillis();
                for (auto& entry : leftEntries)
                    if (entry.type == LeftPanelEntry::Control && entry.deviceId == targetDeviceId.str()
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
    if (onBindingsChanged) onBindingsChanged();

    if (isLearning) armLearnCapture();
}

void ControllersPane::onMidiEvent(const std::string& type, int channel, int number,
                                   const std::string& deviceId) {
    auto now = juce::Time::currentTimeMillis();
    for (auto& e : leftEntries)
        if (e.type == LeftPanelEntry::Control && e.deviceId == deviceId
            && e.controlType == type && e.channel == channel && e.number == number)
            e.lastActivityMs = now;
}

void ControllersPane::handleMidiActivity(const std::string& type, int channel, int number,
                                          const std::string& deviceId) {
    onMidiEvent(type, channel, number, deviceId);
}

void ControllersPane::timerCallback() {
    auto midiDevices = juce::MidiInput::getAvailableDevices();
    int count = (int)midiDevices.size();
    if (count != lastMidiDeviceCount) {
        lastMidiDeviceCount = count;
        buildRows();
    }
    repaint();
}
