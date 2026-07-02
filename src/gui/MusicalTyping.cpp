#include "gui/MusicalTyping.h"
#include "engine/AudioEngine.h"

MusicalTyping::MusicalTyping() {
    setSize(panelWidth, panelHeight);
}

// Map key character to semitone offset from C (covers ~1.5 octaves)
// Bottom row: A=C, S=D, D=E, F=F, G=G, H=A, J=B, K=C+12, L=D+12, ;=E+12, '=F+12
// Middle row: W=C#, E=D#, T=F#, Y=G#, U=A#, O=C#+12, P=D#+12
int MusicalTyping::keyToSemitone(int keyChar) {
    switch (keyChar) {
        // White keys (bottom row)
        case 'a': return 0;   // C
        case 's': return 2;   // D
        case 'd': return 4;   // E
        case 'f': return 5;   // F
        case 'g': return 7;   // G
        case 'h': return 9;   // A
        case 'j': return 11;  // B
        case 'k': return 12;  // C+1
        case 'l': return 14;  // D+1
        case ';': return 16;  // E+1
        case '\'': return 17; // F+1

        // Black keys (middle row)
        case 'w': return 1;   // C#
        case 'e': return 3;   // D#
        case 't': return 6;   // F#
        case 'y': return 8;   // G#
        case 'u': return 10;  // A#
        case 'o': return 13;  // C#+1
        case 'p': return 15;  // D#+1

        default: return -1;
    }
}

bool MusicalTyping::isBlackKey(int semitone) {
    int s = semitone % 12;
    return s == 1 || s == 3 || s == 6 || s == 8 || s == 10;
}

void MusicalTyping::sendNoteOn(int note) {
    if (note < 0 || note > 127 || !audioEngine) return;
    heldNotes.insert(note);
    auto msg = juce::MidiMessage::noteOn(1, note, (juce::uint8)velocity);
    audioEngine->injectMidi(msg);
    repaint();
}

void MusicalTyping::sendNoteOff(int note) {
    if (note < 0 || note > 127 || !audioEngine) return;
    heldNotes.erase(note);
    if (!sustainOn) {
        auto msg = juce::MidiMessage::noteOff(1, note);
        audioEngine->injectMidi(msg);
    }
    repaint();
}

void MusicalTyping::allNotesOff() {
    if (!audioEngine) return;
    for (int n : heldNotes) {
        auto msg = juce::MidiMessage::noteOff(1, n);
        audioEngine->injectMidi(msg);
    }
    heldNotes.clear();
    heldKeys.clear();
    if (sustainOn) {
        sustainOn = false;
        audioEngine->injectMidi(juce::MidiMessage::controllerEvent(1, 64, 0));
    }
    repaint();
}

bool MusicalTyping::handleKey(const juce::KeyPress& key, bool isKeyDown) {
    int ch = key.getTextCharacter();

    // Note keys
    int semitone = keyToSemitone(ch);
    if (semitone >= 0) {
        int note = baseOctave * 12 + semitone;
        if (note > 127) note = 127;
        if (isKeyDown) {
            if (heldKeys.count(ch) == 0) {
                heldKeys.insert(ch);
                sendNoteOn(note);
            }
        } else {
            heldKeys.erase(ch);
            sendNoteOff(note);
        }
        return true;
    }

    // Only handle other controls on key-down
    if (!isKeyDown) return false;

    // Octave: Z=down, X=up
    if (ch == 'z') { baseOctave = std::max(0, baseOctave - 1); allNotesOff(); repaint(); return true; }
    if (ch == 'x') { baseOctave = std::min(8, baseOctave + 1); allNotesOff(); repaint(); return true; }

    // Velocity: C=down, V=up
    if (ch == 'c') { velocity = std::max(1, velocity - 10); repaint(); return true; }
    if (ch == 'v') { velocity = std::min(127, velocity + 10); repaint(); return true; }

    // Sustain (Tab)
    if (key.getKeyCode() == juce::KeyPress::tabKey) {
        sustainOn = !sustainOn;
        if (audioEngine)
            audioEngine->injectMidi(juce::MidiMessage::controllerEvent(1, 64, sustainOn ? 127 : 0));
        if (!sustainOn) {
            // Release any notes that aren't physically held
            std::set<int> toRelease;
            for (int n : heldNotes) {
                bool stillHeld = false;
                for (int k : heldKeys) {
                    int s = keyToSemitone(k);
                    if (s >= 0 && baseOctave * 12 + s == n) { stillHeld = true; break; }
                }
                if (!stillHeld) toRelease.insert(n);
            }
            for (int n : toRelease) {
                heldNotes.erase(n);
                if (audioEngine) audioEngine->injectMidi(juce::MidiMessage::noteOff(1, n));
            }
        }
        repaint();
        return true;
    }

    return false;
}

// --- Paint ---

void MusicalTyping::mouseDown(const juce::MouseEvent& event) {
    // Clickable controls mirror the keyboard handlers (Z/X octave, C/V velocity).
    auto p = event.position;
    if (ocDownRect.contains(p))  { baseOctave = std::max(0, baseOctave - 1); allNotesOff(); repaint(); return; }
    if (ocUpRect.contains(p))    { baseOctave = std::min(8, baseOctave + 1); allNotesOff(); repaint(); return; }
    if (velDownRect.contains(p)) { velocity = std::max(1, velocity - 10); repaint(); return; }
    if (velUpRect.contains(p))   { velocity = std::min(127, velocity + 10); repaint(); return; }

    // Otherwise drag the panel.
    dragger.startDraggingComponent(this, event);
}

void MusicalTyping::mouseDrag(const juce::MouseEvent& event) {
    dragger.dragComponent(this, event, nullptr);
}

static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

void MusicalTyping::paintKey(juce::Graphics& g, juce::Rectangle<float> bounds,
                              const juce::String& label, bool isBlack, bool isPressed) {
    auto col = isBlack ? juce::Colour(0xff1a1a1a) : juce::Colour(0xffe8e8e8);
    if (isPressed)
        col = juce::Colour(0xff4488cc);

    g.setColour(col);
    g.fillRoundedRectangle(bounds, 4.0f);
    g.setColour(juce::Colour(0xff333333));
    g.drawRoundedRectangle(bounds, 4.0f, 1.0f);

    // Label
    g.setColour(isBlack && !isPressed ? juce::Colour(0xffcccccc) : juce::Colour(0xff222222));
    g.setFont(Theme::fontMono(11.0f));
    g.drawText(label, bounds.reduced(2), juce::Justification::centredBottom);
}

void MusicalTyping::paint(juce::Graphics& g) {
    // Panel background
    auto area = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xee2a2a2a));
    g.fillRoundedRectangle(area, 8.0f);
    g.setColour(juce::Colour(0xff555555));
    g.drawRoundedRectangle(area, 8.0f, 1.5f);

    // Title
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(13.0f));
    g.drawText("Musical Typing", area.removeFromTop(24), juce::Justification::centred);

    // --- Controls row ---
    float y = 28;
    float cx = 12;

    // Sustain indicator
    g.setColour(sustainOn ? juce::Colour(0xff44aa44) : juce::Colour(0xff444444));
    g.fillRoundedRectangle(cx, y, 50, 24, 4.0f);
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(10.0f));
    g.drawText("sustain", cx, y, 50, 14, juce::Justification::centred);
    g.setFont(Theme::font(8.0f));
    g.setColour(Theme::color(Theme::Color::textDim));
    g.drawText("tab", cx, y + 12, 50, 12, juce::Justification::centred);

    // Octave
    cx = 80;
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(10.0f));
    g.drawText("Oct:", cx, y, 32, 24, juce::Justification::centredLeft);
    cx += 32;
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::fontMono(12.0f));
    g.drawText(juce::String("C") + juce::String(baseOctave), cx, y, 24, 24, juce::Justification::centredLeft);
    cx += 24;

    ocDownRect = juce::Rectangle<float>(cx, y + 1, 24, 22);
    ocUpRect = juce::Rectangle<float>(cx + 26, y + 1, 24, 22);
    g.setColour(juce::Colour(0xff8a7a2e));
    g.fillRoundedRectangle(ocDownRect, 3.0f);
    g.fillRoundedRectangle(ocUpRect, 3.0f);
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(11.0f));
    g.drawText("-", ocDownRect, juce::Justification::centred);
    g.drawText("+", ocUpRect, juce::Justification::centred);
    g.setFont(Theme::font(7.0f));
    g.setColour(Theme::color(Theme::Color::textDim));
    g.drawText("Z", ocDownRect.translated(0, 10), juce::Justification::centred);
    g.drawText("X", ocUpRect.translated(0, 10), juce::Justification::centred);

    cx += 70;
    g.setColour(Theme::color(Theme::Color::textSecondary));
    g.setFont(Theme::font(10.0f));
    g.drawText("Vel:", cx, y, 28, 24, juce::Justification::centredLeft);
    cx += 28;
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::fontMono(12.0f));
    g.drawText(juce::String(velocity), cx, y, 28, 24, juce::Justification::centredLeft);
    cx += 28;

    velDownRect = juce::Rectangle<float>(cx, y + 1, 24, 22);
    velUpRect = juce::Rectangle<float>(cx + 26, y + 1, 24, 22);
    g.setColour(juce::Colour(0xffaa6622));
    g.fillRoundedRectangle(velDownRect, 3.0f);
    g.fillRoundedRectangle(velUpRect, 3.0f);
    g.setColour(Theme::color(Theme::Color::textPrimary));
    g.setFont(Theme::font(11.0f));
    g.drawText("-", velDownRect, juce::Justification::centred);
    g.drawText("+", velUpRect, juce::Justification::centred);
    g.setFont(Theme::font(7.0f));
    g.setColour(Theme::color(Theme::Color::textDim));
    g.drawText("C", velDownRect.translated(0, 10), juce::Justification::centred);
    g.drawText("V", velUpRect.translated(0, 10), juce::Justification::centred);

    // Cmd+K hint
    g.setColour(Theme::color(Theme::Color::textDim));
    g.setFont(Theme::font(9.0f));
    g.drawText(juce::CharPointer_UTF8("\xe2\x8c\x98K"), getWidth() - 30, 28, 22, 16, juce::Justification::centredRight);

    // --- Piano keys ---
    float keyW = 38;
    float blackW = 30;
    float whiteH = 68;
    float blackH = 44;
    float keyGap = 2;
    float pianoY = 56;
    float pianoX = 16;

    // White key info: { semitone, label }
    struct WhiteKeyInfo { int semitone; const char* label; };
    WhiteKeyInfo whites[] = {
        {0,"A"}, {2,"S"}, {4,"D"}, {5,"F"}, {7,"G"}, {9,"H"}, {11,"J"},
        {12,"K"}, {14,"L"}, {16,";"}, {17,"'"}
    };

    // Draw white keys first
    for (int i = 0; i < 11; ++i) {
        float kx = pianoX + i * (keyW + keyGap);
        int note = baseOctave * 12 + whites[i].semitone;
        bool pressed = heldNotes.count(note) > 0;
        auto bounds = juce::Rectangle<float>(kx, pianoY, keyW, whiteH);
        paintKey(g, bounds, whites[i].label, false, pressed);
    }

    // Black key info: { whiteIndex (positioned after this white), semitone, label }
    struct BlackKeyInfo { int afterWhite; int semitone; const char* label; };
    BlackKeyInfo blacks[] = {
        {0, 1, "W"}, {1, 3, "E"},
        {3, 6, "T"}, {4, 8, "Y"}, {5, 10, "U"},
        {7, 13, "O"}, {8, 15, "P"}
    };

    for (auto& bk : blacks) {
        float kx = pianoX + bk.afterWhite * (keyW + keyGap) + keyW - blackW / 2 + keyGap / 2;
        int note = baseOctave * 12 + bk.semitone;
        bool pressed = heldNotes.count(note) > 0;
        auto bounds = juce::Rectangle<float>(kx, pianoY, blackW, blackH);
        paintKey(g, bounds, bk.label, true, pressed);
    }

    // Note name labels below white keys
    g.setFont(Theme::font(9.0f));
    g.setColour(Theme::color(Theme::Color::textDim));
    for (int i = 0; i < 11; ++i) {
        float kx = pianoX + i * (keyW + keyGap);
        int s = whites[i].semitone % 12;
        g.drawText(noteNames[s], kx, pianoY + whiteH + 2, keyW, 14, juce::Justification::centred);
    }
}
