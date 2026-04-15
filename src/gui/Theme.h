#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme {
    // Colors
    namespace Color {
        constexpr uint32_t bgApp        = 0xff0e0e0e;
        constexpr uint32_t bgPanel      = 0xff161616;
        constexpr uint32_t bgTrack      = 0xff1c1c1c;
        constexpr uint32_t bgSlot       = 0xff303030;
        constexpr uint32_t bgSlotHover  = 0xff3c3c3c;
        constexpr uint32_t bgHeader     = 0xff1a3a55;  // muted blue
        constexpr uint32_t bgHeaderOff  = 0xff252525;
        constexpr uint32_t bgHeaderBus  = 0xff2a2245;  // muted purple
        constexpr uint32_t bgHeaderOut  = 0xff223022;  // muted green
        constexpr uint32_t border       = 0xff444444;
        constexpr uint32_t textPrimary  = 0xffd8d8d8;
        constexpr uint32_t textSecondary= 0xff999999;
        constexpr uint32_t textDim      = 0xff666666;
        constexpr uint32_t textWhite    = 0xffffffff;
        constexpr uint32_t accent       = 0xff2a6aaa;
        constexpr uint32_t lcdDigit     = 0xffd0d0d0;
        constexpr uint32_t midiActive   = 0xff44cc44;
        constexpr uint32_t instrument   = 0xffddaa44;
        constexpr uint32_t effect       = 0xff88aacc;

        // Device color palette — muted tints for MIDI device rows
        constexpr uint32_t deviceColors[] = {
            0xff2a4a6a,  // steel blue
            0xff4a3a5a,  // plum
            0xff3a5a4a,  // sage
            0xff5a3a3a,  // rust
            0xff3a4a3a,  // forest
            0xff5a4a3a,  // tan
            0xff3a3a5a,  // slate
            0xff4a5a3a,  // moss
        };
        constexpr int deviceColorCount = 8;

        // Track color palette — assigned to new tracks in rotation
        constexpr uint32_t trackColors[] = {
            0xff2a5a3a,  // green
            0xff3a3a5a,  // purple
            0xff4a3a2a,  // brown
            0xff2a4a5a,  // teal
            0xff5a2a3a,  // rose
            0xff3a5a2a,  // olive
            0xff4a2a5a,  // violet
            0xff5a4a2a,  // amber
        };
        constexpr int trackColorCount = 8;

        // Chat
        constexpr uint32_t chatBgUser  = 0xff2a4a6a;
        constexpr uint32_t chatBgAsst  = 0xff2a2a2a;
        constexpr uint32_t chatBgTool  = 0xff1a2a1a;
        constexpr uint32_t chatBgError = 0xff3a1a1a;
        constexpr uint32_t chatInput   = 0xff252525;

        // Track header pill button colors
        constexpr uint32_t pillMuteActive   = 0xff8a7a3a;  // muted gold
        constexpr uint32_t pillSoloActive   = 0xff3a6a8a;  // muted steel
        constexpr uint32_t pillArmActive    = 0xff8a4040;  // muted red
        constexpr uint32_t pillInputActive  = 0xff8a4040;  // muted red
        constexpr uint32_t pillInactive     = 0xff555555;  // dim outline
        constexpr uint32_t pillTextInactive = 0xff888888;  // dim but readable
        constexpr uint32_t pillOffFill     = 0xff333333;  // dark filled background for inactive
    }

    // Dimensions
    constexpr float cornerRadius    = 6.0f;
    constexpr float cornerRadiusSm  = 4.0f;
    constexpr int   toolbarHeight   = 32;
    constexpr int   trackStripWidth = 160;
    constexpr int   slotHeight      = 24;
    constexpr int   slotGap         = 4;
    constexpr int   slotPadding     = 4;
    constexpr int   trackPadding    = 12;
    constexpr int   headerHeight    = 32;
    constexpr float chatBubbleRadius = 12.0f;
    constexpr int   chatBubblePad   = 12;
    constexpr int   chatGap         = 8;
    constexpr int   chatInputHeight = 40;

    // Pill button dimensions
    constexpr int   pillSize        = 20;
    constexpr float pillRadius      = 4.0f;
    constexpr float fontSizePill    = 13.0f;
    constexpr int   pillGap         = 5;   // between pills in a group
    constexpr int   pillGroupGap    = 8;   // between pill groups
    constexpr int   pillNameGap     = 8;   // after last pill, before name

    // Font
    constexpr float fontSize        = 14.0f;
    constexpr float fontSizeSm      = 13.0f;
    constexpr float fontSizeXs      = 12.0f;

    // Helpers
    inline juce::Colour color(uint32_t c) { return juce::Colour(c); }

    inline juce::Font font(float size = fontSize) {
        return juce::Font(juce::FontOptions(size));
    }

    inline juce::Font fontMono(float size = fontSize) {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, 0));
    }
}
