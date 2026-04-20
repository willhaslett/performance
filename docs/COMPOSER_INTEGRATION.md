# Composer integration — investigation

**Status:** investigation only; no design decisions yet.
**Date started:** 2026-04-20.
**Source project:** `~/ideas_and_projects/dawai/` (standalone, git-tracked, phase 11 "prompt & workflow tuning").

---

## What dawai is, in one paragraph

dawai is a conversational composition tool that produces **multi-track MIDI**. The user and an LLM work together, through chat, on a piece of music. Creative decisions accumulate in a **piece document** (markdown). The LLM writes **regions** — 2–8 bar fragments in a custom text notation — which a **compiler** translates to MIDI. Output is `.mid` files that can open in any DAW. The tool runs locally: Python FastAPI backend, React web frontend, Anthropic Messages API for the agent, FluidSynth + GM soundfont for in-app playback.

It is emphatically **not** a "press generate and get a song." It's a creative-partner loop built around *the user deciding what's good*. The LLM's role is to generate candidates, maintain coherence across a complex document, and translate between abstraction levels ("darker" → lower register, minor mode, sparse voicing). The user curates.

---

## Why this is worth pursuing *inside* Performance

Will's own framing, from CLAUDE.md:

> Previous standalone iterations produced toy/child-like output and were abandoned ("polishing a stone and hoping for a ruby") partly because a written-once-listened-once surface has no escape hatch — weak output = finished product. In a DAW the composer's output becomes a *seed* that the session's tools can evolve (layer, morph, edit, re-perform), lowering the bar from "how good is this take" to "how good a starting point did it give the user."

Three concrete amplifiers Performance brings that dawai-standalone doesn't:

1. **Real instruments.** dawai currently plays through FluidSynth + MuseScore General — a GM soundfont that sounds anonymous no matter the notes. The same MIDI through Dexed, Surge XT, or Airwindows-processed audio sounds *intentional*. Bundled plugin pack (shipped 0.0.3) is the enabler here; this is why that work happened first.
2. **The existing environment.** A generated MIDI phrase isn't a finished product; it's a starting take that the user can morph, layer, bounce, cycle over, transpose, assign to a different patch. Every Performance feature is a lever on the output.
3. **Shared chat surface.** Performance already has a working Claude chat pane, a chat-proxy Lambda, streaming SSE, and tool-use plumbing. The composer doesn't need its own AI infrastructure — it slots into what's there.

---

## What dawai ships today

### Core artifacts (candidates for porting)

| Asset | Size / shape | Portability |
|---|---|---|
| `runtime/CLAUDE.md` — composer system prompt | ~400 lines; defines roles (creative partner / composer / editor / arranger), quality checklist, feedback-translation guide | Direct lift. Worth treating as the most valuable asset dawai produced. |
| `app/compiler.py` — v2 notation → MIDI | 443 lines of Python + `mido`; swing-aware; handles bars, chords, rests, ties, drums, dynamics, tied notes | Runs as a subprocess in the current architecture. **Porting to C++ is non-trivial** — Mido is the heavy lift. Three options: embed Python, run as sidecar, port to C++. |
| v2 notation format | Text, bar-grouped multitrack, fixed duration vocabulary (w/h/q/8th/16th...), named drum hits, velocity keywords | Pure syntax. Reusable unchanged. |
| Piece document schema (`piece.md`) | Markdown with Overview / Narrative / Tracks / Regions / Concepts / Decisions / Arrangement / Processes | Schema portable; whether Performance *needs* a separate "piece" concept (vs. using our "song") is an open design question. |

### Backend code (less obviously portable)

- `app/conversation.py` — Anthropic Messages API wrapper. **Performance already has an equivalent** (`src/api/ClaudeClient.cpp` + chat proxy Lambda). Won't be ported; our plumbing supersedes it.
- `app/server.py` — FastAPI. Not needed in-process.
- `app/piece_parser.py` — parses piece.md into typed structures for the GUI. Same schema work would be needed in C++ if we keep piece documents.
- `app/audio.py` — FluidSynth rendering. **Not needed at all** — we render through our own plugin hosts.

### Frontend

- React + TypeScript two-column layout. Piece Browser / Chat / Quick Actions / Player. Not directly portable (Performance is JUCE/C++). The *pattern* of "chat + piece-doc view + playback transport" is information, though.

---

## Integration shapes (four options, ranked by scope)

Ordered from minimal port to maximal port. Not mutually exclusive — (A) and (B) can be incremental steps toward (C) or (D).

### A. Composer as a chat tool — "add a region of 8 bars of bossa nova on track 2"

The smallest possible integration:

- Take dawai's composer prompt (or a compressed version) and add it as an **instruction block** the existing `perf` chat can invoke when the user asks for music.
- Add a Lua function `composeRegion(trackName, bars, intent, ...)` that:
  1. Calls Claude (same chat-proxy path) with the composer prompt + intent.
  2. Gets back v2 notation.
  3. Runs it through the compiler.
  4. Imports resulting MIDI into the target track as a new region at the requested beat range.
- No piece document, no separate UI. Just a richer output of the existing chat.

Effort: **small.** A week-ish, mostly wiring + compiler hosting.
Ceiling: **medium.** One-shot output, no iteration framework beyond "ask again." dawai's whole architectural insight is that the piece document is what makes repeated iteration coherent — without it we'd be back at "kindergarten-level" quality.

### B. Composer pane — like dawai's GUI, inside Performance

- Dedicated pane (⌘-something) with: piece list on the left, chat + audition transport in the middle, maybe a regions/arrangement view on the right.
- Piece documents persisted under `~/.config/performance/composer/pieces/<id>/` (or wherever makes sense).
- The chat here is composer-scoped (uses the composer system prompt) — distinct from the general Performance chat pane.
- Audition via "drop this region into a temp track and play" — using Performance's existing plugin hosts.

Effort: **medium-large.** Real UI work. Porting piece_parser to C++. Probably still calling compiler.py as a subprocess for the first iteration.
Ceiling: **high.** Gets dawai's full architectural benefit inside Performance's richer playback environment.

### C. Song-is-piece-document — composer writes into the active Performance song

Rather than maintaining a parallel "piece" concept, the active Performance song *becomes* the piece document:

- Tracks in dawai's sense ≈ Performance tracks.
- Regions in dawai's sense ≈ Performance MIDI regions.
- The piece document's narrative / concepts / decisions live as song metadata (or in a side file).
- Composer chat operates on the current song directly: "add 8 bars of bossa nova bass starting at bar 9" → writes a region into the current song.

Effort: **large.** Requires unifying two state models. Some concepts don't map 1:1 (dawai's "Processes", "Concepts" don't have direct equivalents).
Ceiling: **highest** *if it works.* Everything is in one place; no mode-switching; full Performance affordances on every composed note. Also highest risk of conceptual contortion — dawai's piece model might not fit Performance's song model cleanly.

### D. Keep dawai standalone, wire Performance as its playback target

Inverse direction. dawai continues to exist as a separate tool; Performance becomes its playback backend (replacing FluidSynth). Maybe via a local IPC or file-watch.

Effort: **small to medium.** But this is really "improve dawai by using Performance as a soundfont" — doesn't bring the composer into Performance.
Ceiling: **low for this repo's purposes.** Doesn't accomplish the 0.1.0 goal of "a composer feature inside the beta."

---

## Load-bearing open questions

1. **Compiler language.** The compiler is 443 lines of Python using `mido`. Four paths:
   - **(i) Embed Python** via pybind11 or Python.framework in the bundle. Adds ~20 MB, opens a can of version-skew worms.
   - **(ii) Subprocess.** Ship a small `perf-compile` binary (PyInstaller-frozen), invoke via fork/exec or spawn. Simple; adds 30–50 MB to the bundle; tolerates version skew.
   - **(iii) Port to C++.** Direct translation using `juce::MidiFile`. The logic is mechanical (parse tokens, emit MIDI events); not algorithmically hard. Maybe 1–2 days. Keeps the bundle pure C++; no runtime dependencies; easier to debug.
   - **(iv) Port to Lua.** Since we already have Lua embedded, a Lua implementation integrates trivially with the existing action algebra. More interpreter overhead but perfectly adequate for compile-time.
   - Best answer probably depends on how much we expect the compiler to evolve. If it's stable, port once. If it's evolving, subprocess.

2. **Piece document — do we need one at all?**
   - The dawai hypothesis (validated in their Phase 8) is that the piece document is *what* makes multi-turn composition produce coherent output. Without it, the LLM has no persistent context across edits.
   - But Performance already has a song with persistent state. The song knows its tracks, regions, current tempo, key. Is that enough compositional context, or do we need dawai's extra dimensions (Narrative, Concepts, Decisions, Processes) for quality?
   - Testable hypothesis: do A (small integration, song is the context) first. If output quality is kindergarten-level, the piece document is load-bearing and we step up to B or C.

3. **Two chat panes or one?**
   - Performance's current chat is general-purpose: "make a reverb bus," "fade this track out," etc. It uses the general `perf` system prompt.
   - dawai's composer prompt is long and specific. Mixing them into one prompt dilutes both.
   - Likely answer: separate pane, separate system prompt, **same bearer/Lambda/token-cap plumbing**. The infrastructure is shared, the voice isn't.

4. **Token costs.** Composer sessions are long. Each compose turn involves the full piece document + the conversation history. Our current chat quota (100k input / 25k output per install per month) is tuned for short helpful exchanges. A composer user could burn through that in an hour. Options: higher cap for installs with composer active, or usage-based opt-in ("this is a composer session, tokens used more aggressively, confirm?"), or dawai-style *local* Claude Code invocation (which free-tier users already have).

5. **Cold-start UX.** New user opens the composer pane for the first time — what do they see? dawai's current answer: "How can I help you create music?" with optional resume-existing-piece. That's probably still right, in whatever pane we build.

6. **Audition flow.** The speed of the compose → hear loop matters a lot. In dawai today: agent writes → auto-compile → FluidSynth render → browser audio element. In Performance: agent writes → (compile) → write MIDI into a temp track → user hits play. The temp-track approach is natively fast but potentially clutters the session. Worth thinking about a "scratch track" concept.

---

## Questions I can't answer without Will

- What is the *quality floor* we need for 0.1.0? Is "composer produces a listenable bossa nova A-section you can keep iterating on" the bar, or does it need to be "composer writes a finished 32-bar chart"?
- Is the composer meant to be **the tester's first experience** or a **power-user surface** they'll find after basic exploration?
- Does the composer need its own presence in the Sidebar / Help / perfuce.com copy, or is it a chat feature that's discoverable through usage?
- How much of dawai's own phase 11 work ("prompt & workflow tuning") is settled vs. still in flux? If the composer prompt is still changing weekly over there, that argues for making it bundled-data (like `runtime/CLAUDE.md`) so we can refresh it in isolation.

---

## Recommended next step

Don't write code yet. Have a conversation with Will about:

1. Quality floor for 0.1.0.
2. Whether a separate composer pane feels right, or whether he'd rather try a tool-in-existing-chat approach first.
3. Compiler language choice (subprocess is the path of least resistance for a first test; C++ port is the path of least resistance for long-term maintenance).
4. Whether to treat a Performance song as the piece document, or add a parallel piece concept.

Then pick an option (A / B / C), scope it into steps, add a second section to this doc titled `Design` with the actual plan, and execute.

---

# Design

*Decisions made 2026-04-20 after the conversation the investigation recommended. Values here are load-bearing; the phased plan below follows from them.*

## Decisions

**1. Shape — an A+B hybrid.** Single ChatView with a **latching "compose" mode**. When the toggle is on: the Lambda swaps in the composer system prompt and exposes a `compose` tool; output lands directly on the Project's tracks via the normal StateAPI write path. When off: back to the general `perf` chat. Not a separate pane — one UI, two modes, reusing all the existing chat plumbing (chat-proxy Lambda, bearer, token caps, SSE streaming).

**2. No MIDI file intermediate.** dawai's pipeline goes notation → MIDI → FluidSynth → .wav. Ours goes **notation → StateAPI writer → Project region**. The app plays the result directly because it's already native state. This halves the compiler port (parse-to-MidiFile is the heavier half) and collapses "compose / hear" into one step — there's no separate render to wait on.

**3. Keep the v2 notation format — but abstract it.** The notation format (bar-grouped multitrack, fixed duration vocabulary) is dawai's most settled asset, tuned over months for LLM training fit and token efficiency (`beat 1 q mf` is ~4 tokens vs. ~20 for the equivalent `addNote(...)` Lua call). But we're not sure it's optimal, so the parser is pluggable — one `NotationParser` interface, a V2 implementation as first subclass, swap-in-place for future experiments.

**4. "Provisional" = the existing undo machinery.** Composed output lands as a normal region on a track. ⌘Z removes it. No special "accept / reject" visual mode for MVP; that's a polish question for after we see how the loop feels.

**5. A Project *is* the piece document — but with a small side file.** dawai's Narrative / Concepts / Decisions / Processes are conversation context, not music. They live in `~/.config/performance/projects/<projectId>/piece.md` alongside the Project. The audio / MIDI regions continue to live in our SQLite state. The composer chat reads + writes that `piece.md` as the persistent context across sessions. If quality is fine without it, we drop it — but the investigation suggests rich context is what elevates output from "kindergarten-level" to coherent.

**6. Quality floor for 0.1.0 — TBD.** Picked pragmatically by testing, not pre-specified. The goal is "tester opens compose mode, describes what they want, gets something worth iterating on."

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  ChatView  [latching Compose toggle in header]         │
└─────────────────┬───────────────────────────────────────┘
                  │ POST (mode: "compose" | "perf")
                  ▼
┌─────────────────────────────────────────────────────────┐
│  Chat Lambda — selects system prompt based on mode:    │
│    "perf"    → existing prompt                          │
│    "compose" → composer prompt (bundled as BinaryData)  │
└─────────────────┬───────────────────────────────────────┘
                  │ SSE stream with tool_use: compose(...)
                  ▼
┌─────────────────────────────────────────────────────────┐
│  ClaudeClient → LuaEngine.compose(tracks, beat, text)  │
│                                                         │
│  Lua binding → NotationParser.parse(text)              │
│              → ComposerWriter.apply(output, state)     │
│                                                         │
│  src/composer/                                          │
│    ComposerOutput.h     — canonical struct              │
│    NotationParser.h     — interface                     │
│    V2NotationParser.*   — first impl (dawai v2)         │
│    ComposerWriter.*     — writes canonical to state     │
└─────────────────────────────────────────────────────────┘
```

## Phased plan

Each phase builds cleanly on its own. Intended as small-enough-to-ship commits.

**Phase 1 — Abstraction scaffold (no behavior).**
Create `src/composer/` with `ComposerOutput.h`, `NotationParser.h` (interface), `V2NotationParser.{h,cpp}` (stub returning `not_implemented` error), `ComposerWriter.{h,cpp}` (stub). Wire into `CMakeLists.txt`. Build cleanly. Nothing user-visible yet; the point is a foundation that's trivially extensible.

**Phase 2 — V2 parser + Writer implementation.**
Port dawai's notation grammar to C++: parse header (tempo / time sig / tracks), parse bar blocks (`bar N | Chord | track lines`), emit `ComposerOutput`. Writer takes the output and, given a start beat + target tracks, creates a region on each track and populates MidiEventState. Tested via the existing unit-test suite with fixtures drawn from dawai's own test cases.

**Phase 3 — Lua `compose()` binding.**
One new Lua function: `compose(trackNames, startBeat, notation)`. Calls parser → writer. Errors surface via the existing silent-failure-fixed error path. Testable from `bin/perf` before any UI exists.

**Phase 4 — Lambda composer-mode prompt.**
Bundle dawai's `runtime/CLAUDE.md` (possibly trimmed) as a BinaryData resource in the app, sent in the chat request alongside a `mode: "compose"` field. Lambda picks the system prompt based on mode. Existing bearer / token-cap plumbing unchanged. Consider: separate token budget for compose mode, or shared — probably shared for v1.

**Phase 5 — ChatView compose toggle.**
A small latching toggle in the chat header (e.g. "Compose" button that stays pressed). Flipping it updates the mode field on subsequent requests. Visual affordance that the chat is now in composer mode. No other UI changes.

**Out of scope for this sprint:**
- piece.md side file + persistent compositional context (adds meaningful value but phase 1–5 are already a lot; try it without first).
- "Provisional" visual mode (colored notes, accept/reject buttons). Undo is the MVP safety net.
- Dexed / Surge XT preset selection by the composer. Default to whatever the track already has.
- Stem export / MIDI export. Can be added via existing bounce flow later.

## Open questions to revisit during implementation

- **Token budget**: compose sessions are heavier than regular chat. We may need to raise the monthly cap (currently 100k input / 25k output per install) or carve off a separate compose budget. Decide when we see real usage.
- **piece.md or not**: dawai's own phase 8 validated the piece document as load-bearing for quality. We're deferring it — if early tests produce kindergarten-level output with the same prompt, the piece document is the next lever.
- **Compose-mode tool surface**: for MVP, compose-mode has only the `compose` tool. Longer-term, it should probably also be able to read existing regions (to iterate on what the user already has), rename regions, move regions, etc. Scope after the write path works.
