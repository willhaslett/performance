#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <set>
#include "gui/Theme.h"

class AudioEngine;

// Musical Typing — on-screen keyboard mapped to computer keys.
// Sends MIDI directly to the audio engine. Cmd+K toggles visibility.
// When active, intercepts all keyboard input except Cmd+K and Escape.

class MusicalTyping : public juce::Component {
public:
    MusicalTyping();

    void setAudioEngine(AudioEngine* engine) { audioEngine = engine; }

    // Returns true if this component handled the key (blocks further processing)
    bool handleKey(const juce::KeyPress& key, bool isKeyDown);

    void paint(juce::Graphics& g) override;

private:
    AudioEngine* audioEngine = nullptr;

    int baseOctave = 3;      // C3 default
    int velocity = 98;
    bool sustainOn = false;

    // Currently held notes (for visual feedback and noteOff tracking)
    std::set<int> heldNotes;    // MIDI note numbers currently held
    std::set<int> heldKeys;     // key codes currently held (for key-up tracking)

    // Key → semitone offset from C (within the octave span)
    static int keyToSemitone(int keyChar);
    // Key → true if it's a black key
    static bool isBlackKey(int semitone);

    void sendNoteOn(int note);
    void sendNoteOff(int note);
public:
    void allNotesOff();
private:

    // Paint helpers
    void paintKey(juce::Graphics& g, juce::Rectangle<float> bounds,
                  const juce::String& label, bool isBlack, bool isPressed);

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    juce::ComponentDragger dragger;

    static constexpr int panelWidth = 470;
    static constexpr int panelHeight = 150;
};
