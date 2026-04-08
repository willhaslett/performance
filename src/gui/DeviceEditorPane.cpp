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

    setWantsKeyboardFocus(true);
}

DeviceEditorPane::~DeviceEditorPane() {
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

    if (!deviceId.empty()) {
        currentDeviceId = deviceId;
    } else if (!portName.empty()) {
        // Auto-register the device using portName as both name and port
        currentDeviceId = state.registerDevice(portName, portName);
        perfLog("[DeviceEditor] Auto-registered device '%s' -> %s\n",
                portName.c_str(), currentDeviceId.c_str());
    } else {
        currentDeviceId.clear();
        deviceNameLabel.setVisible(false);
        portNameLabel.setVisible(false);
        learnButton.setVisible(false);
        statusLabel.setVisible(false);
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

    // Show header and controls
    deviceNameLabel.setVisible(true);
    portNameLabel.setVisible(true);
    learnButton.setVisible(true);
    statusLabel.setVisible(true);

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

    // Add the control with empty name (user will rename via inline editor)
    state.addDeviceControl(currentDeviceId, "", type, channel, number);
    refreshControls();

    // Open inline editor on the new row's name field
    int newRowIndex = (int)controls.size() - 1;
    if (newRowIndex >= 0) {
        auto nameBounds = getNameCellBounds(newRowIndex);
        inlineEditor.onCommit = [this, newRowIndex](const juce::String& newText) {
            state.renameDeviceControl(currentDeviceId, newRowIndex, newText.toStdString());
            refreshControls();
        };
        inlineEditor.onCancel = nullptr;
        inlineEditor.show(*this, nameBounds, "");
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
    return juce::Rectangle<int>(row.getX() + 12, row.getY(), 160, rowHeight);
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
    g.drawText("Name",  12,  colY, 160, columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("Type",  180, colY, 80,  columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("Ch",    268, colY, 40,  columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("#",     316, colY, 40,  columnHeaderHeight, juce::Justification::centredLeft);
    g.drawText("Group", 364, colY, 100, columnHeaderHeight, juce::Justification::centredLeft);

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
            g.setColour(Theme::color(Theme::Color::textPrimary));
            g.setFont(Theme::font(Theme::fontSizeSm));

            auto name = ctrl.name.empty() ? "(unnamed)" : ctrl.name;
            g.drawText(juce::String(name), 12, rowBounds.getY(), 160, rowHeight,
                       juce::Justification::centredLeft);
            g.drawText(juce::String(ctrl.controlType), 180, rowBounds.getY(), 80, rowHeight,
                       juce::Justification::centredLeft);
            g.drawText(juce::String(ctrl.channel), 268, rowBounds.getY(), 40, rowHeight,
                       juce::Justification::centredLeft);
            g.drawText(juce::String(ctrl.number), 316, rowBounds.getY(), 40, rowHeight,
                       juce::Justification::centredLeft);

            g.setColour(Theme::color(Theme::Color::textSecondary));
            g.drawText(juce::String(ctrl.group), 364, rowBounds.getY(), 100, rowHeight,
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

    // Header labels
    deviceNameLabel.setBounds(12, 8, getWidth() - 24, 24);
    portNameLabel.setBounds(12, 32, getWidth() - 24, 18);

    // Bottom bar
    int bottomY = getHeight() - bottomBarHeight;
    learnButton.setBounds(12, bottomY + 6, 80, 28);
    statusLabel.setBounds(100, bottomY + 6, getWidth() - 112, 28);
}

void DeviceEditorPane::mouseUp(const juce::MouseEvent& event) {
    if (currentDeviceId.empty()) return;

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
    if (key == juce::KeyPress::escapeKey && isLearning) {
        cancelLearn();
        return true;
    }
    return false;
}
