#include "composer/ABCWriter.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace {

// ---------------- Pitch encoding -----------------------------------------

// MIDI pitch → ABC token. Returns "" for out-of-range.
//
// ABC pitch convention:
//   C,, = MIDI 36 (C2)
//   C,  = MIDI 48 (C3)
//   C   = MIDI 60 (C4, middle C)
//   c   = MIDI 72 (C5)
//   c'  = MIDI 84 (C6)
//   c'' = MIDI 96 (C7)
//
// Accidentals always emitted explicitly (^C / _C); never relying on
// bar-implicit naturals. For chromatic pitches we prefer sharps, since
// ABC's `^` is more common in the corpora than `_`.
const char* kSharpLetters[12] = {
    "C", "^C", "D", "^D", "E", "F", "^F", "G", "^G", "A", "^A", "B"
};

std::string encodePitch(int midi) {
    if (midi < 0 || midi > 127) return "";

    int pc      = midi % 12;
    int octave  = midi / 12 - 1;     // MIDI octave: 60 = C4 → octave=4

    const char* letter = kSharpLetters[pc];
    bool sharp         = (letter[0] == '^');
    char baseUpper     = sharp ? letter[1] : letter[0];

    // Octaves 4 (C4..B4) → uppercase no marker.
    // Octaves >4 → lowercase, then ' for each octave above 5.
    // Octaves <4 → uppercase, then , for each octave below 4.
    std::string out;
    if (sharp) out.push_back('^');

    if (octave >= 5) {
        char baseLower = static_cast<char>(baseUpper - 'A' + 'a');
        out.push_back(baseLower);
        for (int i = 5; i < octave; ++i) out.push_back('\'');
    } else if (octave == 4) {
        out.push_back(baseUpper);
    } else {
        out.push_back(baseUpper);
        for (int i = 4; i > octave; --i) out.push_back(',');
    }
    return out;
}

// ---------------- Drum letters -------------------------------------------
//
// Custom drum-letter macros emitted into a `%%MIDI drummap` directive.
// Letters chosen for mnemonic clarity + token compression.
//
//    B = kick  (Bass)
//    S = snare
//    H = hhc   (Hihat closed)
//    O = hho   (Open hat)
//    R = ride
//    C = crash (Cymbal)
//    T = ltom  (Tom low)
//    M = mtom  (Mid)
//    A = htom  (high tom — A for Apex)
struct DrumMapping { char letter; int midi; const char* name; };
const DrumMapping kDrumMappings[] = {
    {'B', 36, "kick"},
    {'S', 38, "snare"},
    {'H', 42, "hhc"},
    {'O', 46, "hho"},
    {'R', 51, "ride"},
    {'C', 49, "crash"},
    {'T', 45, "ltom"},
    {'M', 47, "mtom"},
    {'A', 50, "htom"},
};

const char* drumLetterForPitch(int midi) {
    static char single[2] = {0, 0};
    for (auto& d : kDrumMappings) {
        if (d.midi == midi) { single[0] = d.letter; return single; }
    }
    return nullptr;
}

// ---------------- Duration encoding ---------------------------------------
//
// L: is locked to 1/8 (one beat = 0.5 in L-units → 8 ticks of 1/16
// would be needed for full sixteenth-note resolution). We emit
// durations as multiples / fractions of L:.
//
// duration in beats → ABC suffix. With L:1/8, one eighth = 0.5 beats.
//
//   beats * 2 = eighths = the "raw" multiplier we want to emit
//
// Examples:
//   0.5 beats (eighth)        → "" (bare letter)
//   1.0 beats (quarter)       → "2"
//   1.5 beats (dotted quarter)→ "3"
//   2.0 beats (half)          → "4"
//   4.0 beats (whole)         → "8"
//   0.25 beats (sixteenth)    → "/2"
//   0.75 beats (dotted 8th)   → "3/2"
//   0.125 beats (32nd)        → "/4"
std::string encodeDuration(double beats) {
    // Convert to multiples of L=1/8 (one beat = 2 eighths).
    double units = beats * 2.0;
    // Snap to nearest 1/16 of an eighth (= 1/128 note) to avoid float ugliness.
    constexpr double kSnap = 16.0;
    long long num = static_cast<long long>(std::llround(units * kSnap));
    long long den = static_cast<long long>(kSnap);
    // Reduce.
    auto gcd = [](long long a, long long b) -> long long {
        while (b) { a %= b; std::swap(a, b); }
        return a < 0 ? -a : a;
    };
    long long g = gcd(num, den);
    if (g > 0) { num /= g; den /= g; }

    if (num <= 0) return "";
    if (den == 1) {
        if (num == 1) return "";
        return std::to_string(num);
    }
    if (num == 1) return "/" + std::to_string(den);
    return std::to_string(num) + "/" + std::to_string(den);
}

// ---------------- Chord grouping -----------------------------------------

struct GroupedNote {
    double startBeat;
    double durationBeats;
    std::vector<int> pitches;   // 1+ — chord if >1
};

std::vector<GroupedNote> groupChords(const std::vector<ABCWriteInput::Note>& notes) {
    std::vector<ABCWriteInput::Note> sorted = notes;
    std::sort(sorted.begin(), sorted.end(),
        [](const ABCWriteInput::Note& a, const ABCWriteInput::Note& b) {
            if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
            if (a.durationBeats != b.durationBeats) return a.durationBeats < b.durationBeats;
            return a.pitch < b.pitch;
        });

    std::vector<GroupedNote> out;
    constexpr double kEps = 1e-6;
    for (auto& n : sorted) {
        if (!out.empty()
            && std::abs(out.back().startBeat - n.startBeat) < kEps
            && std::abs(out.back().durationBeats - n.durationBeats) < kEps) {
            out.back().pitches.push_back(n.pitch);
        } else {
            out.push_back({n.startBeat, n.durationBeats, {n.pitch}});
        }
    }
    return out;
}

// ---------------- Bar splitting ------------------------------------------
//
// If a grouped note crosses a bar boundary, split into head + tail with
// tie. The output stream's per-note units are still GroupedNote, but
// each emit knows whether to append a `-` for tie-out.

struct BarPiece {
    double startBeat;        // within this piece's bar
    double durationBeats;
    std::vector<int> pitches;
    bool tiesOut = false;    // emit trailing `-`
    bool isRest = false;
};

struct BarContent {
    int barNumber = 0;       // 1-based
    double barStartBeat = 0.0;
    double barLengthBeats = 0.0;
    std::vector<BarPiece> pieces;
};

// Splits notes + rest fillers into per-bar pieces.
std::vector<BarContent> splitIntoBars(const std::vector<GroupedNote>& notes,
                                       int barCount,
                                       double beatsPerBar) {
    std::vector<BarContent> bars(barCount);
    for (int i = 0; i < barCount; ++i) {
        bars[i].barNumber       = i + 1;
        bars[i].barStartBeat    = i * beatsPerBar;
        bars[i].barLengthBeats  = beatsPerBar;
    }

    constexpr double kEps = 1e-6;

    // Walk notes in time order, tracking the "cursor" (next free beat).
    double cursor = 0.0;
    for (auto& n : notes) {
        double noteStart = n.startBeat;
        double noteEnd   = n.startBeat + n.durationBeats;
        if (noteEnd <= cursor + kEps) continue;          // already passed
        if (noteStart < cursor - kEps) noteStart = cursor; // clamp overlap

        // Fill rest from cursor → noteStart.
        double restEnd = noteStart;
        while (cursor + kEps < restEnd) {
            int barIdx = static_cast<int>(cursor / beatsPerBar + kEps);
            if (barIdx >= barCount) break;
            double barEnd = (barIdx + 1) * beatsPerBar;
            double pieceEnd = std::min(restEnd, barEnd);
            BarPiece p;
            p.startBeat     = cursor - barIdx * beatsPerBar;
            p.durationBeats = pieceEnd - cursor;
            p.isRest        = true;
            bars[barIdx].pieces.push_back(p);
            cursor = pieceEnd;
        }

        // Emit the note, splitting on bar boundaries with ties.
        double remStart = noteStart;
        while (cursor + kEps < noteEnd && remStart + kEps < noteEnd) {
            int barIdx = static_cast<int>(remStart / beatsPerBar + kEps);
            if (barIdx >= barCount) break;
            double barEnd = (barIdx + 1) * beatsPerBar;
            double pieceEnd = std::min(noteEnd, barEnd);
            BarPiece p;
            p.startBeat     = remStart - barIdx * beatsPerBar;
            p.durationBeats = pieceEnd - remStart;
            p.pitches       = n.pitches;
            p.tiesOut       = (pieceEnd < noteEnd - kEps);
            bars[barIdx].pieces.push_back(p);
            remStart = pieceEnd;
            cursor   = pieceEnd;
        }
    }

    // Trailing rests → fill to end.
    double total = barCount * beatsPerBar;
    while (cursor + kEps < total) {
        int barIdx = static_cast<int>(cursor / beatsPerBar + kEps);
        if (barIdx >= barCount) break;
        double barEnd = (barIdx + 1) * beatsPerBar;
        BarPiece p;
        p.startBeat     = cursor - barIdx * beatsPerBar;
        p.durationBeats = barEnd - cursor;
        p.isRest        = true;
        bars[barIdx].pieces.push_back(p);
        cursor = barEnd;
    }

    return bars;
}

// ---------------- Voice body emission ------------------------------------

std::string emitPiece(const BarPiece& p, bool isDrums) {
    if (p.isRest) {
        return "z" + encodeDuration(p.durationBeats);
    }
    std::string body;
    if (p.pitches.size() == 1) {
        if (isDrums) {
            const char* dl = drumLetterForPitch(p.pitches[0]);
            body = dl ? std::string(dl) : encodePitch(p.pitches[0]);
        } else {
            body = encodePitch(p.pitches[0]);
        }
    } else {
        body = "[";
        for (auto pi : p.pitches) {
            if (isDrums) {
                const char* dl = drumLetterForPitch(pi);
                body += dl ? std::string(dl) : encodePitch(pi);
            } else {
                body += encodePitch(pi);
            }
        }
        body += "]";
    }
    body += encodeDuration(p.durationBeats);
    if (p.tiesOut) body += "-";
    return body;
}

std::string emitVoiceBody(const std::vector<BarContent>& bars, bool isDrums) {
    constexpr int kBarsPerLine = 4;
    std::string out;
    for (size_t i = 0; i < bars.size(); ++i) {
        if (i > 0 && (i % kBarsPerLine == 0)) out += "|\n";
        else if (i > 0)                       out += "| ";

        const auto& bar = bars[i];
        for (size_t j = 0; j < bar.pieces.size(); ++j) {
            if (j > 0) out += " ";
            out += emitPiece(bar.pieces[j], isDrums);
        }
        if (bar.pieces.empty()) out += "z" + encodeDuration(bar.barLengthBeats);
    }
    out += "|";
    return out;
}

// ---------------- Drummap directive --------------------------------------

std::string drummapDirective() {
    std::string out;
    for (auto& d : kDrumMappings) {
        out += "%%MIDI drummap " + std::string(1, d.letter) + " "
             + std::to_string(d.midi) + "\n";
    }
    return out;
}

}  // namespace

std::string ABCWriter::write(const ABCWriteInput& input) {
    // Resolve length.
    double length = input.lengthBeats;
    if (length <= 0.0) {
        for (auto& v : input.voices) {
            for (auto& n : v.notes) {
                length = std::max(length, n.startBeat + n.durationBeats);
            }
        }
    }
    double beatsPerBar = (input.timeSignatureNum > 0 && input.timeSignatureDen > 0)
        ? (4.0 * input.timeSignatureNum / input.timeSignatureDen)
        : 4.0;
    int barCount = std::max(1, static_cast<int>(std::ceil(length / beatsPerBar - 1e-6)));

    std::ostringstream os;

    // Header.
    os << "X:1\n";
    if (!input.title.empty())
        os << "T:" << input.title << "\n";
    os << "L:1/8\n";
    // Tempo: emit as Q:1/4=BPM (quarter-note = bpm). ABC convention.
    os << "Q:1/4=" << static_cast<int>(std::lround(input.tempo)) << "\n";
    os << "M:" << input.timeSignatureNum << "/" << input.timeSignatureDen << "\n";
    os << "K:" << (input.key.empty() ? std::string("none") : input.key) << "\n";

    bool anyDrums = false;
    for (auto& v : input.voices) anyDrums = anyDrums || v.isDrums;
    if (anyDrums) os << drummapDirective();

    if (input.voices.size() == 1) {
        const auto& v = input.voices.front();
        auto grouped = groupChords(v.notes);
        auto bars    = splitIntoBars(grouped, barCount, beatsPerBar);
        os << emitVoiceBody(bars, v.isDrums) << "\n";
    } else {
        // Voice declarations first, then one block per voice.
        for (auto& v : input.voices) os << "V:" << v.name << "\n";
        for (auto& v : input.voices) {
            os << "V:" << v.name << "\n";
            auto grouped = groupChords(v.notes);
            auto bars    = splitIntoBars(grouped, barCount, beatsPerBar);
            os << emitVoiceBody(bars, v.isDrums) << "\n";
        }
    }
    return os.str();
}
