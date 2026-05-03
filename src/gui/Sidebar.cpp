#include "gui/Sidebar.h"
#include "gui/UiTerms.h"
#include "api/StateAPI.h"
#include "api/EngineAPI.h"
#include "state/StateEvents.h"

Sidebar::Sidebar() {
    startTimerHz(4);
}

Sidebar::~Sidebar() {
    if (state && subscriptionId >= 0)
        state->events().unsubscribe(subscriptionId);
}

void Sidebar::setStateAPI(StateAPI* s) {
    state = s;
    if (state) {
        subscriptionId = state->events().subscribe([this](const StateEvent&) {
            juce::MessageManager::callAsync([this] { rebuild(); repaint(); });
        });
    }
    rebuild();
}

void Sidebar::setEngineAPI(EngineAPI* e) { engineAPI = e; }
void Sidebar::setCoordinator(PerformanceCoordinator* c) { coordinator = c; }

void Sidebar::timerCallback() {
    // Periodic refresh for dynamic content (MIDI device hot-plug, etc.)
    rebuild();
    repaint();
}

void Sidebar::rebuild() {
    items.clear();
    int y = Theme::spacingM;

    // --- View ---
    items.push_back({ Item::SectionHeader, "View", "", {} });
    items.back().bounds = { 0, y, getWidth(), sectionHeight };
    y += sectionHeight;

    // Order matches the keyboard row (left → right): Y · U · I · O · P.
    // Sidebar itself is ⌘S (handled in the View menu, not listed here).
    juce::String cmd = juce::String::fromUTF8("\xe2\x8c\x98");
    for (auto& v : std::vector<std::tuple<std::string, juce::String, juce::String>>{
        {"produce",  "Produce",  cmd + "Y"},
        {"perform",  "Perform",  cmd + "U"},
        {"chat",     "Chat",     cmd + "I"},
        {"mixer",    "Mixer",    cmd + "O"},
        {"looper",   "Looper",   cmd + "P"},
    }) {
        auto& [id, label, shortcut] = v;
        items.push_back({ Item::ViewToggle, label, id, {}, shortcut });
        items.back().bounds = { 0, y, getWidth(), itemHeight };
        y += itemHeight;
    }

    y += sectionGap;

    // --- Songs ---
    items.push_back({ Item::SectionHeader, UiTerms::docPlural, "", {} });
    items.back().bounds = { 0, y, getWidth(), sectionHeight };
    y += sectionHeight;

    if (state) {
        auto* currentSong = state->currentSong();
        for (auto& song : state->allSongs()) {
            items.push_back({ Item::SongEntry, juce::String(song.name), song.id.str(), {} });
            items.back().bounds = { 0, y, getWidth(), itemHeight };
            y += itemHeight;
        }
    }

    // + New Song
    items.push_back({ Item::ActionButton, "+ New " + UiTerms::docSingular, "new_song", {} });
    items.back().bounds = { 0, y, getWidth(), itemHeight };
    y += itemHeight;

    // --- Track Presets ---
    // Lists every saved track preset alphabetically. Click creates a
    // fresh track and loads the preset onto it. Section is omitted
    // entirely when there are no presets — no point in a header that
    // only labels emptiness.
    if (onListTrackPresets) {
        auto presets = onListTrackPresets();
        std::sort(presets.begin(), presets.end(),
            [](const juce::String& a, const juce::String& b) {
                return a.compareIgnoreCase(b) < 0;
            });
        if (! presets.empty()) {
            y += sectionGap;
            items.push_back({ Item::SectionHeader, "Track Presets", "", {} });
            items.back().bounds = { 0, y, getWidth(), sectionHeight };
            y += sectionHeight;

            for (auto& name : presets) {
                items.push_back({ Item::TrackPresetEntry, name, name.toStdString(), {} });
                items.back().bounds = { 0, y, getWidth(), itemHeight };
                y += itemHeight;
            }
        }
    }
}

void Sidebar::paint(juce::Graphics& g) {
    g.fillAll(Theme::color(Theme::Color::bgSurface));

    // Right border
    g.setColour(Theme::color(Theme::Color::border));
    g.drawLine((float)getWidth() - 1, 0.0f, (float)getWidth() - 1, (float)getHeight(), 1.0f);

    std::string currentSongId;
    if (state) {
        auto* cur = state->currentSong();
        if (cur) currentSongId = cur->id.str();
    }

    for (int i = 0; i < (int)items.size(); ++i) {
        auto& item = items[i];
        auto bounds = item.bounds;

        if (item.kind == Item::SectionHeader) {
            g.setColour(Theme::color(Theme::Color::textDim));
            g.setFont(Theme::font(Theme::fontSizeLg));
            g.drawText(item.label, bounds.withLeft(leftPad), juce::Justification::centredLeft);
            continue;
        }

        bool hovered = (i == hoveredIndex);
        bool active = false;

        if (item.kind == Item::ViewToggle && isViewActive)
            active = isViewActive(item.id);
        else if (item.kind == Item::SongEntry)
            active = (item.id == currentSongId);

        // Row background: current song stays highlighted (meaningful indicator);
        // view toggles only show hover since the displayed pane is self-evident.
        if (active && item.kind == Item::SongEntry) {
            g.setColour(Theme::color(Theme::Color::bgListActive));
            g.fillRect(bounds);
        } else if (hovered) {
            g.setColour(Theme::color(Theme::Color::bgControlHover));
            g.fillRect(bounds);
        }

        // Label
        if (item.kind == Item::ActionButton) {
            g.setColour(Theme::color(Theme::Color::textSecondary));
        } else if (active) {
            g.setColour(Theme::color(Theme::Color::textPrimary));
        } else {
            g.setColour(hovered ? Theme::color(Theme::Color::textPrimary)
                                : Theme::color(Theme::Color::textSecondary));
        }
        g.setFont(Theme::font(Theme::fontSizeLg));
        g.drawText(item.label, bounds.withLeft(leftPad).withWidth(labelWidth),
                   juce::Justification::centredLeft);

        // Keybinding hint — left-aligned right after the label column, monospaced so glyphs line up.
        if (item.shortcut.isNotEmpty()) {
            g.setColour(Theme::color(Theme::Color::textKeyHint));
            g.setFont(Theme::fontMono(Theme::fontSizeKeyHint));
            g.drawText(item.shortcut, bounds.withLeft(leftPad + labelWidth).withTrimmedRight(leftPad),
                       juce::Justification::centredLeft);
        }
    }
}

void Sidebar::resized() {
    rebuild();
}

void Sidebar::mouseUp(const juce::MouseEvent& event) {
    auto pos = event.getPosition();
    for (int i = 0; i < (int)items.size(); ++i) {
        auto& item = items[i];
        if (!item.bounds.contains(pos)) continue;

        if (item.kind == Item::ViewToggle && onToggleView) {
            onToggleView(item.id);
            repaint();
            return;
        }

        if (item.kind == Item::SongEntry && onLoadSong) {
            onLoadSong(item.id);
            return;
        }

        if (item.kind == Item::ActionButton && item.id == "new_song" && onNewSong) {
            onNewSong();
            return;
        }

        if (item.kind == Item::TrackPresetEntry && onTrackPresetClicked) {
            onTrackPresetClicked(juce::String(item.id));
            return;
        }

        if (item.kind == Item::SongEntry && event.mods.isPopupMenu() && onDeleteSong) {
            auto songId = item.id;
            juce::PopupMenu menu;
            menu.addItem(1, "Delete " + UiTerms::docSingular);
            menu.showMenuAsync(juce::PopupMenu::Options(),
                [this, songId](int result) {
                    if (result == 1 && onDeleteSong)
                        onDeleteSong(songId);
                });
            return;
        }
    }
}

void Sidebar::mouseMove(const juce::MouseEvent& event) {
    int prev = hoveredIndex;
    hoveredIndex = -1;
    for (int i = 0; i < (int)items.size(); ++i) {
        if (items[i].kind != Item::SectionHeader && items[i].bounds.contains(event.getPosition())) {
            hoveredIndex = i;
            break;
        }
    }
    if (prev != hoveredIndex) repaint();
}

void Sidebar::mouseExit(const juce::MouseEvent&) {
    if (hoveredIndex != -1) {
        hoveredIndex = -1;
        repaint();
    }
}
