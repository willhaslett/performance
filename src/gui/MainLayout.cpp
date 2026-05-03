#include "gui/MainLayout.h"
#include "gui/KeyBindings.h"
#include "gui/Theme.h"
#include "gui/UiTerms.h"
#include "BuildVersion.h"
#include "BinaryData.h"
#include <set>
#include "engine/Log.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "api/JsonFileChatHistoryStore.h"
#include "api/PerformanceCoordinator.h"
#include "state/ActionRefs.h"
#include "state/StateEvents.h"

MainLayout::MainLayout(StateAPI& state, EngineAPI& engine, LuaEngine& lua,
                       PerformanceCoordinator& coordinator)
    : state(state), engine(engine),
      debugPane(coordinator, engine),
      performPane(state, engine, coordinator),
      looperPane(state, engine, coordinator),
      chatView(lua), mixerView(state, engine) {
    sidebar.setStateAPI(&state);
    sidebar.setEngineAPI(&engine);
    sidebar.setCoordinator(&coordinator);

    producePane.setState(&state, coordinator.sequencer(), &coordinator.arrangement());
    producePane.onStartRecordMode = [&coordinator]() { coordinator.startRecordMode(); };
    producePane.onStopRecordMode = [&coordinator]() { coordinator.stopRecordMode(); };
    producePane.onIsRecordMode = [&coordinator]() { return coordinator.isInRecordMode(); };
    producePane.onRegionsChanged = [&coordinator]() { coordinator.reloadAudioFiles(); };

    looperPane.setSequencer(coordinator.sequencer());
    looperPane.setOnShowPerformPane([this] {
        setPaneContent(PaneSlot::Left, PaneContent::Perform);
        savePaneConfig();
    });

    // After a song loads, scan its bindings + action events for refs that
    // don't resolve anymore (e.g. a track got deleted in a past session and
    // left stale bindings). Per doc: show the user a one-time dialog rather
    // than silently pruning or letting the assertion fire at trigger time.
    coordinator.onSongLoaded = [this]() {
        auto stale = ActionRefs::findStaleRefs(this->state);
        if (stale.empty()) return;

        juce::String body;
        body << "This " << UiTerms::docSingularLower << " has " << (int)stale.size()
             << (stale.size() == 1 ? " binding or action event" : " bindings or action events")
             << " referencing entities that no longer exist:\n\n";
        int shown = 0;
        for (auto& s : stale) {
            if (shown++ >= 6) { body << "  ...and more\n"; break; }
            body << "  • " << juce::String(s.summary) << "\n";
        }
        body << "\nDelete them? (Keeping them will crash when triggered.)";

        // Move the removes into a shared lambda-owned vector so the callback
        // can fire them all after the modal closes.
        auto removesPtr = std::make_shared<std::vector<std::function<void()>>>();
        for (auto& s : stale) removesPtr->push_back(std::move(s.remove));

        juce::AlertWindow::showOkCancelBox(
            juce::MessageBoxIconType::WarningIcon,
            "Stale references found", body,
            "Delete", "Keep", nullptr,
            juce::ModalCallbackFunction::create([removesPtr](int ok) {
                if (ok != 1) return;
                for (auto& r : *removesPtr) r();
            }));
    };

    // All components start hidden — setPaneContent will show the right ones
    sidebar.setVisible(false);
    producePane.setVisible(false);
    looperPane.setVisible(false);
    performPane.setVisible(false);
    debugPane.setVisible(false);
    chatView.setVisible(false);
    logPane.setVisible(false);
    mixerView.setVisible(false);

    addChildComponent(sidebar);
    addChildComponent(producePane);
    addChildComponent(looperPane);
    addChildComponent(performPane);

    // Single global MIDI monitor — coordinator.setGlobalMidiMonitor is a
    // last-writer-wins slot, so we install it once here and dispatch to both
    // panes. This keeps the Controllers activity dots alive alongside the
    // SongMappings row pulses.
    coordinator.setGlobalMidiMonitor(
        [this](const std::string& deviceName, const std::string&,
               const std::string& evType, int ch, int num, int) {
            auto* dev = this->state.findDeviceByPortName(deviceName);
            if (!dev) return;
            auto devId = dev->id;
            std::string ctrlType;
            if (evType == "CC") ctrlType = "cc";
            else if (evType == "NoteOn" || evType == "NoteOff") ctrlType = "note";
            else if (evType == "Pitch") ctrlType = "pitchbend";
            else if (evType == "Pressure") ctrlType = "pressure";
            if (ctrlType.empty()) return;
            juce::MessageManager::callAsync([this, ctrlType, ch, num, devId = devId.str()] {
                performPane.controllers().handleMidiActivity(ctrlType, ch, num, devId);
                performPane.songMappings().handleMidiActivity(ctrlType, ch, num, devId);
            });
        });
    addChildComponent(debugPane);
    addChildComponent(chatView);
    addChildComponent(logPane);
    addChildComponent(mixerView);

    // Musical Typing (hidden by default)
    musicalTyping.setAudioEngine(&engine.getAudioEngine());
    addChildComponent(musicalTyping);

    // Build info — read-only TextEditor so text is selectable/copyable (⌘C)
    {
        juce::String info;
        juce::String tag(BUILD_GIT_TAG);
        if (tag.isNotEmpty()) info = "v" + tag + "  ";
        info += juce::String(BUILD_GIT_COMMIT);
        buildInfoField.setReadOnly(true);
        buildInfoField.setCaretVisible(false);
        buildInfoField.setScrollbarsShown(false);
        buildInfoField.setFont(Theme::font(Theme::fontSizeSm));
        buildInfoField.setJustification(juce::Justification::centred);
        buildInfoField.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        buildInfoField.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        buildInfoField.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        buildInfoField.setColour(juce::TextEditor::textColourId,
                                 Theme::color(Theme::Color::textDim));
        buildInfoField.setColour(juce::TextEditor::highlightColourId,
                                 Theme::color(Theme::Color::accent));
        buildInfoField.setColour(juce::TextEditor::highlightedTextColourId,
                                 Theme::color(Theme::Color::textOnColor));
        buildInfoField.setText(info, false);
        addAndMakeVisible(buildInfoField);
    }

    // Sidebar divider
    addAndMakeVisible(sidebarDivider);
    sidebarDivider.onDragStart = [this]() { dragStartSidebarWidth = sidebarWidth; };
    sidebarDivider.onDrag = [this](int delta) {
        sidebarWidth = std::max(minPaneSize, dragStartSidebarWidth + delta);
        resized();
    };

    // Center divider — between the Left and Right pane slots. Drag-to-
    // resize. Stored width persists in config so it survives relaunch.
    addAndMakeVisible(centerDivider);
    centerDivider.onDragStart = [this]() {
        // Use the actual rendered left-pane width as the drag baseline,
        // not the override (which may be -1 = auto).
        if (auto* leftComp = componentForContent(paneAssignments[PaneSlot::Left]))
            dragStartLeftPaneWidth = leftComp->getWidth();
        else
            dragStartLeftPaneWidth = 0;
    };
    centerDivider.onDrag = [this](int delta) {
        leftPaneWidthOverride = std::max(minPaneSize, dragStartLeftPaneWidth + delta);
        this->state.setConfig("left_pane_width", std::to_string(leftPaneWidthOverride));
        resized();
    };

    // Default pane assignments
    paneAssignments[PaneSlot::Sidebar] = PaneContent::SidebarTree;
    paneAssignments[PaneSlot::Left] = PaneContent::Produce;
    paneAssignments[PaneSlot::Right] = PaneContent::Chat;
    paneAssignments[PaneSlot::Bottom] = PaneContent::Mixer;

    loadPaneConfig();
    {
        auto saved = state.getConfig("left_pane_width");
        if (!saved.empty()) leftPaneWidthOverride = std::max(minPaneSize, std::stoi(saved));
    }

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

    // Sidebar navigation. Left-slot clicks (produce / looper / perform)
    // are mutually exclusive workspace selectors — clicking any of
    // them switches the Left slot to that content. App mode follows
    // automatically via the bridge in setPaneContent.
    // See docs/PANE_MODE_MODEL.md.
    sidebar.onToggleView = [this](const std::string& viewName) {
        if (viewName == "sidebar") {
            auto cur = getPaneContent(PaneSlot::Sidebar);
            setPaneContent(PaneSlot::Sidebar,
                           cur == PaneContent::Hidden ? PaneContent::SidebarTree : PaneContent::Hidden);
        } else if (viewName == "produce") {
            setPaneContent(PaneSlot::Left, PaneContent::Produce);
        } else if (viewName == "looper") {
            setPaneContent(PaneSlot::Left, PaneContent::Looper);
        } else if (viewName == "perform") {
            setPaneContent(PaneSlot::Left, PaneContent::Perform);
        } else if (viewName == "mixer") {
            auto cur = getPaneContent(PaneSlot::Bottom);
            setPaneContent(PaneSlot::Bottom,
                           cur == PaneContent::Mixer ? PaneContent::Hidden : PaneContent::Mixer);
        } else if (viewName == "chat") {
            auto cur = getPaneContent(PaneSlot::Right);
            setPaneContent(PaneSlot::Right,
                           cur == PaneContent::Chat ? PaneContent::Hidden : PaneContent::Chat);
        }
        savePaneConfig();
    };
    sidebar.isViewActive = [this](const std::string& viewName) -> bool {
        if (viewName == "sidebar")  return getPaneContent(PaneSlot::Sidebar) != PaneContent::Hidden;
        if (viewName == "produce")  return getPaneContent(PaneSlot::Left) == PaneContent::Produce;
        if (viewName == "looper")   return getPaneContent(PaneSlot::Left) == PaneContent::Looper;
        if (viewName == "perform")  return getPaneContent(PaneSlot::Left) == PaneContent::Perform;
        if (viewName == "mixer")    return getPaneContent(PaneSlot::Bottom) == PaneContent::Mixer;
        if (viewName == "chat")     return getPaneContent(PaneSlot::Right) == PaneContent::Chat;
        return false;
    };
    // sidebar.onNewSong is wired by main.mm after layout construction.

    // Load the chat LLM's system prompt from BinaryData (baked in at
    // build time from runtime/SYSTEM_PROMPT.md — rebuilt automatically
    // when the .md changes). Composition is part of the same prompt;
    // there is no separate compose mode.
    const auto toolPreamble = juce::String(
        "You have a `perf` tool that executes Lua code directly in the running performance engine. "
        "Use tool calls instead of shell commands. The `code` parameter takes the same Lua that the "
        "API docs below describe. Always use the perf tool to make changes — never suggest shell commands.\n\n");

    juce::String perfPrompt;
    int size = 0;
    if (auto* data = BinaryData::getNamedResource("SYSTEM_PROMPT_md", size); data && size > 0) {
        perfPrompt = toolPreamble + juce::String::fromUTF8(data, size);
    } else {
        perfLog("[MainLayout] WARNING: SYSTEM_PROMPT.md not found in BinaryData — chat LLM will have no perf prompt\n");
    }
    chatView.setSystemPrompt(perfPrompt);

    // Wire chat-history persistence. Single JSON file per install,
    // shared across songs (one continuous conversation, not per-song).
    // Decoupled from state.db because chat isn't song state — and so we
    // can swap the storage backend without touching ClaudeClient.
    auto chatHistoryFile = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                              .getChildFile(".config/performance/chat_history.json");
    chatView.setHistoryStore(std::make_unique<JsonFileChatHistoryStore>(chatHistoryFile));

    // Reverse-sync: when currentMode flips from outside the GUI (Lua,
    // Claude via perf, IPC, MIDI bindings), bring the Left slot in
    // line. The forward direction is handled in setPaneContent; the
    // idempotent guard in StateAPI::setMode breaks the potential loop
    // between these two handlers.
    // See docs/PANE_MODE_MODEL.md.
    stateSubId = state.events().subscribe([this](const StateEvent& ev) {
        if (ev.entity != StateEvent::App || ev.action != StateEvent::Updated) return;
        bool looperMode = this->state.getMode() == AppMode::Looper;
        auto cur = getPaneContent(PaneSlot::Left);
        if (looperMode && cur != PaneContent::Looper) {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainLayout>(this)] {
                if (safe) safe->setPaneContent(PaneSlot::Left, PaneContent::Looper);
            });
        } else if (!looperMode && cur == PaneContent::Looper) {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainLayout>(this)] {
                if (safe) safe->setPaneContent(PaneSlot::Left, PaneContent::Produce);
            });
        }
    });
}

MainLayout::~MainLayout() {
    if (stateSubId >= 0)
        state.events().unsubscribe(stateSubId);
}

// --- Pane content management ---

juce::Component* MainLayout::componentForContent(PaneContent content) {
    switch (content) {
        case PaneContent::SidebarTree: return &sidebar;
        case PaneContent::Produce:     return &producePane;
        case PaneContent::Looper:      return &looperPane;
        case PaneContent::Perform:     return &performPane;
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
        case PaneContent::Looper:      return "looper";
        case PaneContent::Perform:     return "perform";
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
    if (s == "looper")       return PaneContent::Looper;
    if (s == "perform")      return PaneContent::Perform;
    if (s == "debug")        return PaneContent::Debug;
    if (s == "chat")         return PaneContent::Chat;
    if (s == "logs")         return PaneContent::Logs;
    if (s == "mixer")        return PaneContent::Mixer;
    return PaneContent::Hidden;  // includes legacy "controllers" / "song_mappings" / "mappings"
}

const char* MainLayout::contentLabel(PaneContent content) {
    switch (content) {
        case PaneContent::Hidden:      return "Hide";
        case PaneContent::SidebarTree: return "Sidebar";
        case PaneContent::Produce:     return "Produce";
        case PaneContent::Looper:      return "Looper";
        case PaneContent::Perform:     return "Perform";
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

    // Single SSOT bridge: Left-slot content drives currentMode.
    // See docs/PANE_MODE_MODEL.md. Every GUI path that changes the Left
    // slot funnels through here; no duplicate mode-flip logic elsewhere.
    // setMode is idempotent, so no-op when value unchanged.
    if (slot == PaneSlot::Left)
        state.setMode(content == PaneContent::Looper ? AppMode::Looper : AppMode::Arrangement);

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
        case PaneSlot::Left:    return { PaneContent::Hidden, PaneContent::Produce, PaneContent::Perform, PaneContent::Debug };
        case PaneSlot::Right:   return { PaneContent::Hidden, PaneContent::Chat, PaneContent::Logs };
        case PaneSlot::Bottom:  return { PaneContent::Hidden, PaneContent::Mixer };
    }
    return { PaneContent::Hidden };
}

// Preferred proportion of the Left/Right row for each content type when it
// occupies the Left slot and has a Right sibling. Right slot gets (1 - left).
// Used to snap the split to a sensible width whenever content is swapped.
static float preferredLeftProportion(PaneContent content) {
    switch (content) {
        case PaneContent::Produce:     return 0.65f;  // wide timeline
        case PaneContent::Perform:     return 0.75f;  // Controllers + SongMappings side-by-side
        case PaneContent::Debug:       return 0.50f;
        default:                       return 0.50f;
    }
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
    // (Vertical divider between Left and Right panes is centerDivider,
    // a child Component — paints itself on top of everything.)
}

void MainLayout::resized() {
    auto area = getLocalBounds();

    bool hasSidebar = (paneAssignments[PaneSlot::Sidebar] != PaneContent::Hidden);
    bool hasBottom = (paneAssignments[PaneSlot::Bottom] != PaneContent::Hidden);
    bool hasLeft = (paneAssignments[PaneSlot::Left] != PaneContent::Hidden);
    bool hasRight = (paneAssignments[PaneSlot::Right] != PaneContent::Hidden);

    // Sidebar
    if (hasSidebar) {
        auto* sideComp = componentForContent(paneAssignments[PaneSlot::Sidebar]);
        juce::Rectangle<int> sideBounds;
        if (sideComp) {
            sideBounds = area.removeFromLeft(sidebarWidth);
            sideComp->setBounds(sideBounds);
        }
        sidebarDivider.setBounds(area.getX() - Divider::thickness, area.getY(),
                                  Divider::thickness * 2, area.getHeight());
        sidebarDivider.toFront(false);

        // Build info — centered along the sidebar's bottom edge.
        // Dropped from the old top toolbar (which is gone). Width
        // matches the sidebar so the readout always fits.
        constexpr int buildInfoH = 24;
        constexpr int buildInfoBotPad = 6;
        buildInfoField.setBounds(sideBounds.getX(),
                                  sideBounds.getBottom() - buildInfoH - buildInfoBotPad,
                                  sideBounds.getWidth(), buildInfoH);
        buildInfoField.setVisible(true);
        buildInfoField.toFront(false);
    } else {
        buildInfoField.setVisible(false);
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
        // User override (drag) wins over the per-content preferred prop.
        int leftWidth;
        if (leftPaneWidthOverride > 0) {
            leftWidth = leftPaneWidthOverride;
        } else {
            float prop = preferredLeftProportion(paneAssignments[PaneSlot::Left]);
            leftWidth = (int)(area.getWidth() * prop);
        }
        leftWidth = std::max(minPaneSize,
                              std::min(area.getWidth() - minPaneSize, leftWidth));
        if (leftComp) leftComp->setBounds(area.removeFromLeft(leftWidth));
        if (rightComp) rightComp->setBounds(area);
        // Center divider — straddles the boundary between the two
        // panes so it's visible no matter what content is in either slot.
        centerDivider.setBounds(area.getX() - Divider::thickness, area.getY(),
                                 Divider::thickness * 2, area.getHeight());
        centerDivider.setVisible(true);
        centerDivider.toFront(false);
    } else if (hasLeft) {
        auto* leftComp = componentForContent(paneAssignments[PaneSlot::Left]);
        if (leftComp) leftComp->setBounds(area);
        centerDivider.setVisible(false);
    } else if (hasRight) {
        auto* rightComp = componentForContent(paneAssignments[PaneSlot::Right]);
        if (rightComp) rightComp->setBounds(area);
        centerDivider.setVisible(false);
    } else {
        centerDivider.setVisible(false);
    }
}

void MainLayout::mouseUp(const juce::MouseEvent& /*event*/) {
}

void MainLayout::mouseMove(const juce::MouseEvent& /*event*/) {
}

void MainLayout::toggleMusicalTyping() {
    musicalTypingActive = !musicalTypingActive;
    musicalTyping.setVisible(musicalTypingActive);
    if (musicalTypingActive) {
        if (musicalTypingLastPos.x < 0) {
            // First open — center at top
            musicalTypingLastPos.x = (getWidth() - musicalTyping.getWidth()) / 2;
            musicalTypingLastPos.y = 8;
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
    if (matches("view.looper", key)) {
        auto current = getPaneContent(PaneSlot::Left);
        setPaneContent(PaneSlot::Left,
                       current == PaneContent::Looper ? PaneContent::Hidden : PaneContent::Looper);
        return true;
    }
    if (matches("view.mappings", key)) {
        auto current = getPaneContent(PaneSlot::Left);
        setPaneContent(PaneSlot::Left,
                       current == PaneContent::Perform ? PaneContent::Hidden : PaneContent::Perform);
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

// --- Startup Chooser ---

void MainLayout::StartupChooser::setSongs(
        const std::vector<std::pair<std::string, std::string>>& songList) {
    songs.clear();
    for (auto& [id, name] : songList)
        songs.push_back({ id, juce::String(name), {} });
    resized();
    repaint();
}

void MainLayout::StartupChooser::resized() {
    int cardW = 400;
    int titleH = 50;
    int listPad = 16;
    int itemH = 32;
    int minListRows = 5;
    int listRows = std::max(minListRows, (int)songs.size());
    int listH = listRows * itemH;
    int buttonH = 38;
    int sectionGap = 16;
    int cardPad = 20;

    int cardX = (getWidth() - cardW) / 2;
    int cardY = getHeight() / 5;

    // Song rows inside the list container
    int listX = cardX + cardPad;
    int listY = cardY + titleH + listPad;
    int listW = cardW - cardPad * 2;

    for (int i = 0; i < (int)songs.size(); ++i)
        songs[i].bounds = { listX + 4, listY + i * itemH, listW - 8, itemH };

    // New Song button below the list
    int btnY = cardY + titleH + listPad * 2 + listH + sectionGap;
    newSongBounds = { cardX + cardPad, btnY, cardW - cardPad * 2, buttonH };
}

void MainLayout::StartupChooser::paint(juce::Graphics& g) {
    // Gentle dim — enough to accent the popup without crushing the background
    g.fillAll(juce::Colour(0x60000000));

    // Card layout constants
    int cardW = 400;
    int titleH = 50;
    int listPad = 16;
    int itemH = 32;
    int minListRows = 5;
    int listRows = std::max(minListRows, (int)songs.size());
    int listH = listRows * itemH;
    int buttonH = 38;
    int sectionGap = 16;
    int cardPad = 20;
    int cardH = titleH + listPad * 2 + listH + sectionGap + buttonH + cardPad;

    int cardX = (getWidth() - cardW) / 2;
    int cardY = getHeight() / 5;
    auto cardBounds = juce::Rectangle<int>(cardX, cardY, cardW, cardH);

    // Shadow
    g.setColour(juce::Colour(0x18000000));
    g.fillRoundedRectangle(cardBounds.expanded(6).toFloat(), Theme::cornerRadius + 2);

    // Card fill
    g.setColour(Theme::color(Theme::Color::bgPanel));
    g.fillRoundedRectangle(cardBounds.toFloat(), Theme::cornerRadius);
    g.setColour(Theme::color(Theme::Color::border));
    g.drawRoundedRectangle(cardBounds.toFloat(), Theme::cornerRadius, 1.0f);

    // Title
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(Theme::fontSizeTitle));
    g.drawText("Choose a " + UiTerms::docSingular, cardBounds.getX(), cardBounds.getY(),
               cardBounds.getWidth(), titleH, juce::Justification::centred);

    // Song list container — Finder-like inset pane
    int listX = cardX + cardPad;
    int listY = cardY + titleH;
    int listW = cardW - cardPad * 2;
    auto listBounds = juce::Rectangle<int>(listX, listY, listW, listH + listPad * 2);

    g.setColour(Theme::color(Theme::Color::bgSlot));
    g.fillRoundedRectangle(listBounds.toFloat(), Theme::cornerRadiusSm);

    // Song rows — each row gets a visible background so it reads as a
    // discrete item even when there's only one. The empty space below
    // stays as the container color, providing the "this is a list" cue.
    for (int i = 0; i < (int)songs.size(); ++i) {
        auto& s = songs[i];
        bool hovered = (hoveredIndex == i);

        if (hovered) {
            g.setColour(Theme::color(Theme::Color::accent));
            g.fillRoundedRectangle(s.bounds.toFloat(), Theme::cornerRadiusXs);
            g.setColour(Theme::color(Theme::Color::textOnColor));
        } else {
            // Alternating row tones for visual rhythm
            g.setColour(Theme::color((i % 2 == 0) ? Theme::Color::bgControl
                                                   : Theme::Color::bgStripe));
            g.fillRoundedRectangle(s.bounds.toFloat(), Theme::cornerRadiusXs);
            g.setColour(Theme::color(Theme::Color::textPrimary));
        }
        g.setFont(Theme::font(Theme::fontSizeLg));
        g.drawText(s.name, s.bounds.reduced(Theme::spacingL, 0),
                   juce::Justification::centredLeft);
    }

    if (songs.empty()) {
        g.setColour(Theme::color(Theme::Color::textDim));
        g.setFont(Theme::font(Theme::fontSizeMd));
        g.drawText("No " + UiTerms::docPluralLower + " yet", listBounds, juce::Justification::centred);
    }

    // "Create New Song" button — visually distinct from the list
    bool newHovered = (hoveredIndex == -2);
    if (newHovered) {
        g.setColour(Theme::color(Theme::Color::accent));
        g.fillRoundedRectangle(newSongBounds.toFloat(), Theme::cornerRadiusSm);
        g.setColour(Theme::color(Theme::Color::textOnColor));
    } else {
        g.setColour(Theme::color(Theme::Color::accent).withAlpha(0.7f));
        g.drawRoundedRectangle(newSongBounds.toFloat().reduced(0.5f), Theme::cornerRadiusSm, 1.5f);
        g.setColour(Theme::color(Theme::Color::accent));
    }
    g.setFont(Theme::font(Theme::fontSizeLg));
    g.drawText("Create New " + UiTerms::docSingular, newSongBounds, juce::Justification::centred);
}

void MainLayout::StartupChooser::mouseUp(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    for (int i = 0; i < (int)songs.size(); ++i) {
        if (songs[i].bounds.contains(pos) && onLoadSong) {
            onLoadSong(songs[i].id);
            return;
        }
    }
    if (newSongBounds.contains(pos) && onNewSong) {
        onNewSong();
    }
}

void MainLayout::StartupChooser::mouseMove(const juce::MouseEvent& event) {
    int prev = hoveredIndex;
    hoveredIndex = -1;
    auto pos = event.getPosition();
    for (int i = 0; i < (int)songs.size(); ++i) {
        if (songs[i].bounds.contains(pos)) { hoveredIndex = i; break; }
    }
    if (hoveredIndex == -1 && newSongBounds.contains(pos))
        hoveredIndex = -2;
    if (prev != hoveredIndex) repaint();
}
