#include "composer/V2NotationParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>

// Port of dawai's compiler.py (app/compiler.py at commit 24a325e in
// ~/ideas_and_projects/dawai). The goal is bit-for-bit agreement on
// the shared test cases: same notation string must produce the same
// note (startBeat, pitch, velocity, durationBeats) quartets. Fixtures
// land in tests/PerformanceTests.cpp.
//
// Key differences from the Python version:
//   - Output is ComposerOutput (canonical) instead of MIDI bytes.
//   - Everything is expressed in beats (double), not ticks. The
//     Python compiler uses TICKS_PER_BEAT=480 internally; we
//     translate to beats at emit time.
//   - We ignore program-change / channel-routing metadata. The
//     ComposerWriter maps tracks by display name; the host project
//     already has a sound assigned to each track.

namespace {

// ------------- Tables --------------------------------------------------

const std::unordered_map<char, int> kNoteNames = {
    {'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11}
};

const std::unordered_map<std::string, int> kVelocityMap = {
    {"ppp", 20}, {"pp", 35}, {"p", 50}, {"mp", 64},
    {"mf", 80}, {"f", 96}, {"ff", 112}, {"fff", 124}
};

// Durations expressed in beats (the Python version uses ticks at
// TICKS_PER_BEAT=480; we skip the tick-intermediate).
const std::unordered_map<std::string, double> kDurationBeats = {
    {"w", 4.0},     {"h", 2.0},     {"h.", 3.0},
    {"q", 1.0},     {"q.", 1.5},
    {"8th", 0.5},   {"8th.", 0.75},
    {"16th", 0.25},
};

const std::unordered_map<std::string, int> kDrumMap = {
    {"kick", 36}, {"snare", 38}, {"rim", 37}, {"clap", 39},
    {"hhc", 42},  {"hho", 46},   {"hhp", 44},
    {"crash", 49},{"ride", 51},  {"rbell", 53},
    {"ltom", 45}, {"mtom", 47},  {"htom", 50},
};

// ------------- Small string helpers ------------------------------------

std::string trimCopy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size()
        && std::equal(prefix.begin(), prefix.end(), s.begin());
}

std::vector<std::string> splitLines(const std::string& src) {
    std::vector<std::string> out;
    std::string line;
    for (char c : src) {
        if (c == '\n') { out.push_back(line); line.clear(); }
        else          { line.push_back(c); }
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

std::vector<std::string> splitOn(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string part;
    for (char c : s) {
        if (c == sep) { out.push_back(part); part.clear(); }
        else          { part.push_back(c); }
    }
    out.push_back(part);
    return out;
}

std::vector<std::string> splitWhitespace(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

// ------------- Token parsers -------------------------------------------

std::optional<int> parseNoteName(const std::string& noteIn, bool isDrums) {
    auto note = trimCopy(noteIn);
    if (note.empty() || note == "r") return std::nullopt;

    if (isDrums) {
        auto it = kDrumMap.find(note);
        if (it == kDrumMap.end()) return std::nullopt;
        return it->second;
    }

    size_t i = 0;
    char baseChar = static_cast<char>(std::toupper(static_cast<unsigned char>(note[i])));
    auto bit = kNoteNames.find(baseChar);
    if (bit == kNoteNames.end()) return std::nullopt;
    int midi = bit->second;
    ++i;

    if (i < note.size() && note[i] == '#') {
        ++midi; ++i;
    } else if (i < note.size() && note[i] == 'b'
               && (i + 1 >= note.size()
                   || !std::isalpha(static_cast<unsigned char>(note[i + 1])))) {
        --midi; ++i;
    }

    if (i >= note.size()) return std::nullopt;
    std::string octaveStr = note.substr(i);
    try {
        int octave = std::stoi(octaveStr);
        return midi + (octave + 1) * 12;
    } catch (...) {
        return std::nullopt;
    }
}

// Beat position within a bar. "1" → 0.0 beats, "2+" → 1.5 beats,
// "3.5" → 2.5 beats. The format is 1-based (like dawai) but we emit
// 0-based relative offsets.
double parseBeatPosition(const std::string& in) {
    auto s = trimCopy(in);
    double beat = 0.0;
    if (!s.empty() && s.back() == '+') {
        try { beat = std::stod(s.substr(0, s.size() - 1)) + 0.5; }
        catch (...) { beat = 1.0; }
    } else {
        try { beat = std::stod(s); } catch (...) { beat = 1.0; }
    }
    return beat - 1.0;
}

// Returns (durationBeats, tiedFlag). Unknown durations fall back to 1 beat.
std::pair<double, bool> parseDuration(const std::string& in) {
    auto s = trimCopy(in);
    bool tied = !s.empty() && s.back() == '~';
    if (tied) s.pop_back();
    auto it = kDurationBeats.find(s);
    double dur = (it == kDurationBeats.end()) ? 1.0 : it->second;
    return {dur, tied};
}

int parseVelocity(const std::string& in) {
    auto s = trimCopy(in);
    auto it = kVelocityMap.find(s);
    if (it != kVelocityMap.end()) return it->second;
    try {
        int v = std::stoi(s);
        return std::max(1, std::min(127, v));
    } catch (...) {
        return 80;
    }
}

// "swing" / "shuffle" / "swung" / "triplet feel" in the feel string
// → 1.0, anything else → 0.0. Matches Python's detect_swing().
double detectSwing(const std::string& feelRaw) {
    std::string feel;
    feel.reserve(feelRaw.size());
    for (char c : feelRaw) feel.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    const char* kws[] = {"swing", "shuffle", "swung", "triplet feel"};
    for (auto* kw : kws) {
        if (feel.find(kw) != std::string::npos) return 1.0;
    }
    return 0.0;
}

// ------------- Event parser -------------------------------------------

struct RawEvent {
    double beatOffsetInBar;   // 0 = downbeat of bar
    int pitch;                // MIDI 0-127
    int velocity;             // 1-127
    double durationBeats;
};

// Diagnostic counters threaded through parseVoiceEvents so the
// top-level parser can produce a specific error when every event was
// silently dropped (the most common cause: drum tokens on a non-drums
// track, or pitched notes on a drums track — parseNoteName returns
// nullopt in both cases). Without this the user sees only "nothing to
// write" with no hint at the actual mismatch, and the LLM tends to
// react by creating duplicate tracks instead of fixing the notation.
struct DropDiag {
    int attempted = 0;             // notes the parser tried to read
    int dropped   = 0;             // notes parseNoteName couldn't resolve
    std::string firstDroppedToken; // sample for the error message
};

// Returns true if `tok` is a known drum-hit token (kick / snare /
// hhc / etc.). Used by the diagnostic layer to distinguish "drum
// tokens on non-drums track" from "pitched notes on drums track".
bool isDrumToken(const std::string& tok) {
    return kDrumMap.find(tok) != kDrumMap.end();
}

// Mirrors parse_events() in compiler.py. Splits a voice line on `|`
// then on `,`, parses each `beat <pos> <note-or-chord> <dur> <vel>`
// clause, handles chords in `[n n n]` form.
std::vector<RawEvent> parseVoiceEvents(const std::string& eventStr, bool isDrums,
                                         DropDiag* diag = nullptr) {
    std::vector<RawEvent> out;

    static const std::regex beatRe(R"(beat\s+([\d.+]+)\s+(.+))");
    static const std::regex chordRe(R"(\[([^\]]+)\]\s+(\S+)\s+(\S+))");
    static const std::regex singleRe(R"((\S+)\s+(\S+)\s+(\S+))");

    for (auto& partRaw : splitOn(eventStr, '|')) {
        auto part = trimCopy(partRaw);
        if (part.empty()) continue;

        for (auto& subRaw : splitOn(part, ',')) {
            auto sub = trimCopy(subRaw);
            if (sub.empty()) continue;

            std::smatch m;
            if (!std::regex_match(sub, m, beatRe)) continue;

            double beatOffset = parseBeatPosition(m[1].str());
            std::string remainder = trimCopy(m[2].str());

            if (startsWith(remainder, "r ")) continue;  // rest

            std::smatch cm, sm;
            if (std::regex_search(remainder, cm, chordRe) && cm.position() == 0) {
                auto notesStr = cm[1].str();
                auto [dur, _tied] = parseDuration(cm[2].str());
                int vel = parseVelocity(cm[3].str());
                for (auto& noteTok : splitWhitespace(notesStr)) {
                    if (diag) diag->attempted++;
                    auto pitch = parseNoteName(noteTok, isDrums);
                    if (pitch) out.push_back({beatOffset, *pitch, vel, dur});
                    else if (diag) {
                        diag->dropped++;
                        if (diag->firstDroppedToken.empty())
                            diag->firstDroppedToken = noteTok;
                    }
                }
            } else if (std::regex_search(remainder, sm, singleRe) && sm.position() == 0) {
                if (diag) diag->attempted++;
                auto pitch = parseNoteName(sm[1].str(), isDrums);
                if (pitch) {
                    auto [dur, _tied] = parseDuration(sm[2].str());
                    int vel = parseVelocity(sm[3].str());
                    out.push_back({beatOffset, *pitch, vel, dur});
                } else if (diag) {
                    diag->dropped++;
                    if (diag->firstDroppedToken.empty())
                        diag->firstDroppedToken = sm[1].str();
                }
            }
        }
    }
    return out;
}

// ------------- Top-level parse ----------------------------------------

struct BarData {
    int number = 0;
    std::string chord;
    // voice label (may be "Piano" or "Piano melody") → events
    std::vector<std::pair<std::string, std::vector<RawEvent>>> voices;
};

struct Score {
    int tempo = 120;
    std::string timeSignature = "4/4";
    std::string key;
    std::string feel;
    // declaration order preserved (dawai relies on order for channel assignment)
    std::vector<std::pair<std::string, std::string>> tracks;  // name → gm-program or "drums"
    std::vector<BarData> bars;
    double swing = 0.0;

    // Aggregated drop diagnostics — populated by parseScore so the
    // top-level parse() can return a specific "drum vs pitched
    // mismatch"-style error when every event got silently dropped.
    int totalAttempted = 0;
    int totalDropped = 0;
    std::string firstDroppedToken;     // sample
    std::string firstDroppingTrack;
    bool firstDroppingIsDrumsTrack = false;
};

// Parse a single track declaration line ("  Piano: 0", "  Drum Kit: drums").
bool parseTrackDecl(const std::string& line, std::string& name, std::string& value) {
    static const std::regex trackRe(R"(\s+([^:]+):\s*(.+))");
    std::smatch m;
    if (!std::regex_match(line, m, trackRe)) return false;
    name = trimCopy(m[1].str());
    value = trimCopy(m[2].str());
    return !name.empty();
}

bool isDrumsTrack(const Score& s, const std::string& trackName) {
    for (auto& [n, v] : s.tracks) {
        if (n == trackName) return v == "drums";
    }
    return false;
}

// Voice label → parent track name. Matches if label equals the track
// name exactly, or starts with the track name + space (so "Piano" and
// "Piano melody" both map to track "Piano", with the longer match
// preferred).
std::string resolveParentTrack(const Score& s, const std::string& voiceLabel) {
    std::string best;
    for (auto& [trackName, _v] : s.tracks) {
        if (voiceLabel == trackName) return trackName;
        if (startsWith(voiceLabel, trackName + " ")
            && trackName.size() > best.size()) {
            best = trackName;
        }
    }
    return best;
}

bool parseScore(const std::string& src, Score& score, std::string& err) {
    auto lines = splitLines(src);
    bool inTracks = false;
    BarData* currentBar = nullptr;

    for (auto& rawLine : lines) {
        // Strip trailing \r\n artefacts only; keep leading indent for
        // detecting voice / track lines.
        std::string line = rawLine;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();

        if (line.empty() || trimCopy(line).empty()) {
            inTracks = false;
            currentBar = nullptr;
            continue;
        }

        auto stripped = trimCopy(line);

        if (stripped == "tracks:") {
            inTracks = true;
            continue;
        }

        if (inTracks && startsWith(line, "  ")) {
            std::string n, v;
            if (parseTrackDecl(line, n, v)) {
                score.tracks.emplace_back(n, v);
            }
            continue;
        }
        inTracks = false;

        // Metadata lines
        if (startsWith(stripped, "tempo:")) {
            try { score.tempo = std::stoi(trimCopy(stripped.substr(6))); }
            catch (...) {}
            continue;
        }
        if (startsWith(stripped, "time_signature:")) {
            score.timeSignature = trimCopy(stripped.substr(15));
            continue;
        }
        if (startsWith(stripped, "key:")) {
            score.key = trimCopy(stripped.substr(4));
            continue;
        }
        if (startsWith(stripped, "feel:")) {
            score.feel = trimCopy(stripped.substr(5));
            continue;
        }

        // bar N | Chord
        if (startsWith(stripped, "bar ")) {
            static const std::regex barRe(R"(bar\s+(\d+)\s*\|\s*(.+))");
            std::smatch m;
            if (std::regex_match(stripped, m, barRe)) {
                BarData b;
                try { b.number = std::stoi(m[1].str()); } catch (...) { b.number = 0; }
                b.chord = trimCopy(m[2].str());
                score.bars.push_back(std::move(b));
                currentBar = &score.bars.back();
            }
            continue;
        }

        // Voice line (starts with "  " and we have a current bar).
        if (startsWith(line, "  ") && currentBar != nullptr) {
            auto body = stripped;
            auto colon = body.find(':');
            if (colon == std::string::npos) continue;
            std::string voiceLabel = trimCopy(body.substr(0, colon));
            std::string events = body.substr(colon + 1);

            auto parent = resolveParentTrack(score, voiceLabel);
            if (parent.empty()) continue;

            DropDiag diag;
            bool isDrumsParent = isDrumsTrack(score, parent);
            auto parsed = parseVoiceEvents(events, isDrumsParent, &diag);
            currentBar->voices.emplace_back(voiceLabel, std::move(parsed));

            score.totalAttempted += diag.attempted;
            score.totalDropped   += diag.dropped;
            if (diag.dropped > 0 && score.firstDroppedToken.empty()) {
                score.firstDroppedToken         = diag.firstDroppedToken;
                score.firstDroppingTrack        = parent;
                score.firstDroppingIsDrumsTrack = isDrumsParent;
            }
        }
    }

    score.swing = detectSwing(score.feel);

    if (score.tracks.empty()) {
        err = "no tracks declared (expected `tracks:` block)";
        return false;
    }
    return true;
}

// Convert parsed Score → ComposerOutput in beats.
void emitOutput(const Score& score, ComposerOutput& out) {
    out.tempo = static_cast<double>(score.tempo);

    // Time signature (optional override).
    {
        auto slash = score.timeSignature.find('/');
        if (slash != std::string::npos) {
            try {
                int num = std::stoi(score.timeSignature.substr(0, slash));
                int den = std::stoi(score.timeSignature.substr(slash + 1));
                out.timeSignature.emplace_back(num, den);
            } catch (...) {}
        }
    }

    // dawai's Python compiler hardcodes `bar_start = (bar - 1) * TICKS_PER_BEAT * 4`
    // — i.e., assumes 4/4. We preserve that behaviour for bit-level
    // agreement with their tests, and revisit if the format grows
    // variable-meter support.
    const double beatsPerBar = 4.0;

    for (auto& bar : score.bars) {
        double barStart = (bar.number - 1) * beatsPerBar;
        for (auto& [voiceLabel, evs] : bar.voices) {
            auto parent = resolveParentTrack(score, voiceLabel);
            if (parent.empty()) continue;
            for (auto& e : evs) {
                double absBeat = barStart + e.beatOffsetInBar;
                // Swing shifts offbeat eighths to the triplet grid:
                //   half-beat (0.5) → two-thirds (2/3) when swing=1.
                double fracInBeat = e.beatOffsetInBar - std::floor(e.beatOffsetInBar);
                constexpr double kHalf = 0.5;
                if (score.swing > 0.0 && std::abs(fracInBeat - kHalf) < 1e-9) {
                    double shift = score.swing * (2.0 / 3.0 - kHalf);
                    absBeat += shift;
                }

                ComposerOutput::Note n;
                n.trackName = parent;
                n.startBeat = absBeat;
                n.durationBeats = e.durationBeats;
                n.pitch = e.pitch;
                n.velocity = static_cast<float>(e.velocity) / 127.0f;
                out.notes.push_back(std::move(n));
            }
        }
    }

    // Total region length: at least `bar count × beats per bar`, but
    // extend to cover any note whose tail runs past the final bar
    // (e.g. a half note on beat 4 of the last bar). Clipping would
    // change the user's intent; growing the region is the honest move.
    if (!score.bars.empty()) {
        int maxBar = 0;
        for (auto& b : score.bars) maxBar = std::max(maxBar, b.number);
        out.lengthBeats = maxBar * beatsPerBar;
    }
    for (auto& n : out.notes) {
        out.lengthBeats = std::max(out.lengthBeats, n.startBeat + n.durationBeats);
    }

    // Stable order for deterministic output + downstream sort-friendliness.
    std::sort(out.notes.begin(), out.notes.end(),
        [](const ComposerOutput::Note& a, const ComposerOutput::Note& b) {
            if (a.trackName != b.trackName) return a.trackName < b.trackName;
            if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
            return a.pitch < b.pitch;
        });
}

}  // namespace

bool V2NotationParser::parse(const juce::String& input,
                              ComposerOutput& out,
                              std::string& err) {
    out = {};
    err.clear();

    Score score;
    if (!parseScore(input.toStdString(), score, err)) return false;
    emitOutput(score, out);

    // Specific-error layer: if every note got silently dropped, blaming
    // "nothing to write" makes callers (especially the chat LLM) suspect
    // the wrong thing — typically they react by creating duplicate
    // tracks instead of fixing the notation. Detect the common drum-vs-
    // pitched mismatch and say so plainly.
    if (out.notes.empty() && score.totalAttempted > 0 && score.totalDropped > 0) {
        const auto& tok = score.firstDroppedToken;
        const auto& track = score.firstDroppingTrack;
        if (score.firstDroppingIsDrumsTrack) {
            // Drums-typed track but the events use non-drum tokens
            // (almost always pitched note names like C1 / D1).
            err = "track '" + track + "' is declared as 'drums' but its events "
                  "use non-drum tokens (e.g. '" + tok + "'). Drums-typed tracks "
                  "accept only drum tokens like kick, snare, hhc, hho, ride, "
                  "crash, ltom, mtom, htom. Either change the header to '"
                  + track + ": 0' (or another GM program) and keep the pitched "
                  "notes, or keep '" + track + ": drums' and rewrite the "
                  "events using drum tokens.";
        } else if (isDrumToken(tok)) {
            // GM-instrument track but the events use drum tokens.
            err = "track '" + track + "' is declared as a GM instrument but its "
                  "events use drum tokens (e.g. '" + tok + "'). Drum tokens "
                  "require the track to be declared as 'drums' in the header. "
                  "Either change the header to '" + track + ": drums' or rewrite "
                  "the events using pitched note names like C1, D1, E1 (these map "
                  "to the GM percussion notes 36, 38, 40).";
        } else {
            // Unknown token shape — generic fallback that at least names
            // the offender so the caller doesn't have to guess.
            err = "all events on track '" + track + "' were dropped — the parser "
                  "didn't recognize the note token '" + tok + "'. Pitched notes "
                  "look like 'C4' / 'G#4' / 'Bb2'; drum tokens (on a drums-typed "
                  "track) look like 'kick' / 'snare' / 'hhc'.";
        }
        return false;
    }
    return true;
}
