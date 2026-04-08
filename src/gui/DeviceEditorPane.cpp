#include "gui/DeviceEditorPane.h"
#include "api/StateAPI.h"
#include "api/PerformanceCoordinator.h"
#include "state/StateEvents.h"
#include "engine/Log.h"

DeviceEditorPane::DeviceEditorPane(StateAPI& state, PerformanceCoordinator& coordinator)
    : state(state), coordinator(coordinator) {

    deviceNameLabel.setFont(Theme::font(18.0f));
    deviceNameLabel.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textWhite));
    deviceNameLabel.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(deviceNameLabel);

    portNameLabel.setFont(Theme::font(Theme::fontSizeXs));
    portNameLabel.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textDim));
    portNameLabel.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(portNameLabel);

    learnButton.setButtonText("Learn");
    learnButton.setColour(juce::TextButton::buttonColourId, Theme::color(Theme::Color::accent));
    learnButton.setColour(juce::TextButton::textColourOnId, Theme::color(Theme::Color::textWhite));
    learnButton.setColour(juce::TextButton::textColourOffId, Theme::color(Theme::Color::textWhite));
    learnButton.onClick = [this]() {
        if (isLearning)
            cancelLearn();
        else
            startLearn();
    };
    addChildComponent(learnButton);

    statusLabel.setFont(Theme::font(Theme::fontSizeSm));
    statusLabel.setColour(juce::Label::textColourId, Theme::color(Theme::Color::textSecondary));
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(statusLabel);

    midiEventLabel.setFont(Theme::font(Theme::fontSizeXs));
    midiEventLabel.setColour(juce::Label::textColourId, Theme::color(Theme::Color::midiActive));
    midiEventLabel.setJustificationType(juce::Justification::centred);
    midiEventLabel.setText("Incoming MIDI: \xe2\x80\x94", juce::dontSendNotification);
    addChildComponent(midiEventLabel);

    setWantsKeyboardFocus(true);
}

DeviceEditorPane::~DeviceEditorPane() {
    stopTimer();
    coordinator.clearMidiDeviceMonitor();
    if (stateSubscriptionId >= 0)
        state.events().unsubscribe(stateSubscriptionId);
}

void DeviceEditorPane::setDevice(const std::string& deviceId, const std::string& portName) {
    // Unsubscribe from previous
    if (stateSubscriptionId >= 0) {
        state.events().unsubscribe(stateSubscriptionId);
        stateSubscriptionId = -1;
    }

    // Cancel any active learn
    if (isLearning) cancelLearn();

    // Clear monitor for previous device
    coordinator.clearMidiDeviceMonitor();
    lastEvent1.clear();
    lastEvent2.clear();
    midiEventLabel.setText("Incoming MIDI: \xe2\x80\x94", juce::dontSendNotification);

    if (!deviceId.empty()) {
        currentDeviceId = deviceId;
    } else if (!portName.empty()) {
        // Auto-register the device using portName as both name and port
        currentDeviceId = state.registerDevice(portName, portName);
        coordinator.refreshMidiDevices();
        perfLog("[DeviceEditor] Auto-registered device '%s' -> %s\n",
                portName.c_str(), currentDeviceId.c_str());
    } else {
        currentDeviceId.clear();
        stopTimer();
        deviceNameLabel.setVisible(false);
        portNameLabel.setVisible(false);
        learnButton.setVisible(false);
        statusLabel.setVisible(false);
        midiEventLabel.setVisible(false);
        controls.clear();
        repaint();
        return;
    }

    // Subscribe to device events
    stateSubscriptionId = state.events().subscribe([this](const StateEvent& event) {
        if (event.entity == StateEvent::Device && event.entityId == currentDeviceId) {
            juce::MessageManager::callAsync([this] {
                refreshControls();
            });
        }
    });

    refreshControls();

    // Set up MIDI monitor for this device
    // The MIDIEngine callback is already dispatched to the message thread
    coordinator.setMidiDeviceMonitor(currentDeviceId,
        [this](const std::string& desc, const std::string& type, int ch, int num) {
            onMidiEvent(desc, type, ch, num);
        });

    startTimer(100); // 10Hz for activity light decay

    // Show header and controls
    deviceNameLabel.setVisible(true);
    portNameLabel.setVisible(true);
    learnButton.setVisible(true);
    statusLabel.setVisible(true);
    midiEventLabel.setVisible(true);

    auto* device = state.findDevice(currentDeviceId);
    if (device) {
        deviceNameLabel.setText(juce::String(device->name), juce::dontSendNotification);
        portNameLabel.setText(juce::String(device->midiPortName), juce::dontSendNotification);
    }

    resized();
    repaint();
}

void DeviceEditorPane::refreshControls() {
    controls.clear();
    auto* device = state.findDevice(currentDeviceId);
    if (!device) return;

    deviceNameLabel.setText(juce::String(device->name), juce::dontSendNotification);
    portNameLabel.setText(juce::String(device->midiPortName), juce::dontSendNotification);

    for (auto& ctrl : device->controls) {
        ControlRow row;
        row.name = ctrl.name;
        row.controlType = ctrl.controlType;
        row.channel = ctrl.channel;
        row.number = ctrl.number;
        row.group = ctrl.group;
        controls.push_back(row);
    }

    repaint();
}

void DeviceEditorPane::onMidiEvent(const std::string& description,
                                    const std::string& type, int channel, int number) {
    lastEvent2 = lastEvent1;
    lastEvent1 = description;
    std::string display = "Incoming MIDI: " + lastEvent1;
    if (!lastEvent2.empty())
        display += " | " + lastEvent2;
    midiEventLabel.setText(juce::String(display), juce::dontSendNotification);

    // Match against controls for activity light
    auto now = juce::Time::currentTimeMillis();
    for (auto& ctrl : controls) {
        if (ctrl.controlType == type && ctrl.channel == channel && ctrl.number == number) {
            ctrl.lastActivityMs = now;
            repaint();
            break;
        }
    }
}

void DeviceEditorPane::startLearn() {
    if (currentDeviceId.empty()) return;

    isLearning = true;
    learnButton.setButtonText("Cancel");

    auto* device = state.findDevice(currentDeviceId);
    std::string deviceName = device ? device->name : "device";
    statusLabel.setText("Move a control on " + juce::String(deviceName) + "...",
                        juce::dontSendNotification);
    repaint();

    coordinator.startMidiLearn(currentDeviceId,
        [this](const std::string& type, int channel, int number) {
            juce::MessageManager::callAsync([this, type, channel, number] {
                onLearnCapture(type, channel, number);
            });
        });
}

void DeviceEditorPane::cancelLearn() {
    if (!isLearning) return;
    isLearning = false;
    coordinator.cancelMidiLearn();
    learnButton.setButtonText("Learn");
    statusLabel.setText("", juce::dontSendNotification);
    repaint();
}

void DeviceEditorPane::onLearnCapture(const std::string& type, int channel, int number) {
    isLearning = false;
    learnButton.setButtonText("Learn");
    statusLabel.setText("", juce::dontSendNotification);

    // Check if this control (same type+channel+number) already exists
    int existingIndex = -1;
    for (int i = 0; i < (int)controls.size(); ++i) {
        if (controls[i].controlType == type &&
            controls[i].channel == channel &&
            controls[i].number == number) {
            existingIndex = i;
            break;
        }
    }

    int editRowIndex;
    std::string editInitialText;

    if (existingIndex >= 0) {
        // Select existing row and open rename
        editRowIndex = existingIndex;
        editInitialText = controls[existingIndex].name;
        pendingLearnControlIndex = -1;
    } else {
        // Add new control with default name
        std::string defaultName = "Control " + std::to_string(controls.size() + 1);
        state.addDeviceControl(currentDeviceId, defaultName, type, channel, number);
        refreshControls();
        editRowIndex = (int)controls.size() - 1;
        editInitialText = defaultName;
        pendingLearnControlIndex = editRowIndex;
    }

    // Open inline editor on the row's name field
    if (editRowIndex >= 0) {
        auto nameBounds = getNameCellBounds(editRowIndex);
        inlineEditor.onCommit = [this, editRowIndex](const juce::String& newText) {
            pendingLearnControlIndex = -1;
            state.renameDeviceControl(currentDeviceId, editRowIndex, newText.toStdString());
            refreshControls();
        };
        inlineEditor.onCancel = [this]() {
            if (pendingLearnControlIndex >= 0) {
                state.removeDeviceControl(currentDeviceId, pendingLearnControlIndex);
                pendingLearnControlIndex = -1;
                refreshControls();
            }
        };
        inlineEditor.show(*this, nameBounds, juce::String(editInitialText));
    }
}

juce::Rectangle<int> DeviceEditorPane::getControlListArea() const {
    return juce::Rectangle<int>(0, headerHeight + columnHeaderHeight,
                                 getWidth(), getHeight() - headerHeight - columnHeaderHeight - bottomBarHeight);
}

juce::Rectangle<int> DeviceEditorPane::getRowBounds(int rowIndex) const {
    int y = headerHeight + columnHeaderHeight + rowIndex * rowHeight;
    return juce::Rectangle<int>(0, y, getWidth(), rowHeight);
}

juce::Rectangle<int> DeviceEditorPane::getNameCellBounds(int rowIndex) const {
    auto row = getRowBounds(rowIndex);
    return juce::Rectangle<int>(row.getX() + 24, row.getY(), 148, rowHeight);
}

juce::Rectangle<int> DeviceEditorPane::getDeleteButtonBounds(int rowIndex) const {
    auto row = getRowBounds(rowIndex);
    return juce::Rectangle<int>(row.getRight() - 28, row.getY() + 2, 20, rowHeight - 4);
}

void DeviceEditorPane::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    if (currentDeviceId.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSize));
        g.drawText("Select a device from the sidebar",
                   getLocalBounds(), juce::Justification::centred);
        return;
    }

    // Header background
    auto headerArea = getLocalBounds().removeFromTop(headerHeight);
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(headerArea);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)headerHeight, (float)getWidth(), (float)headerHeight, 1.0f);

    // Column headers
    int colY = headerHeight;
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRect(0, colY, getWidth(), columnHeaderHeight);
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(Theme::fontSizeXs));
    g.drawText("Name",  24,  colY, 148, columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("Type",  180, colY, 80,  columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("Ch",    268, colY, 40,  columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("#",     316, colY, 40,  columnHeaderHeight, juce::Justification::centredLeft);

    // Control rows
    if (controls.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSize));
        auto listArea = getControlListArea();
        g.drawText("No controls mapped. Click Learn to add.",
                   listArea, juce::Justification::centred);
    } else {
        for (int i = 0; i < (int)controls.size(); ++i) {
            auto rowBounds = getRowBounds(i);
            if (rowBounds.getBottom() < headerHeight + columnHeaderHeight) continue;
            if (rowBounds.getY() > getHeight() - bottomBarHeight) break;

            // Alternating row background
            if (i % 2 == 1) {
                g.setColour(Theme::color(Theme::Color::bgPanel));
                g.fillRect(rowBounds);
            }

            // Hover highlight
            if (i == hoveredRow) {
                g.setColour(Theme::color(Theme::Color::bgSlotHover));
                g.fillRect(rowBounds);
            }

            auto& ctrl = controls[i];

            // Activity light
            auto now = juce::Time::currentTimeMillis();
            bool active = (ctrl.lastActivityMs > 0 && now - ctrl.lastActivityMs < 300);
            float lightX = 8.0f;
            float lightY = rowBounds.getY() + (rowHeight - 6) / 2.0f;
            g.setColour(active ? juce::Colour(0xff44cc44) : juce::Colour(0xff1a3a1a));
            g.fillEllipse(lightX, lightY, 6.0f, 6.0f);

            g.setColour(Theme::color(Theme::Color::textPrimary));
            g.setFont(Theme::font(Theme::fontSizeSm));

            auto name = ctrl.name.empty() ? "(unnamed)" : ctrl.name;
            g.drawText(juce::String(name), 24, rowBounds.getY(), 148, rowHeight,
                       juce::Justification::centredLeft);
            g.drawText(juce::String(ctrl.controlType), 180, rowBounds.getY(), 80, rowHeight,
                       juce::Justification::centredLeft);
            g.drawText(juce::String(ctrl.channel), 268, rowBounds.getY(), 40, rowHeight,
                       juce::Justification::centredLeft);
            g.drawText(juce::String(ctrl.number), 316, rowBounds.getY(), 40, rowHeight,
                       juce::Justification::centredLeft);

            // Delete button (x) — only show on hover
            if (i == hoveredRow) {
                auto delBounds = getDeleteButtonBounds(i);
                g.setColour(Theme::color(Theme::Color::textDim));
                g.setFont(Theme::font(Theme::fontSizeSm));
                g.drawText("x", delBounds, juce::Justification::centred);
            }
        }
    }

    // Bottom bar background
    auto bottomArea = getLocalBounds().removeFromBottom(bottomBarHeight);
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(bottomArea);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)bottomArea.getY(), (float)getWidth(), (float)bottomArea.getY(), 1.0f);
}

void DeviceEditorPane::resized() {
    if (currentDeviceId.empty()) return;

    // Header row 1: device name (left) + learn button (right)
    int learnW = 70;
    int learnH = 24;
    learnButton.setBounds(getWidth() - learnW - 12, 10, learnW, learnH);
    deviceNameLabel.setBounds(12, 8, getWidth() - learnW - 36, 24);

    // Header row 2: port name (left), MIDI event log (centered across full width)
    portNameLabel.setBounds(12, 34, 200, 18);
    midiEventLabel.setBounds(0, 34, getWidth(), 18);

    // Status label (learn feedback) in bottom bar
    statusLabel.setBounds(12, getHeight() - bottomBarHeight + 6, getWidth() - 24, 28);
}

void DeviceEditorPane::mouseUp(const juce::MouseEvent& event) {
    if (currentDeviceId.empty()) return;

    // Right-click context menu on control rows
    if (event.mods.isPopupMenu() && !isLearning) {
        for (int i = 0; i < (int)controls.size(); ++i) {
            auto rowBounds = getRowBounds(i);
            if (rowBounds.contains(event.getPosition())) {
                juce::PopupMenu menu;
                menu.addItem(1, "Delete");
                menu.showMenuAsync(juce::PopupMenu::Options(),
                    [this, i](int result) {
                        if (result == 1) {
                            state.removeDeviceControl(currentDeviceId, i);
                            refreshControls();
                        }
                    });
                return;
            }
        }
    }

    // Check delete button clicks
    for (int i = 0; i < (int)controls.size(); ++i) {
        auto delBounds = getDeleteButtonBounds(i);
        if (delBounds.contains(event.getPosition())) {
            state.removeDeviceControl(currentDeviceId, i);
            refreshControls();
            return;
        }
    }
}

void DeviceEditorPane::mouseMove(const juce::MouseEvent& event) {
    int newHovered = -1;
    for (int i = 0; i < (int)controls.size(); ++i) {
        auto rowBounds = getRowBounds(i);
        if (rowBounds.contains(event.getPosition())) {
            newHovered = i;
            break;
        }
    }
    if (newHovered != hoveredRow) {
        hoveredRow = newHovered;
        repaint();
    }
}

void DeviceEditorPane::mouseDoubleClick(const juce::MouseEvent& event) {
    if (currentDeviceId.empty()) return;

    // Check if double-click is on a name cell
    for (int i = 0; i < (int)controls.size(); ++i) {
        auto nameBounds = getNameCellBounds(i);
        if (nameBounds.contains(event.getPosition())) {
            inlineEditor.onCommit = [this, i](const juce::String& newText) {
                state.renameDeviceControl(currentDeviceId, i, newText.toStdString());
                refreshControls();
            };
            inlineEditor.onCancel = nullptr;
            inlineEditor.show(*this, nameBounds, juce::String(controls[i].name));
            return;
        }
    }
}

bool DeviceEditorPane::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress::escapeKey) {
        if (pendingLearnControlIndex >= 0) {
            // Cancel the pending learn entry
            state.removeDeviceControl(currentDeviceId, pendingLearnControlIndex);
            pendingLearnControlIndex = -1;
            // Dismiss the inline editor if visible
            if (inlineEditor.getParentComponent())
                inlineEditor.getParentComponent()->removeChildComponent(&inlineEditor);
            refreshControls();
            return true;
        }
        if (isLearning) {
            cancelLearn();
            return true;
        }
    }
    return false;
}

void DeviceEditorPane::timerCallback() {
    auto now = juce::Time::currentTimeMillis();
    bool needsRepaint = false;
    for (auto& ctrl : controls) {
        // Check if any light just decayed (was active within last 400ms but not 300ms)
        if (ctrl.lastActivityMs > 0 && now - ctrl.lastActivityMs >= 300 && now - ctrl.lastActivityMs < 400) {
            needsRepaint = true;
        }
    }
    if (needsRepaint)
        repaint();
}
