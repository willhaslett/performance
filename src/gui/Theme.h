#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme {

    // ── Colors ────────────────────────────────────────────────
    namespace Color {

        // Surfaces — darkest to lightest
        constexpr uint32_t bgApp           = 0xff161616;  // app background
        constexpr uint32_t bgPanel         = 0xff161616;  // panels, sidebars, pane headers (same as bgApp)
        constexpr uint32_t bgSurface       = 0xff1e1e1e;  // track lanes, mixer strips
        constexpr uint32_t bgSurfaceRaised = 0xff333333;  // region fills — one step brighter than bgSurface
        constexpr uint32_t bgSlot          = 0xff2a2a2a;  // passive inset surfaces: meter grooves, fader/slider troughs
        constexpr uint32_t bgControl       = 0xff2d2d2d;  // interactive control base: pills, plugin slots, LCD, pickers
        constexpr uint32_t bgControlHover  = 0xff333333;  // interactive control hover state
        constexpr uint32_t bgSelection     = 0xff262626;  // selected track row highlight — between bgSurface and bgControl so pill backgrounds contrast
        constexpr uint32_t bgOverlay       = 0xff2a2a2a;  // modal/overlay/popup backdrop
        constexpr uint32_t bgDisabled      = 0xff252525;  // disabled strip/header
        constexpr uint32_t bgRecessed      = 0xff121212;  // meter grooves, fader tracks
        constexpr uint32_t overlayDim      = 0x40000000;  // semi-transparent dim (drag source, etc.)

        // Borders
        constexpr uint32_t border         = 0xff444444;  // standard divider lines
        constexpr uint32_t borderSubtle   = 0xff333333;  // lighter separators within panels

        // Text
        constexpr uint32_t textPrimary    = 0xffd8d8d8;  // main readable text
        constexpr uint32_t textSecondary  = 0xffaaaaaa;  // labels, descriptions (was 0xff999999)
        constexpr uint32_t textDim        = 0xff666666;  // disabled, placeholder
        constexpr uint32_t textOnColor    = 0xffffffff;  // text on colored backgrounds
        constexpr uint32_t controlHandle  = 0xffffffff;  // fader handles, prominent grabbable controls

        // Accent
        constexpr uint32_t accent         = 0xff2a6aaa;  // selection, focus, links
        constexpr uint32_t accentDim      = 0xff1a4a6a;  // subtle accent

        // Semantic / status
        constexpr uint32_t activityOn     = 0xff44cc44;  // MIDI activity, connected
        constexpr uint32_t activityOff    = 0xff1a3a1a;  // no signal
        constexpr uint32_t statusError    = 0xff994444;  // load failures

        // Transport
        constexpr uint32_t transportPlay      = 0xff2a6a2a;  // play button active
        constexpr uint32_t transportRec       = 0xff6a2a2a;  // record mode active
        constexpr uint32_t transportRecDot    = 0xffcc4444;  // record dot
        constexpr uint32_t transportCycle     = 0xff8a8a40;  // cycle active
        constexpr uint32_t transportCycleOff  = 0xff505050;  // cycle inactive
        constexpr uint32_t playhead           = 0xccffffff;  // playhead vertical line

        // Meter
        constexpr uint32_t meterGreen     = 0xff44cc44;  // safe level
        constexpr uint32_t meterAmber     = 0xffccaa44;  // warning zone
        constexpr uint32_t meterRed       = 0xffcc4444;  // clipping zone
        constexpr uint32_t sendPeak       = 0xff1a6e1a;  // send knob peak-activity glow (darker green)

        // Track pills
        constexpr uint32_t pillMute       = 0xff8a7a3a;  // M active — muted gold
        constexpr uint32_t pillSolo       = 0xff3a6a8a;  // S active — muted steel
        constexpr uint32_t pillArm        = 0xff8a4040;  // R active — muted red
        constexpr uint32_t pillInput      = 0xff8a4040;  // I active — muted red
        constexpr uint32_t pillTextOff    = 0xff888888;  // inactive text

        // Slot type tints
        constexpr uint32_t slotInstrument = 0xffddaa44;  // instrument name
        constexpr uint32_t slotEffect     = 0xff88aacc;  // effect name

        // LCD
        constexpr uint32_t lcdDigit       = 0xffd0d0d0;

        // Track color palette
        constexpr uint32_t trackColors[] = {
            0xff2a5a3a, 0xff3a3a5a, 0xff4a3a2a, 0xff2a4a5a,
            0xff5a2a3a, 0xff3a5a2a, 0xff4a2a5a, 0xff5a4a2a,
        };
        constexpr int trackColorCount = 8;

        // Device color palette
        constexpr uint32_t deviceColors[] = {
            0xff2a4a6a, 0xff4a3a5a, 0xff3a5a4a, 0xff5a3a3a,
            0xff3a4a3a, 0xff5a4a3a, 0xff3a3a5a, 0xff4a5a3a,
        };
        constexpr int deviceColorCount = 8;

        // Chat
        constexpr uint32_t chatUser       = 0xff2a4a6a;
        constexpr uint32_t chatAssistant  = 0xff2a2a2a;
        constexpr uint32_t chatTool       = 0xff1a2a1a;
        constexpr uint32_t chatError      = 0xff3a1a1a;
        constexpr uint32_t chatInput      = 0xff252525;
    }

    // ── Typography ────────────────────────────────────────────
    constexpr float fontSizeTitle    = 16.0f;  // pane headers, section titles
    constexpr float fontSizeLg       = 14.0f;  // track names, primary content
    constexpr float fontSizeMd       = 13.0f;  // slot labels, secondary content
    constexpr float fontSizeSm       = 12.0f;  // badges, hints, type indicators
    constexpr float fontSizeXs       = 9.0f;   // very small labels (metronome, region counters)
    constexpr float fontSizePill     = 13.0f;  // pill button labels
    constexpr float fontSizeMeter    = 10.0f;  // dB tick labels

    // LCD display (ProducePane transport)
    constexpr float fontSizeLcdLg    = 29.0f;
    constexpr float fontSizeLcdMd    = 23.0f;
    constexpr float fontSizeLcdLabel = 8.0f;

    // Legacy aliases (to be removed as files migrate)
    constexpr float fontSize         = fontSizeLg;

    // ── Spacing ───────────────────────────────────────────────
    constexpr int   spacingXs   = 2;
    constexpr int   spacingS    = 4;
    constexpr int   spacingM    = 8;
    constexpr int   spacingL    = 12;
    constexpr int   spacingXl   = 16;

    // ── Component dimensions ──────────────────────────────────
    constexpr int   headerHeight    = 28;  // name block height: mixer strip headers, ProducePane track row 1
    constexpr int   toolbarHeight   = 32;
    constexpr int   slotHeight      = 24;
    constexpr int   slotGap         = 4;
    constexpr int   slotPadding     = 4;
    constexpr int   trackPadding    = 12;
    constexpr int   trackStripWidth = 160;
    constexpr int   iconSize        = 14;

    // Pill buttons
    constexpr int   pillSize        = 20;
    constexpr float pillRadius      = 4.0f;
    constexpr int   pillGap         = 5;
    constexpr int   pillGroupGap    = 8;
    constexpr int   pillNameGap     = 8;

    // Corners
    constexpr float cornerRadius    = 6.0f;
    constexpr float cornerRadiusSm  = 4.0f;

    // Chat
    constexpr float chatBubbleRadius = 12.0f;
    constexpr int   chatBubblePad   = 12;
    constexpr int   chatGap         = 8;
    constexpr int   chatInputHeight = 40;

    // ── Helpers ───────────────────────────────────────────────
    inline juce::Colour color(uint32_t c) { return juce::Colour(c); }

    inline juce::Font font(float size = fontSizeLg) {
        return juce::Font(juce::FontOptions(size));
    }

    inline juce::Font fontMono(float size = fontSizeLg) {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, 0));
    }
}
