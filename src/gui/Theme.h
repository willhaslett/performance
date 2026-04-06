#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace Theme {
    // Colors
    namespace Color {
        constexpr uint32_t bgApp        = 0xff121212;
        constexpr uint32_t bgPanel      = 0xff1a1a1a;
        constexpr uint32_t bgTrack      = 0xff1e1e1e;
        constexpr uint32_t bgSlot       = 0xff2a2a2a;
        constexpr uint32_t bgSlotHover  = 0xff353535;
        constexpr uint32_t bgHeader     = 0xff2a6aaa;
        constexpr uint32_t bgHeaderOff  = 0xff2a2a2a;
        constexpr uint32_t bgHeaderBus  = 0xff4a3a6a;
        constexpr uint32_t border       = 0xff3a3a3a;
        constexpr uint32_t textPrimary  = 0xffcccccc;
        constexpr uint32_t textSecondary= 0xff888888;
        constexpr uint32_t textDim      = 0xff555555;
        constexpr uint32_t textWhite    = 0xffffffff;
        constexpr uint32_t accent       = 0xff2a6aaa;
        constexpr uint32_t midiActive   = 0xff44cc44;
        constexpr uint32_t instrument   = 0xffddaa44;
        constexpr uint32_t effect       = 0xff88aacc;
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

    // Font
    constexpr float fontSize        = 13.0f;
    constexpr float fontSizeSm      = 12.0f;
    constexpr float fontSizeXs      = 11.0f;

    // Helpers
    inline juce::Colour color(uint32_t c) { return juce::Colour(c); }

    inline juce::Font font(float size = fontSize) {
        return juce::Font(juce::FontOptions(size));
    }

    inline juce::Font fontMono(float size = fontSize) {
        return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), size, 0));
    }
}
