#include "gui/SendsPanel.h"
#include "api/PerformanceAPI.h"

SendsPanel::SendsPanel(const juce::String& trackName, PerformanceAPI& api)
    : api(api), trackName(trackName) {}

void SendsPanel::setSends(const std::vector<SendInfo>& sends) {
    bool changed = (sends.size() != columns.size());
    if (!changed) {
        for (size_t i = 0; i < sends.size(); ++i) {
            if (i < columns.size() && columns[i].busName != sends[i].busName) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        // Rebuild columns
        for (auto& col : columns)
            if (col.faderMeter) removeChildComponent(col.faderMeter.get());
        columns.clear();

        for (auto& send : sends) {
            SendColumn col;
            col.busName = send.busName;
            col.faderMeter = std::make_unique<FaderMeter>();
            col.faderMeter->onGainChanged = [this, busName = send.busName](float newGain) {
                api.setSendGain(trackName, busName, newGain);
            };
            addAndMakeVisible(*col.faderMeter);
            columns.push_back(std::move(col));
        }

        // Add empty column for new send
        {
            SendColumn col;
            col.faderMeter = std::make_unique<FaderMeter>();
            addAndMakeVisible(*col.faderMeter);
            columns.push_back(std::move(col));
        }

        resized();
    }

    // Update values
    for (size_t i = 0; i < sends.size() && i < columns.size(); ++i) {
        columns[i].faderMeter->setGain(sends[i].gain);
        columns[i].faderMeter->setPeakLevel(sends[i].peakLevel);
    }

    repaint();
}

void SendsPanel::setAvailableBusses(const std::vector<juce::String>& busNames) {
    bool wasEmpty = availableBusses.empty();
    availableBusses = busNames;
    // If busses just became available and we have no columns, rebuild to show empty "Send" pill
    if (wasEmpty && !busNames.empty() && columns.empty()) {
        std::vector<SendInfo> empty;
        setSends(empty);
    }
}

void SendsPanel::paint(juce::Graphics& g) {
    // Header pills for each send
    headerBounds.clear();
    g.setFont(Theme::font(Theme::fontSizeXs));

    int totalWidth = (int)columns.size() * sendColumnWidth + ((int)columns.size() - 1) * sendPadding;
    int startX = (getWidth() - totalWidth) / 2;

    for (size_t i = 0; i < columns.size(); ++i) {
        int x = startX + (int)i * (sendColumnWidth + sendPadding);
        auto headerArea = juce::Rectangle<int>(x, sendPadding, sendColumnWidth, sendHeaderHeight);
        headerBounds.push_back(headerArea);

        g.setColour(Theme::color(Theme::Color::bgSlot));
        g.fillRoundedRectangle(headerArea.toFloat(), Theme::cornerRadiusSm);

        if (columns[i].busName.isNotEmpty()) {
            g.setColour(Theme::color(Theme::Color::effect));
            g.drawText(columns[i].busName, headerArea.reduced(2, 0),
                       juce::Justification::centred, true);
        } else {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.drawText("Send", headerArea.reduced(2, 0),
                       juce::Justification::centred, true);
        }
    }
}

void SendsPanel::resized() {
    int totalWidth = (int)columns.size() * sendColumnWidth + ((int)columns.size() - 1) * sendPadding;
    int startX = (getWidth() - totalWidth) / 2;

    for (size_t i = 0; i < columns.size(); ++i) {
        int x = startX + (int)i * (sendColumnWidth + sendPadding);
        int fmTop = sendPadding + sendHeaderHeight + sendPadding;
        auto fmArea = juce::Rectangle<int>(x + 8, fmTop, sendColumnWidth - 16,
                                            getHeight() - fmTop - sendPadding);
        if (columns[i].faderMeter)
            columns[i].faderMeter->setBounds(fmArea);
    }
}

void SendsPanel::mouseUp(const juce::MouseEvent& event) {
    for (size_t i = 0; i < headerBounds.size(); ++i) {
        if (headerBounds[i].contains(event.getPosition())) {
            if (columns[i].busName.isEmpty())
                showBusPicker((int)i, event.getScreenPosition());
            break;
        }
    }
}

void SendsPanel::showBusPicker(int columnIndex, juce::Point<int> position) {
    juce::PopupMenu menu;
    for (int i = 0; i < (int)availableBusses.size(); ++i)
        menu.addItem(i + 1, availableBusses[i]);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(
        juce::Rectangle<int>(position.x, position.y, 1, 1)),
        [this, columnIndex](int result) {
            if (result == 0 || result - 1 >= (int)availableBusses.size()) return;
            auto busName = availableBusses[result - 1];
            api.addSend(trackName, busName, 1.0f);
        });
}
