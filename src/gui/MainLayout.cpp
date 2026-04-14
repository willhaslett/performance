#include "gui/MainLayout.h"
#include "gui/KeyBindings.h"
#include "gui/Theme.h"
#include <set>
#include "engine/Log.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/PerformanceCoordinator.h"

MainLayout::MainLayout(StateAPI& state, EngineAPI& engine, LuaEngine& lua,
                       PerformanceCoordinator& coordinator)
    : state(state), engine(engine),
      debugPane(coordinator, engine), mappingPane(state, engine, coordinator),
      chatView(lua), mixerView(state, engine) {
    sidebar.setStateAPI(&state);
    sidebar.setEngineAPI(&engine);
    sidebar.setCoordinator(&coordinator);

    producePane.setState(&state, coordinator.sequencer(), &coordinator.arrangement());
    producePane.onStartRecordMode = [&coordinator]() { coordinator.startRecordMode(); };
    producePane.onStopRecordMode = [&coordinator]() { coordinator.stopRecordMode(); };
    producePane.onIsRecordMode = [&coordinator]() { return coordinator.isInRecordMode(); };
    producePane.onRegionsChanged = [&coordinator]() { coordinator.reloadAudioFiles(); };

    // All components start hidden — setPaneContent will show the right ones
    sidebar.setVisible(false);
    producePane.setVisible(false);
    mappingPane.setVisible(false);
    debugPane.setVisible(false);
    chatView.setVisible(false);
    logPane.setVisible(false);
    mixerView.setVisible(false);

    addChildComponent(sidebar);
    addChildComponent(producePane);
    addChildComponent(mappingPane);
    addChildComponent(debugPane);
    addChildComponent(chatView);
    addChildComponent(logPane);
    addChildComponent(mixerView);

    // Musical Typing (hidden by default)
    musicalTyping.setAudioEngine(&engine.getAudioEngine());
    addChildComponent(musicalTyping);

    // Sidebar divider
    addAndMakeVisible(sidebarDivider);
    sidebarDivider.onDragStart = [this]() { dragStartSidebarWidth = sidebarWidth; };
    sidebarDivider.onDrag = [this](int delta) {
        sidebarWidth = std::max(minPaneSize, dragStartSidebarWidth + delta);
        resized();
    };

    // Default pane assignments
    paneAssignments[PaneSlot::Sidebar] = PaneContent::SidebarTree;
    paneAssignments[PaneSlot::Left] = PaneContent::Produce;
    paneAssignments[PaneSlot::Right] = PaneContent::Chat;
    paneAssignments[PaneSlot::Bottom] = PaneContent::Mixer;

    loadPaneConfig();

    // Force-apply all assignments (make components visible)
    for (auto& [slot, content] : paneAssignments) {
        auto* comp = componentForContent(content);
        if (comp) {
            comp->setVisible(true);
            if (content == PaneContent::Debug) debugPane.activate();
            if (content == PaneContent::Logs) logPane.activate();
        }
    }
    sidebarDivider.setVisible(paneAssignments[PaneSlot::Sidebar] != PaneContent::Hidden);

    setWantsKeyboardFocus(true);

    // Wire sidebar pane selection to the new system
    // Legacy callbacks (still used by some sidebar paths)
    sidebar.onProduceSelected = [this]() { setPaneContent(PaneSlot::Left, PaneContent::Produce); };
    sidebar.onDebugSelected = [this]() { setPaneContent(PaneSlot::Left, PaneContent::Debug); };
    sidebar.onChatSelected = [this]() { setPaneContent(PaneSlot::Right, PaneContent::Chat); };
    sidebar.onLogsSelected = [this]() { setPaneContent(PaneSlot::Right, PaneContent::Logs); };

    // New pane system callbacks
    sidebar.onPaneSelected = [this](const std::string& slot, const std::string& content) {
        PaneSlot s = PaneSlot::Left;
        if (slot == "sidebar") s = PaneSlot::Sidebar;
        else if (slot == "left") s = PaneSlot::Left;
        else if (slot == "right") s = PaneSlot::Right;
        else if (slot == "bottom") s = PaneSlot::Bottom;
        setPaneContent(s, stringToContent(content));
    };
    sidebar.getPaneContent = [this](const std::string& slot) -> std::string {
        PaneSlot s = PaneSlot::Left;
        if (slot == "sidebar") s = PaneSlot::Sidebar;
        else if (slot == "left") s = PaneSlot::Left;
        else if (slot == "right") s = PaneSlot::Right;
        else if (slot == "bottom") s = PaneSlot::Bottom;
        return contentToString(getPaneContent(s));
    };

    // Load system prompt for Claude
    auto workDir = juce::File(juce::File::getSpecialLocation(juce::File::currentApplicationFile)
                       .getFullPathName());
    for (int i = 0; i < 5; ++i)
        workDir = workDir.getParentDirectory();

    auto runtimeDir = workDir.getChildFile("runtime");
    if (!runtimeDir.getChildFile("CLAUDE.md").existsAsFile())
        runtimeDir = juce::File("/Users/will/ideas_and_projects/performance/runtime");

    auto claudeMd = runtimeDir.getChildFile("CLAUDE.md");
    if (claudeMd.existsAsFile()) {
        auto preamble = juce::String(
            "You have a `perf` tool that executes Lua code directly in the running performance engine. "
            "Use tool calls instead of shell commands. The `code` parameter takes the same Lua that the "
            "API docs below describe. Always use the perf tool to make changes — never suggest shell commands.\n\n");
        chatView.setSystemPrompt(preamble + claudeMd.loadFileAsString());
    }
}

// --- Pane content management ---

juce::Component* MainLayout::componentForContent(PaneContent content) {
    switch (content) {
        case PaneContent::SidebarTree: return &sidebar;
        case PaneContent::Produce:     return &producePane;
        case PaneContent::Mappings:    return &mappingPane;
        case PaneContent::Debug:       return &debugPane;
        case PaneContent::Chat:        return &chatView;
        case PaneContent::Logs:        return &logPane;
        case PaneContent::Mixer:       return &mixerView;
        default:                       return nullptr;
    }
}

std::string MainLayout::contentToString(PaneContent content) {
    switch (content) {
        case PaneContent::Hidden:      return "hidden";
        case PaneContent::SidebarTree: return "sidebar_tree";
        case PaneContent::Produce:     return "produce";
        case PaneContent::Mappings:    return "mappings";
        case PaneContent::Debug:       return "debug";
        case PaneContent::Chat:        return "chat";
        case PaneContent::Logs:        return "logs";
        case PaneContent::Mixer:       return "mixer";
    }
    return "hidden";
}

PaneContent MainLayout::stringToContent(const std::string& s) {
    if (s == "sidebar_tree") return PaneContent::SidebarTree;
    if (s == "produce")      return PaneContent::Produce;
    if (s == "mappings")     return PaneContent::Mappings;
    if (s == "debug")        return PaneContent::Debug;
    if (s == "chat")         return PaneContent::Chat;
    if (s == "logs")         return PaneContent::Logs;
    if (s == "mixer")        return PaneContent::Mixer;
    return PaneContent::Hidden;
}

const char* MainLayout::contentLabel(PaneContent content) {
    switch (content) {
        case PaneContent::Hidden:      return "Hide";
        case PaneContent::SidebarTree: return "Sidebar";
        case PaneContent::Produce:     return "Produce";
        case PaneContent::Mappings:    return "Mappings";
        case PaneContent::Debug:       return "Debug";
        case PaneContent::Chat:        return "Chat";
        case PaneContent::Logs:        return "Logs";
        case PaneContent::Mixer:       return "Mixer";
    }
    return "?";
}

void MainLayout::setPaneContent(PaneSlot slot, PaneContent content) {
    auto oldContent = paneAssignments[slot];
    if (oldContent == content) return;

    // Hide old content component
    auto* oldComp = componentForContent(oldContent);
    if (oldComp) {
        oldComp->setVisible(false);
        if (oldContent == PaneContent::Debug) debugPane.deactivate();
        if (oldContent == PaneContent::Logs) logPane.deactivate();
    }

    paneAssignments[slot] = content;

    // Show new content component
    auto* newComp = componentForContent(content);
    if (newComp) {
        newComp->setVisible(true);
        if (content == PaneContent::Debug) debugPane.activate();
        if (content == PaneContent::Logs) logPane.activate();
    }

    // Update sidebar divider visibility
    sidebarDivider.setVisible(paneAssignments[PaneSlot::Sidebar] != PaneContent::Hidden);

    savePaneConfig();
    resized();
    repaint();
}

PaneContent MainLayout::getPaneContent(PaneSlot slot) const {
    auto it = paneAssignments.find(slot);
    return it != paneAssignments.end() ? it->second : PaneContent::Hidden;
}

// Allowed content per slot (opinionated defaults)
static std::vector<PaneContent> allowedContentForSlot(PaneSlot slot) {
    switch (slot) {
        case PaneSlot::Sidebar: return { PaneContent::Hidden, PaneContent::SidebarTree };
        case PaneSlot::Left:    return { PaneContent::Hidden, PaneContent::Produce, PaneContent::Mappings, PaneContent::Debug };
        case PaneSlot::Right:   return { PaneContent::Hidden, PaneContent::Chat, PaneContent::Logs };
        case PaneSlot::Bottom:  return { PaneContent::Hidden, PaneContent::Mixer };
    }
    return { PaneContent::Hidden };
}

juce::PopupMenu MainLayout::buildPaneMenu(PaneSlot slot) {
    auto current = getPaneContent(slot);
    auto allowed = allowedContentForSlot(slot);
    juce::PopupMenu menu;

    // Collect content assigned to other slots (for exclusivity)
    std::set<PaneContent> assignedElsewhere;
    for (auto& [s, c] : paneAssignments)
        if (s != slot && c != PaneContent::Hidden) assignedElsewhere.insert(c);

    for (auto opt : allowed) {
        bool taken = (opt != PaneContent::Hidden && opt != current
                      && assignedElsewhere.count(opt));
        menu.addItem(juce::PopupMenu::Item(contentLabel(opt))
            .setTicked(current == opt)
            .setEnabled(!taken)
            .setAction([this, slot, opt]() { setPaneContent(slot, opt); }));
    }
    return menu;
}

void MainLayout::savePaneConfig() {
    for (auto& [slot, content] : paneAssignments) {
        std::string key;
        switch (slot) {
            case PaneSlot::Sidebar: key = "pane_sidebar"; break;
            case PaneSlot::Left:    key = "pane_left"; break;
            case PaneSlot::Right:   key = "pane_right"; break;
            case PaneSlot::Bottom:  key = "pane_bottom"; break;
        }
        state.setConfig(key, contentToString(content));
    }
}

void MainLayout::loadPaneConfig() {
    auto loadSlot = [&](PaneSlot slot, const std::string& key) {
        auto val = state.getConfig(key);
        if (!val.empty())
            paneAssignments[slot] = stringToContent(val);
    };
    loadSlot(PaneSlot::Sidebar, "pane_sidebar");
    loadSlot(PaneSlot::Left, "pane_left");
    loadSlot(PaneSlot::Right, "pane_right");
    loadSlot(PaneSlot::Bottom, "pane_bottom");
}

// --- Layout ---

void MainLayout::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgApp));

    // Toolbar
    auto toolbar = getLocalBounds().removeFromTop(toolbarHeight);
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRect(toolbar);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine(0.0f, (float)toolbarHeight, (float)getWidth(), (float)toolbarHeight, 1.0f);

    // Sidebar toggle
    auto toggleBounds = juce::Rectangle<int>(4, 4, 24, 24);
    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(toggleBounds.toFloat(), Theme::cornerRadiusSm);

    bool sidebarOpen = (paneAssignments[PaneSlot::Sidebar] != PaneContent::Hidden);
    juce::Path arrow;
    auto a = toggleBounds.reduced(7).toFloat();
    if (sidebarOpen) {
        arrow.addTriangle(a.getRight(), a.getY(),
                          a.getRight(), a.getBottom(),
                          a.getX(), a.getCentreY());
    } else {
        arrow.addTriangle(a.getX(), a.getY(),
                          a.getX(), a.getBottom(),
                          a.getRight(), a.getCentreY());
    }
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.fillPath(arrow);
}

void MainLayout::resized() {
    auto area = getLocalBounds();
    area.removeFromTop(toolbarHeight);

    bool hasSidebar = (paneAssignments[PaneSlot::Sidebar] != PaneContent::Hidden);
    bool hasBottom = (paneAssignments[PaneSlot::Bottom] != PaneContent::Hidden);
    bool hasLeft = (paneAssignments[PaneSlot::Left] != PaneContent::Hidden);
    bool hasRight = (paneAssignments[PaneSlot::Right] != PaneContent::Hidden);

    // Sidebar
    if (hasSidebar) {
        auto* sideComp = componentForContent(paneAssignments[PaneSlot::Sidebar]);
        if (sideComp) sideComp->setBounds(area.removeFromLeft(sidebarWidth));
        sidebarDivider.setBounds(area.getX() - Divider::thickness, area.getY(),
                                  Divider::thickness * 2, area.getHeight());
        sidebarDivider.toFront(false);
    }

    // Bottom pane
    if (hasBottom) {
        auto* botComp = componentForContent(paneAssignments[PaneSlot::Bottom]);
        if (botComp) {
            int botHeight = minPaneSize;
            // MixerView has dynamic height
            if (paneAssignments[PaneSlot::Bottom] == PaneContent::Mixer)
                botHeight = std::max(minPaneSize, std::min(mixerView.getDesiredHeight(),
                                                            area.getHeight() - minPaneSize));
            botComp->setBounds(area.removeFromBottom(botHeight));
        }
    }

    // Left and right panes split the remaining area
    if (hasLeft && hasRight) {
        auto* leftComp = componentForContent(paneAssignments[PaneSlot::Left]);
        auto* rightComp = componentForContent(paneAssignments[PaneSlot::Right]);
        int leftWidth = (int)(area.getWidth() * 0.6f);
        if (leftComp) leftComp->setBounds(area.removeFromLeft(leftWidth));
        if (rightComp) rightComp->setBounds(area);
    } else if (hasLeft) {
        auto* leftComp = componentForContent(paneAssignments[PaneSlot::Left]);
        if (leftComp) leftComp->setBounds(area);
    } else if (hasRight) {
        auto* rightComp = componentForContent(paneAssignments[PaneSlot::Right]);
        if (rightComp) rightComp->setBounds(area);
    }
}

void MainLayout::mouseUp(const juce::MouseEvent& event) {
    auto toggleBounds = juce::Rectangle<int>(4, 4, 24, 24);
    if (toggleBounds.contains(event.getPosition())) {
        auto current = getPaneContent(PaneSlot::Sidebar);
        setPaneContent(PaneSlot::Sidebar,
                       current == PaneContent::Hidden ? PaneContent::SidebarTree : PaneContent::Hidden);
    }
}

void MainLayout::toggleMusicalTyping() {
    musicalTypingActive = !musicalTypingActive;
    musicalTyping.setVisible(musicalTypingActive);
    if (musicalTypingActive) {
        if (musicalTypingLastPos.x < 0) {
            // First open — center at top
            musicalTypingLastPos.x = (getWidth() - musicalTyping.getWidth()) / 2;
            musicalTypingLastPos.y = toolbarHeight + 8;
        }
        musicalTyping.setBounds(musicalTypingLastPos.x, musicalTypingLastPos.y,
                                musicalTyping.getWidth(), musicalTyping.getHeight());
        musicalTyping.toFront(false);
    } else {
        musicalTypingLastPos = musicalTyping.getPosition();
        musicalTyping.allNotesOff();
    }
    repaint();
}

bool MainLayout::handleGlobalKey(const juce::KeyPress& key) {
    // Musical typing toggle — always available
    if (keyBindingMgr && keyBindingMgr->matches("view.musicalTyping", key)) {
        toggleMusicalTyping();
        return true;
    }

    // When musical typing is active, intercept all keys except musical typing toggle
    if (musicalTypingActive) {
        if (musicalTyping.handleKey(key, true))
            return true;
        if (key == juce::KeyPress::escapeKey) {
            toggleMusicalTyping();
            return true;
        }
        return true;  // eat everything else
    }

    // Skip all shortcuts when a text editor has focus
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    if (focused && dynamic_cast<juce::TextEditor*>(focused))
        return false;

    // Use KeyBindingManager if available, fall back to hardcoded
    auto matches = [this](const std::string& cmd, const juce::KeyPress& k) {
        if (keyBindingMgr) return keyBindingMgr->matches(cmd, k);
        return false;
    };

    // --- View ---
    if (matches("view.sidebar", key)) {
        auto current = getPaneContent(PaneSlot::Sidebar);
        setPaneContent(PaneSlot::Sidebar,
                       current == PaneContent::Hidden ? PaneContent::SidebarTree : PaneContent::Hidden);
        return true;
    }
    if (matches("view.mixer", key)) {
        auto current = getPaneContent(PaneSlot::Bottom);
        setPaneContent(PaneSlot::Bottom,
                       current == PaneContent::Hidden ? PaneContent::Mixer : PaneContent::Hidden);
        return true;
    }
    if (matches("view.produce", key)) {
        auto current = getPaneContent(PaneSlot::Left);
        setPaneContent(PaneSlot::Left,
                       current == PaneContent::Produce ? PaneContent::Hidden : PaneContent::Produce);
        return true;
    }
    if (matches("view.mappings", key)) {
        auto current = getPaneContent(PaneSlot::Left);
        setPaneContent(PaneSlot::Left,
                       current == PaneContent::Mappings ? PaneContent::Hidden : PaneContent::Mappings);
        return true;
    }
    if (matches("view.chat", key)) {
        auto current = getPaneContent(PaneSlot::Right);
        setPaneContent(PaneSlot::Right,
                       current == PaneContent::Chat ? PaneContent::Hidden : PaneContent::Chat);
        return true;
    }
    if (matches("view.logs", key)) {
        auto current = getPaneContent(PaneSlot::Right);
        setPaneContent(PaneSlot::Right,
                       current == PaneContent::Logs ? PaneContent::Hidden : PaneContent::Logs);
        return true;
    }
    if (matches("view.closeEditor", key)) {
        engine.closeTopPluginEditor();
        return true;
    }
    if (matches("view.zoomIn", key)) {
        producePane.keyPressed(key); return true;
    }
    if (matches("view.zoomOut", key)) {
        producePane.keyPressed(key); return true;
    }
    if (matches("view.zoomTaller", key)) {
        producePane.keyPressed(key); return true;
    }
    if (matches("view.zoomShorter", key)) {
        producePane.keyPressed(key); return true;
    }

    // --- File ---
    if (matches("file.newSong", key)) {
        if (onNewSong) onNewSong();
        return true;
    }
    if (matches("file.save", key)) {
        if (onSave) onSave();
        return true;
    }
    if (matches("file.settings", key)) {
        if (onOpenSettings) onOpenSettings();
        return true;
    }

    // --- Track ---
    if (matches("track.newInstrument", key)) {
        if (onNewInstrumentTrack) onNewInstrumentTrack();
        return true;
    }
    if (matches("track.newAudioInput", key)) {
        if (onNewAudioTrack) onNewAudioTrack();
        return true;
    }
    if (matches("track.newBus", key)) {
        if (onNewBus) onNewBus();
        return true;
    }

    // --- Edit (check redo before undo — redo has shift) ---
    if (matches("edit.redo", key)) {
        if (onRedo) onRedo();
        return true;
    }
    if (matches("edit.undo", key)) {
        if (onUndo) onUndo();
        return true;
    }

    // --- Forward remaining to ProducePane (transport, region, edit operations) ---
    // These work regardless of which pane is focused
    if (matches("transport.playStop", key) || matches("transport.record", key) ||
        matches("transport.rewind", key) || matches("transport.cycle", key) ||
        matches("transport.metronome", key) || matches("transport.stepFwd", key) ||
        matches("transport.stepBack", key) || matches("transport.stepFwdBar", key) ||
        matches("transport.stepBackBar", key) ||
        matches("edit.split", key) || matches("edit.duplicate", key) ||
        matches("edit.delete", key) || matches("edit.cycleFromSel", key) ||
        matches("region.loop", key)) {
        producePane.keyPressed(key);
        return true;
    }

    return false;
}

bool MainLayout::handleGlobalKeyUp(const juce::KeyPress& key) {
    if (musicalTypingActive)
        return musicalTyping.handleKey(key, false);
    return false;
}

void MainLayout::showOverlay(const juce::String& message) {
    overlay.message = message;
    overlay.setBounds(getLocalBounds());
    addAndMakeVisible(overlay);
    overlay.toFront(false);
    overlay.repaint();
}

void MainLayout::hideOverlay() {
    removeChildComponent(&overlay);
}
