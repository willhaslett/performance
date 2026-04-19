#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace Theme {

    // ── Colors ────────────────────────────────────────────────
    // NOTE: All values below are runtime-mutable (inline non-const) so a
    // theme loader can override them at startup. Call sites read them as
    // plain values; behavior is identical to the old constexpr layout.
    namespace Color {

        // Surfaces — darkest to lightest
        inline uint32_t bgApp           = 0xff161616;  // app background
        inline uint32_t bgPanel         = 0xff161616;  // panels, sidebars, pane headers
        inline uint32_t bgSurface       = 0xff1e1e1e;  // track lanes, mixer strips
        inline uint32_t bgSurfaceRaised = 0xff333333;  // region fills
        inline uint32_t bgSlot          = 0xff2a2a2a;  // passive inset surfaces
        inline uint32_t bgControl       = 0xff2d2d2d;  // interactive control base
        inline uint32_t bgControlHover  = 0xff333333;  // interactive control hover
        inline uint32_t bgListActive    = 0xff333333;  // active/selected entry in a list view (sidebar, palettes)
        inline uint32_t bgSelection     = 0xff262626;  // selected track row highlight
        inline uint32_t bgStripe        = 0xff343434;  // alternate-row zebra stripe in list panes
        inline uint32_t bgOverlay       = 0xff2a2a2a;  // modal/overlay/popup backdrop
        inline uint32_t bgDisabled      = 0xff252525;  // disabled strip/header
        inline uint32_t bgRecessed      = 0xff121212;  // meter grooves, fader tracks
        inline uint32_t overlayDim      = 0x40000000;  // semi-transparent dim

        // Borders
        inline uint32_t border          = 0xff444444;
        inline uint32_t borderSubtle    = 0xff333333;

        // Text
        inline uint32_t textPrimary     = 0xffd8d8d8;
        inline uint32_t textSecondary   = 0xffaaaaaa;
        inline uint32_t textDim         = 0xff666666;
        inline uint32_t textKeyHint     = 0xff909090;  // keybinding hints — between dim and secondary
        inline uint32_t textOnColor     = 0xffffffff;
        inline uint32_t controlHandle   = 0xffffffff;

        // Accent
        inline uint32_t accent          = 0xff2a6aaa;
        inline uint32_t accentDim       = 0xff1a4a6a;

        // Semantic / status
        inline uint32_t activityOn      = 0xff44cc44;
        inline uint32_t activityOff     = 0xff1a3a1a;
        inline uint32_t statusError     = 0xff994444;

        // Transport
        inline uint32_t transportPlay      = 0xff2a6a2a;
        inline uint32_t transportRec       = 0xff6a2a2a;
        inline uint32_t transportRecDot    = 0xffcc4444;
        inline uint32_t transportCycle     = 0xff8a8a40;
        inline uint32_t transportCycleOff  = 0xff505050;
        inline uint32_t playhead           = 0xccffffff;

        // Meter
        inline uint32_t meterGreen      = 0xff44cc44;
        inline uint32_t meterAmber      = 0xffccaa44;
        inline uint32_t meterRed        = 0xffcc4444;
        inline uint32_t sendPeak        = 0xff1a6e1a;

        // Track / channel type accents — subtle hue to distinguish entity kinds
        // at a glance in the mixer (top stripe) and ProducePane (left edge).
        inline uint32_t typeInstrument  = 0xffaa8838;  // muted amber
        inline uint32_t typeAudio       = 0xff5080a0;  // muted teal
        inline uint32_t typeBus         = 0xffa86078;  // muted rose — deliberately warm to separate from typeAudio under CVD
        inline uint32_t typeOutput      = 0xff161616;  // matches bgApp — renders invisibly; absence of color is the signal

        // Track pills
        inline uint32_t pillMute        = 0xff8a7a3a;
        inline uint32_t pillSolo        = 0xff3a6a8a;
        inline uint32_t pillArm         = 0xff8a4040;
        inline uint32_t pillInput       = 0xff8a4040;
        inline uint32_t pillTextOff     = 0xff888888;

        // Slot type tints
        inline uint32_t slotInstrument  = 0xffddaa44;
        inline uint32_t slotEffect      = 0xff88aacc;

        // LCD
        inline uint32_t lcdDigit        = 0xffd0d0d0;

        // Track color palette
        inline std::array<uint32_t, 8> trackColors = {
            0xff2a5a3a, 0xff3a3a5a, 0xff4a3a2a, 0xff2a4a5a,
            0xff5a2a3a, 0xff3a5a2a, 0xff4a2a5a, 0xff5a4a2a,
        };
        inline int trackColorCount = 8;

        // Device color palette
        inline std::array<uint32_t, 8> deviceColors = {
            0xff2a4a6a, 0xff4a3a5a, 0xff3a5a4a, 0xff5a3a3a,
            0xff3a4a3a, 0xff5a4a3a, 0xff3a3a5a, 0xff4a5a3a,
        };
        inline int deviceColorCount = 8;

        // Chat
        inline uint32_t chatUser        = 0xff2a4a6a;
        inline uint32_t chatAssistant   = 0xff2a2a2a;
        inline uint32_t chatTool        = 0xff1a2a1a;
        inline uint32_t chatError       = 0xff3a1a1a;
        inline uint32_t chatInput       = 0xff252525;
    }

    // ── Typography ────────────────────────────────────────────
    inline float fontSizeTitle    = 16.0f;
    inline float fontSizeKeyHint  = 15.0f;  // keybinding hints — slightly above body
    inline float fontSizeLg       = 14.0f;
    inline float fontSizeMd       = 13.0f;
    inline float fontSizeSm       = 12.0f;
    inline float fontSizeXs       = 9.0f;
    inline float fontSizePill     = 13.0f;
    inline float fontSizeMeter    = 10.0f;

    // LCD display (ProducePane transport)
    inline float fontSizeLcdLg    = 29.0f;
    inline float fontSizeLcdMd    = 23.0f;
    inline float fontSizeLcdLabel = 8.0f;

    // ── Spacing ───────────────────────────────────────────────
    inline int   spacingXs   = 2;
    inline int   spacingS    = 4;
    inline int   spacingM    = 8;
    inline int   spacingL    = 12;
    inline int   spacingXl   = 16;

    // ── Component dimensions ──────────────────────────────────
    inline int   headerHeight    = 28;
    inline int   toolbarHeight   = 32;
    inline int   slotHeight      = 24;
    inline int   slotGap         = 4;
    inline int   slotPadding     = 4;
    inline int   trackPadding    = 12;
    inline int   trackStripWidth = 160;
    inline int   iconSize        = 14;

    // Pill buttons
    inline int   pillSize        = 20;
    inline float pillRadius      = 4.0f;
    inline int   pillGap         = 5;
    inline int   pillGroupGap    = 8;
    inline int   pillNameGap     = 8;

    // Corners
    inline float cornerRadius    = 6.0f;
    inline float cornerRadiusSm  = 4.0f;
    inline float cornerRadiusXs  = 3.0f;

    // Activity indicator
    inline float activityDotSize = 6.0f;

    // Chat
    inline float chatBubbleRadius = 12.0f;
    inline int   chatBubblePad   = 12;
    inline int   chatGap         = 8;
    inline int   chatInputHeight = 40;

    // ── Helpers ───────────────────────────────────────────────
    inline juce::Colour color(uint32_t c) { return juce::Colour(c); }

    inline juce::Font font(float size = 14.0f) {
        return juce::Font(juce::FontOptions(size));
    }

    inline juce::Font fontMono(float size = 14.0f) {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, 0));
    }

    // ── Theme source / registry ─────────────────────────────────
    struct ThemeEntry {
        juce::String id;           // stable identifier ("minimal_dark")
        juce::String name;         // display name ("Minimal Dark")
        juce::String description;
        enum class Source { Factory, User } source = Source::Factory;
        juce::File path;           // populated for User themes, empty for Factory
    };

    std::vector<ThemeEntry> availableThemes();       // factory + user, user wins on id
    bool loadThemeById(const juce::String& id);      // resolves id → factory or user file

    // Lower-level APIs
    bool loadTheme(const juce::File& file);          // loads a specific JSON file
    bool saveTheme(const juce::File& file, const juce::String& name, const juce::String& description);
    void loadDefaultTheme();                          // restores compile-time defaults
    juce::String currentThemeName();
    void ensureDefaultThemeFile();                    // creates user themes dir if missing
}
