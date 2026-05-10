#include "composer/ABCParser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

// ---------------- Constants -----------------------------------------------

// ABC base pitch (capital letter, no marker) → MIDI semitone offset
// from C4 (60). 'C' itself is C4 (offset 0). 'B' is B4 (offset 11).
const std::unordered_map<char, int> kBasePitch = {
    {'C', 0}, {'D', 2}, {'E', 4}, {'F', 5}, {'G', 7}, {'A', 9}, {'B', 11}
};

// ---------------- Small helpers -------------------------------------------

std::string trimCopy(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> splitLines(const std::string& src) {
    std::vector<std::string> out;
    std::string line;
    for (char c : src) {
        if (c == '\n') { out.push_back(line); line.clear(); }
        else if (c != '\r') { line.push_back(c); }
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && std::equal(p.begin(), p.end(), s.begin());
}

bool isHeaderLine(const std::string& s) {
    // ABC header lines: single uppercase letter + ':'.
    return s.size() >= 2 && std::isupper(static_cast<unsigned char>(s[0])) && s[1] == ':';
}

// ---------------- Header structures ---------------------------------------

struct Header {
    int x = 1;
    std::string title;
    std::string defaultLength = "1/8";  // L:
    int tempoBpm = 120;                  // from Q:1/4=N (or Q:N)
    int meterNum = 4;                    // M:
    int meterDen = 4;
    std::string key = "none";            // K:
    bool sawK = false;                   // body starts after first K:
};

// ---------------- Per-voice state -----------------------------------------

// Dynamic marks → MIDI velocity (matches V2 / common DAW convention).
const std::unordered_map<std::string, int> kDynamicVelocity = {
    {"ppp", 20}, {"pp", 35}, {"p", 50}, {"mp", 64},
    {"mf", 80}, {"f", 96}, {"ff", 112}, {"fff", 124},
};

struct Voice {
    std::string name;
    bool isDrums = false;
    std::vector<ComposerOutput::Note> notes;
    double cursor = 0.0;        // current beat position
    int barNumber = 1;          // 1-based — for diagnostics
    int currentVelocity = 80;   // sticky — set by !ff! / !p! etc, applied to subsequent notes
    // Carry-tied note: if the previous note ended with `-`, the next
    // note of matching pitch extends rather than starting fresh.
    struct Tie {
        int pitch = -1;
        size_t noteIndex = 0;   // index into `notes` of the tail-receiving note
    };
    std::vector<Tie> openTies;
};

// ---------------- Header parsing ------------------------------------------

bool parseTempo(const std::string& v, int& bpm) {
    // Forms: "120", "1/4=120", "1/4 = 120", "C=120"
    std::string s = trimCopy(v);
    auto eq = s.find('=');
    if (eq == std::string::npos) {
        try { bpm = std::stoi(s); return true; } catch (...) { return false; }
    }
    try { bpm = std::stoi(trimCopy(s.substr(eq + 1))); return true; }
    catch (...) { return false; }
}

bool parseMeter(const std::string& v, int& num, int& den) {
    std::string s = trimCopy(v);
    if (s == "C") { num = 4; den = 4; return true; }
    if (s == "C|") { num = 2; den = 2; return true; }
    auto slash = s.find('/');
    if (slash == std::string::npos) return false;
    try {
        num = std::stoi(trimCopy(s.substr(0, slash)));
        den = std::stoi(trimCopy(s.substr(slash + 1)));
        return num > 0 && den > 0;
    } catch (...) { return false; }
}

double parseDefaultLengthBeats(const std::string& v) {
    // L: value is a fraction "1/8". Return its beat-equivalent
    // assuming quarter = 1 beat. So 1/4 → 1.0, 1/8 → 0.5, 1/16 → 0.25.
    auto s = trimCopy(v);
    auto slash = s.find('/');
    if (slash == std::string::npos) return 0.5;
    try {
        double num = std::stod(s.substr(0, slash));
        double den = std::stod(s.substr(slash + 1));
        if (den == 0) return 0.5;
        // beats = (num/den) * 4 (because quarter = 1 beat = 1/4)
        return (num / den) * 4.0;
    } catch (...) { return 0.5; }
}

// ---------------- Drummap state -------------------------------------------

struct DrumMap {
    std::unordered_map<char, int> map;   // ABC letter → MIDI pitch
    bool empty() const { return map.empty(); }
};

bool parseDrummap(const std::string& body, DrumMap& dm, std::string& err) {
    // Body example: "B 36" or "^G 38"
    std::istringstream is(body);
    std::string letterTok, midiTok;
    if (!(is >> letterTok >> midiTok)) {
        err = "malformed %%MIDI drummap directive: '" + body + "'";
        return false;
    }
    if (letterTok.size() != 1 || !std::isalpha(static_cast<unsigned char>(letterTok[0]))) {
        err = "drummap letter must be a single alphabetic character: '" + letterTok + "'";
        return false;
    }
    try {
        int midi = std::stoi(midiTok);
        if (midi < 0 || midi > 127) { err = "drummap pitch out of range"; return false; }
        dm.map[letterTok[0]] = midi;
        return true;
    } catch (...) {
        err = "drummap pitch not an integer: '" + midiTok + "'";
        return false;
    }
}

// ---------------- Tokenizer for body lines --------------------------------
//
// Walks one body string, emitting events:
//   - bar line (|)
//   - note (pitch, duration, tied flag)
//   - chord (vector<pitch>, duration, tied flag)
//   - rest (duration)
//
// Returns false on syntax error and fills `err`.

struct ParsedNote {
    enum Kind { Note, Chord, Rest, Bar } kind = Note;
    std::vector<int> pitches;   // for Note (1) or Chord (1+)
    double durationBeats = 0.0;
    bool tied = false;
};

// Reads a duration suffix at `pos`. ABC duration grammar (where L: is
// the default length):
//   ""        → 1 × L
//   N         → N × L          (e.g., "C2" = 2L)
//   /         → L/2
//   /N        → L / N          (e.g., "C/2" = L/2)
//   N/        → N × L / 2
//   N/M       → N × L / M
//
// Returns the multiplier applied to L.
double readDuration(const std::string& s, size_t& pos) {
    double num = 0;
    double den = 0;
    bool sawDigits = false;

    // Numerator digits.
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
        num = num * 10 + (s[pos] - '0');
        ++pos;
        sawDigits = true;
    }

    if (pos < s.size() && s[pos] == '/') {
        ++pos;
        bool sawDenDigits = false;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
            den = den * 10 + (s[pos] - '0');
            ++pos;
            sawDenDigits = true;
        }
        if (!sawDigits) num = 1;
        if (!sawDenDigits) den = 2;
        return num / den;
    }

    if (!sawDigits) return 1.0;
    return num;
}

// Reads an ABC pitch starting at `pos`. Returns nullopt + advances `pos`
// past invalid chars. Updates octave / accidental from the encoding.
std::optional<int> readPitch(const std::string& s, size_t& pos) {
    int accidental = 0;   // -2..+2
    while (pos < s.size()) {
        char c = s[pos];
        if (c == '^') { accidental += 1; ++pos; }
        else if (c == '_') { accidental -= 1; ++pos; }
        else if (c == '=') { accidental = 0; ++pos; }
        else break;
    }
    if (pos >= s.size()) return std::nullopt;
    char c = s[pos];
    bool lower = (c >= 'a' && c <= 'g');
    bool upper = (c >= 'A' && c <= 'G');
    if (!lower && !upper) return std::nullopt;
    char base = lower ? static_cast<char>(c - 'a' + 'A') : c;
    auto it = kBasePitch.find(base);
    if (it == kBasePitch.end()) return std::nullopt;
    ++pos;

    // Octave markers: ' raises, , lowers.
    int octave = lower ? 5 : 4;
    while (pos < s.size() && (s[pos] == '\'' || s[pos] == ',')) {
        if (s[pos] == '\'') ++octave;
        else                --octave;
        ++pos;
    }

    int midi = it->second + (octave + 1) * 12 + accidental;
    if (midi < 0 || midi > 127) return std::nullopt;
    return midi;
}

// ---------------- Body line parser ----------------------------------------

bool parseBodyLine(const std::string& lineIn,
                   Voice& voice,
                   const DrumMap& drumMap,
                   double L_beats,
                   double beatsPerBar,
                   std::string& err) {
    std::string line = lineIn;

    // Strip trailing line continuation `\\` if present.
    if (!line.empty() && line.back() == '\\') line.pop_back();

    // Strip inline comments (% to end of line). Watch for `%%` directives
    // — those should already have been handled before we get here.
    {
        auto pct = line.find('%');
        if (pct != std::string::npos) line = line.substr(0, pct);
    }

    size_t pos = 0;
    auto skipSpace = [&]() {
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) ++pos;
    };

    while (pos < line.size()) {
        skipSpace();
        if (pos >= line.size()) break;
        char c = line[pos];

        // Comment chunk (defensive — should be stripped above).
        if (c == '%') break;

        // Quoted text annotation "..." — skip.
        if (c == '"') {
            ++pos;
            while (pos < line.size() && line[pos] != '"') ++pos;
            if (pos < line.size()) ++pos;
            continue;
        }

        // Decorations like !ff! or +p+. If the body matches a known
        // dynamic mark, update the voice's sticky velocity. Otherwise
        // skip silently.
        if (c == '!' || c == '+') {
            char term = c;
            ++pos;
            std::string body;
            while (pos < line.size() && line[pos] != term) {
                body.push_back(line[pos]);
                ++pos;
            }
            if (pos < line.size()) ++pos;
            auto dit = kDynamicVelocity.find(trimCopy(body));
            if (dit != kDynamicVelocity.end()) voice.currentVelocity = dit->second;
            continue;
        }

        // Inline header [X:...] — reject the ones that change time/pitch
        // domains (Q, M, K). Allow harmless ones (e.g., [I:...] info).
        if (c == '[' && pos + 2 < line.size() && line[pos + 2] == ':') {
            char field = line[pos + 1];
            if (field == 'Q' || field == 'M' || field == 'K') {
                err = "inline header '[" + std::string(1, field)
                    + ":...]' is not yet supported (Phase 3)";
                return false;
            }
            // Skip until ].
            ++pos;
            while (pos < line.size() && line[pos] != ']') ++pos;
            if (pos < line.size()) ++pos;
            continue;
        }

        // Bar lines and repeats. Bar-line tokens we recognize:
        //   |   bar
        //   |]  end bar
        //   |:  start repeat
        //   :|  end repeat (V1 just acknowledges; expansion happens via duplicated source if any)
        //   ::  start+end repeat
        //   [1  first volta marker
        //   [2  second volta marker
        if (c == '|' || c == ':') {
            // Volta markers like [1 already handled above (they start with `[`)
            // Walk run of |/:/].
            while (pos < line.size() && (line[pos] == '|' || line[pos] == ':' || line[pos] == ']')) ++pos;
            // Consume optional volta number after barline, e.g., `|1`, `:|2`.
            while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) ++pos;
            ++voice.barNumber;
            // Don't advance cursor — bar boundaries are derived from
            // cumulative durations; the | is informational.
            continue;
        }

        // Volta `[1` `[2`.
        if (c == '[' && pos + 1 < line.size() && std::isdigit(static_cast<unsigned char>(line[pos + 1]))) {
            pos += 2;
            while (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos]))) ++pos;
            continue;
        }

        // Chord [CEG]N
        if (c == '[') {
            ++pos;
            std::vector<int> pitches;
            while (pos < line.size() && line[pos] != ']') {
                size_t before = pos;
                auto p = readPitch(line, pos);
                if (p) pitches.push_back(*p);
                else if (pos == before) ++pos;   // can't make progress: skip 1 char
            }
            if (pos < line.size() && line[pos] == ']') ++pos;
            double mult = readDuration(line, pos);
            bool tied = (pos < line.size() && line[pos] == '-');
            if (tied) ++pos;

            double dur = mult * L_beats;
            for (int p : pitches) {
                ComposerOutput::Note n;
                n.trackName     = voice.name;
                n.startBeat     = voice.cursor;
                n.durationBeats = dur;
                n.pitch         = p;
                n.velocity      = static_cast<float>(voice.currentVelocity) / 127.0f;
                voice.notes.push_back(n);
            }
            voice.cursor += dur;
            (void)tied;   // chord-level ties not modeled in V1
            continue;
        }

        // Rest: z (one L) / Z (one bar).
        if (c == 'z' || c == 'x') {
            ++pos;
            double mult = readDuration(line, pos);
            voice.cursor += mult * L_beats;
            continue;
        }
        if (c == 'Z') {
            ++pos;
            double mult = readDuration(line, pos);
            voice.cursor += mult * beatsPerBar;
            continue;
        }

        // Drum letter (single uppercase non-pitch letter on a drums voice
        // that's been drummap'd). Check drum map BEFORE pitch-letter
        // path so e.g. 'B' on a drums voice maps via drummap, not as
        // ABC pitch B.
        if (voice.isDrums && std::isalpha(static_cast<unsigned char>(c))
            && drumMap.map.count(c)) {
            int pitch = drumMap.map.at(c);
            ++pos;
            double mult = readDuration(line, pos);
            bool tied = (pos < line.size() && line[pos] == '-');
            if (tied) ++pos;

            ComposerOutput::Note n;
            n.trackName     = voice.name;
            n.startBeat     = voice.cursor;
            n.durationBeats = mult * L_beats;
            n.pitch         = pitch;
            n.velocity      = static_cast<float>(voice.currentVelocity) / 127.0f;
            voice.notes.push_back(n);
            voice.cursor += n.durationBeats;
            continue;
        }

        // Pitched note path.
        size_t before = pos;
        auto pitch = readPitch(line, pos);
        if (pitch) {
            double mult = readDuration(line, pos);
            bool tied = (pos < line.size() && line[pos] == '-');
            if (tied) ++pos;

            double dur = mult * L_beats;

            // If this pitch matches a previously-tied-out note of same
            // pitch and the cursor matches the open tie's tail beat,
            // extend rather than start fresh.
            bool extended = false;
            for (auto it = voice.openTies.begin(); it != voice.openTies.end(); ++it) {
                if (it->pitch == *pitch) {
                    voice.notes[it->noteIndex].durationBeats += dur;
                    voice.openTies.erase(it);
                    extended = true;
                    break;
                }
            }
            if (!extended) {
                ComposerOutput::Note n;
                n.trackName     = voice.name;
                n.startBeat     = voice.cursor;
                n.durationBeats = dur;
                n.pitch         = *pitch;
                n.velocity      = static_cast<float>(voice.currentVelocity) / 127.0f;
                voice.notes.push_back(n);
                if (tied) voice.openTies.push_back({*pitch, voice.notes.size() - 1});
            } else if (tied) {
                // Re-arm tie on the now-extended note.
                voice.openTies.push_back({*pitch, voice.notes.size() - 1});
            }
            voice.cursor += dur;
            continue;
        }

        // Unknown token: bail forward by 1 char to avoid infinite loop.
        if (pos == before) ++pos;
    }

    return true;
}

// ---------------- Top-level orchestration ---------------------------------

bool parseImpl(const std::string& src, ComposerOutput& out, std::string& err) {
    auto lines = splitLines(src);
    Header hdr;
    DrumMap drumMap;
    std::vector<Voice> voices;
    Voice* currentVoice = nullptr;

    auto findVoice = [&](const std::string& name) -> Voice* {
        for (auto& v : voices) if (v.name == name) return &v;
        return nullptr;
    };

    auto ensureDefaultVoice = [&]() {
        if (voices.empty()) {
            Voice v;
            v.name = "default";
            voices.push_back(v);
            currentVoice = &voices.back();
        } else if (!currentVoice) {
            currentVoice = &voices.front();
        }
    };

    bool inBody = false;

    for (auto& rawLine : lines) {
        auto line = trimCopy(rawLine);
        if (line.empty()) continue;

        // Directive: %%KEY value
        if (startsWith(line, "%%")) {
            auto rest = trimCopy(line.substr(2));
            if (startsWith(rest, "MIDI drummap ")) {
                std::string e;
                if (!parseDrummap(trimCopy(rest.substr(13)), drumMap, e)) {
                    err = e; return false;
                }
            }
            // Other directives ignored in V1.
            continue;
        }

        // Plain comment.
        if (line[0] == '%') continue;

        // Header line (single uppercase + ':').
        if (isHeaderLine(line)) {
            char field = line[0];
            std::string value = trimCopy(line.substr(2));

            if (!inBody) {
                switch (field) {
                    case 'X': try { hdr.x = std::stoi(value); } catch (...) {} break;
                    case 'T': hdr.title = value; break;
                    case 'L': hdr.defaultLength = value; break;
                    case 'Q': if (!parseTempo(value, hdr.tempoBpm)) {
                                  err = "invalid Q: tempo '" + value + "'";
                                  return false;
                              } break;
                    case 'M': if (!parseMeter(value, hdr.meterNum, hdr.meterDen)) {
                                  err = "invalid M: meter '" + value + "'";
                                  return false;
                              } break;
                    case 'K':
                        hdr.key = value;
                        hdr.sawK = true;
                        inBody = true;
                        break;
                    case 'V':
                        // V: in header section is a voice declaration.
                        if (!findVoice(value)) {
                            Voice v;
                            v.name = value;
                            v.isDrums = (value == "Drums" || value == "drums");
                            voices.push_back(v);
                        }
                        break;
                    default: break;     // ignore other header fields
                }
            } else {
                // In-body header. Only V: is supported (voice switch).
                // Q/M/K mid-piece is rejected.
                if (field == 'V') {
                    Voice* v = findVoice(value);
                    if (!v) {
                        Voice nv;
                        nv.name = value;
                        nv.isDrums = (value == "Drums" || value == "drums");
                        voices.push_back(nv);
                        v = &voices.back();
                    }
                    currentVoice = v;
                } else if (field == 'Q' || field == 'M') {
                    err = "mid-piece header '" + std::string(1, field)
                        + ":' is not yet supported (Phase 3)";
                    return false;
                } else if (field == 'K') {
                    err = "mid-piece key change ('K:') is not yet supported";
                    return false;
                }
                // Other header fields silently ignored mid-body.
            }
            continue;
        }

        // Body content.
        if (!inBody) continue;     // before K:, ignore non-header text

        ensureDefaultVoice();

        double L_beats     = parseDefaultLengthBeats(hdr.defaultLength);
        double beatsPerBar = (hdr.meterNum > 0 && hdr.meterDen > 0)
            ? (4.0 * hdr.meterNum / hdr.meterDen) : 4.0;

        if (!parseBodyLine(line, *currentVoice, drumMap, L_beats, beatsPerBar, err))
            return false;
    }

    if (!hdr.sawK) {
        err = "missing required K: header (marks end of header / start of body)";
        return false;
    }

    // Aggregate.
    out.tempo = static_cast<double>(hdr.tempoBpm);
    out.timeSignature.clear();
    out.timeSignature.emplace_back(hdr.meterNum, hdr.meterDen);

    double maxEnd = 0.0;
    for (auto& v : voices) {
        for (auto& n : v.notes) {
            out.notes.push_back(n);
            maxEnd = std::max(maxEnd, n.startBeat + n.durationBeats);
        }
    }
    out.lengthBeats = maxEnd;

    std::sort(out.notes.begin(), out.notes.end(),
        [](const ComposerOutput::Note& a, const ComposerOutput::Note& b) {
            if (a.trackName != b.trackName) return a.trackName < b.trackName;
            if (a.startBeat != b.startBeat) return a.startBeat < b.startBeat;
            return a.pitch < b.pitch;
        });

    return true;
}

}  // namespace

bool ABCParser::parse(const juce::String& input,
                       ComposerOutput& out,
                       std::string& err) {
    out = {};
    err.clear();
    return parseImpl(input.toStdString(), out, err);
}
