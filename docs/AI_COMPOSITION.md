# Improving AI composition

**Status (2026-05-04):** sub-project just opened. The current chat-based composition flow (V2 notation → `compose()` → MIDI regions) ships and works mechanically, but the music it produces is, in Will's words, "quite childish" and sometimes plainly bad. This doc tracks the effort to make it good.

No hurry. We're in the wait-and-see phase of the friend ship; the rest of the app is in good shape; this is the deepest unsolved quality problem in the product. Other small fixes / features will likely happen alongside.

## Where it sits today

What's in code (see `docs/COMPOSER_INTEGRATION.md` for the shipped detail):
- A `compose(notation)` Lua function that takes V2 notation and writes regions onto the user's tracks.
- A V2 grammar with header (tempo / time-sig / key / feel / tracks) + bar blocks + per-voice events (notes, chords, rests, ties).
- An ~80-line "Composing music" section in `runtime/SYSTEM_PROMPT.md` covering the LLM's posture, "make it not flat" guidelines (motives, voice leading, parts that interact, small surprises), and the V2 grammar reference.
- The LLM (Claude Sonnet 4.5 via the Lambda chat proxy) decides when to compose and emits the notation directly.

What it produces today: workable structurally, recognizable as music, but melodically thin, harmonically safe, rhythmically square. Often misses obvious stylistic cues, doesn't develop motives, doesn't surprise. Not embarrassing — just clearly not good.

## Near-term steps (only these are committed)

We're deliberately not laying out phases 3+ until we've done the first two. Music-AI is moving fast and a long-range plan now would just be wrong.

### 1. Research

Careful survey of what else exists in this space, with similar product DNA — chat / iterative / human-in-the-loop AI composition, not "press generate and get a song." Some categories to look at:

- **AI music notation / iterative tools** — what Suno, Udio, MusicGen, AIVA, Boomy, etc. actually do under the hood. What's their output quality, where do they fail, what kind of user loop do they enable? Especially interested in tools where the user can edit / iterate, not just regenerate.
- **Notation-as-LLM-target work** — academic and indie work using LLMs to write music notation (ABC, MusicXML, MIDI text). What grammars work, what guardrails matter, what evals exist.
- **Magenta / Anticipatory Music Transformer / open music LLMs** — model-side prior art on music sequence generation. Whether any are usable as a pre-processor / post-processor to a general LLM's output.
- **Style transfer / completion / continuation** — research on "given this melody, continue it / harmonize it / arrange it" as distinct from "generate from prompt."
- **Existing DAW + AI integrations** — Logic's Session Players, Ableton's various tools, Captain plugins, Hookpad — what they do well, where they hit walls, what their UX looks like.

Output of this step: a research note in this doc (or a sibling) summarizing the landscape, what the meaningful axes of variation are, what's surprising, what isn't worth pursuing.

### 2. Brainstorming

After research, we sit down and brainstorm what we could try. No commitments before that conversation. Likely outcomes might include better prompting, model choice, multi-pass generation, post-processing, hybrid LLM+specialist-model pipelines, evals — but none of that is decided.

## What's deliberately not in scope yet

- Specific implementation plans
- Effort estimates
- Sequencing
- Pre-commitment to a particular technical direction

We add those after step 2.
