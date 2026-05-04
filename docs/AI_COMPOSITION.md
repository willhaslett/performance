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

#### First-pass landscape sweep (2026-05-04)

Web search across competitor tools, academic systems, and DAW integrations. Brief catalog below; deeper individual investigations as we narrow.

**Direct structural analogues** — chat / NL → notation → DAW, iterative, human-in-the-loop. These are the closest to what we're doing:

- **[DAWZY](https://arxiv.org/abs/2512.03289)** — *most structurally similar to us.* NeurIPS 2025 paper. Open-source assistant for REAPER. Natural language → MCP tool calls → Lua ReaScript → DAW state mutation. Uses GPT-5 to interpret intent. Key principles match ours almost exactly: keep the DAW as the creative hub, emit reversible/atomic actions, refresh state before mutation, single chat box replacing hundreds of menu items. Validates our architecture hard. Worth reading the paper carefully for what they tried, what worked, what didn't.
- **[MIDI Agent](https://www.midiagent.com/)** — VST plugin embedded directly in DAWs (FL/Reaper/Logic/Cubase/Ableton/Bitwig/Studio One). Uses general LLMs (Claude / ChatGPT / Gemini) to generate MIDI from prompts. Lives inside the DAW like ours but as a plugin, not the host. Closest to "general-LLM-emits-notation" approach.
- **[MIDI Lab AI](https://www.midilab.ai/)** — browser-based, prompt-to-MIDI with embedded Piano Roll editor. "Infinite MIDI Engine" streams continuously to hardware/DAWs via Web MIDI. Iterative-by-streaming rather than iterative-by-conversation.
- **[Staccato](https://staccato.ai/)** — prompt-based MIDI generation up to 16 tracks / 32 bars at once. More batch-y than conversational.

**Audio-first generation** — different UX model from ours (one-shot generate-and-listen vs iterative-edit-in-DAW), but capturing market attention so worth knowing:

- **[Suno Studio](https://suno.com/hub/create-music-with-ai)** ($30/mo) — full browser-based DAW: timeline editing, 12-stem separation, **MIDI export**, real-time generation sliders for "weirdness / style influence / audio influence." Iterative loop: generate stems, build, export MIDI, separate, expand. Pushing toward DAW-replacement.
- **[Udio](https://www.tldl.io/blog/suno-vs-udio-comparison)** — main differentiator is *inpainting*: select a 2-second segment, describe what to change, only that section regenerates. Sessions interface for iterative timeline refinement. Audio-first but with the most iterative UX in this category.
- **[ImagineArt Music Studio](https://www.imagine.art/music-studio/ai-midi-music-generator)** — text/lyrics → MIDI tracks with vocals + instrumentals.

**DAW-native AI** (Apple, Ableton, FL):

- **[Logic Pro Session Players + Chord ID](https://www.musicradar.com/music-tech/apple-expands-logic-pros-ai-features-with-a-synth-player-and-a-personal-music-theory-expert-that-can-generate-chord-progressions-from-any-audio-or-midi-recording-that-you-play-it)** — Apple's stance is explicit: MIDI is the bedrock; AI assists rather than replaces. New "Synth Player" + "personal music theory expert" that turns any audio/MIDI into a chord progression. Notably *not* a chat interface — it's traditional UI affordances backed by ML. [Apple's interview](https://www.musicradar.com/music-tech/artists-dont-want-this-type-of-technology-to-replace-their-creativity-they-love-the-notion-of-it-helping-them-when-they-need-it-apple-explains-its-approach-to-implementing-ai-in-logic-pro-and-why-midi-is-still-the-bedrock-of-its-session-players) is worth reading for product philosophy alone.
- **[FL Studio 2025's "Gopher"](https://musictech.com/news/gear/fl-studio-2025-has-arrived-everything-you-need-to-know/)** — multilingual chat assistant trained on the FL Studio reference manual. Advice-only, not generative. Closer to a smart help system than a composer.
- **[Magenta Studio](https://magenta.withgoogle.com/studio/)** — Free Ableton plugin with 5 tools (Continue / Groove / Generate / Drumify / Interpolate) running Google's models on existing MIDI clips. Doesn't generate from prompts — operates *on* what's already there. Very different paradigm: continuation / refinement vs origination.
- **[Ricercar](https://dl.acm.org/doi/10.1145/3749893.3749961)** — Interactive composition system, MIDI in/out, "inspiration and attention curves" the user shapes. Iterative selection from system-generated suggestions. Academic but live.

**Academic / model-side prior art** — informs what's plausible to do under the hood:

- **[ComposerX](https://arxiv.org/pdf/2404.18081)** — multi-agent GPT-4-turbo: leader / melody / harmony / instrument / reviewer / arrangement agents collaborating to generate ABC notation. The multi-agent structure is interesting; results are claimed to outperform single-LLM approaches.
- **[CoComposer](https://arxiv.org/html/2509.00132v1)** — multi-agent collaborative LLM composition. Same broad direction as ComposerX.
- **[ChatMusician](https://arxiv.org/html/2402.16153v1)** — open-source LLM finetuned (LLaMA-2 base) on ABC notation, treating music as a "second language." A specialist alternative to using a general LLM with prompt engineering.
- **[MIDI-LLM](https://arxiv.org/abs/2511.03942)** (Nov 2025) — GPT-style LLM with 55,000 MIDI-specific tokens. Each note = 3 tokens (onset time + attributes). Generates 2K-token sequences in 10s vs 47s for prior work. Specialized text → MIDI model, not a general LLM.
- **[Anticipatory Music Transformer (AMT)](https://github.com/briansemrau/MIDI-LLM-tokenizer)** — the tokenization scheme MIDI-LLM uses; flexible, doesn't require beat-synchronized data.
- **[Teaching LLMs Music Theory with In-Context Learning](https://www.scitepress.org/Papers/2025/135061/135061.pdf)** — Chain-of-thought + 5-shot ICL gets general LLMs to ~70% on music knowledge. Suggests our prompt-engineering ceiling is real but underexplored.
- **[Polyphonic Music Composition via GPT-4](https://hajim.rochester.edu/ece/sites/zduan/teaching/ece477/projects/2023/QixinDeng_TianyiZhang_BoningWang_ProjectReport.pdf)** — early-ish (2023) study of GPT-4 generating polyphonic music. Ceiling probably higher with newer models.
- **[Lilypond + LLM analysis](https://beyondthepiano.jlmirall.es/2024/10/21/the-artificial-intelligence-of-large-language-models-llm-claude-and-gpt-and-their-ability-to-create-sheet-music-and-study-techniques-in-lilypond/)** — head-to-head Claude vs GPT generating LilyPond notation. Shows the format-choice axis matters.

#### What pops out — observations, not conclusions

- **DAWZY validates our architecture.** A NeurIPS-published system with the same shape (NL → tool calls → DAW mutation, iterative, human-driven) is a strong signal we're not on a weird path. Worth reading their evals + user-rating breakdowns.
- **Notation format is a real design axis with prior art.** Three families exist: (a) **ABC** (compresses well, well-documented, LLMs already know it from training), (b) **MIDI-token** (specialist models like MIDI-LLM, custom vocabularies), (c) **custom DSL** (our V2). Our V2's "more like writing prose" goal is structurally aligned with what ABC was originally designed for. We may have rediscovered ABC's strengths in our own grammar — worth comparing what V2 does well/badly against what ABC does well/badly. Especially for *future expressiveness* — ABC supports things V2 doesn't (slurs, articulations, ornaments, multi-staff, lyrics, chord symbols-as-text) that we'd eventually want.
- **Multi-agent decomposition is a credible quality lever.** ComposerX-style "different agents for different musical roles" claims real improvements over single-agent. Could fit our compose flow as a single chat-call that internally orchestrates subagents (melody → harmony → arrangement passes).
- **General LLM vs specialist model is a real fork.** ChatMusician / MIDI-LLM bake in music-domain knowledge by training on music data. Sonnet 4.5 is generalist with whatever music-related text was in pretraining. Specialist models likely produce more idiomatic music; general-LLM gives us a chat-everything-else surface for free. We currently chose general; the long-term answer might be hybrid (general LLM orchestrates, specialist model executes notation).
- **Iterative-edit beats one-shot-generate** in the user experience of nearly every category. Udio's inpainting, Suno Studio's stem editing, Magenta's "operate on existing MIDI," DAWZY's reversible atomic actions — they all enable iteration. Our compose-and-undo loop is in this lineage but lighter; richer iteration affordances (inpaint a region, regenerate a single track, re-harmonize an existing melody) are clear next-feature candidates.
- **Apple's product philosophy is informative.** Their public stance ("artists don't want this type of technology to replace their creativity; they love the notion of it helping when they need it") is exactly our positioning, validated at scale. Their UX choice — *not chat*, just smarter affordances + Session Players — is a distinct path we deliberately didn't take. Worth examining whether chat is genuinely the best surface or whether some tasks would be better served by direct affordances.
- **Audio-first vs notation-first is a market fork, not a feature gap.** Suno/Udio compete for "press generate, get a song" attention. We're in the "compose music *with* a person" lane. Apple/Logic are in the "smart MIDI affordances" lane. These are different products serving different intents.

#### What I haven't yet looked at

- **Quality of output, hands-on.** All the above is positioning + paper claims. Actually trying these tools (especially DAWZY, Staccato, MIDI Agent) and judging the music would change the picture.
- **Hookpad / Captain plugins / older indie tools** — pre-LLM era music-assistance tools that may have UX or prompt-engineering insights worth knowing.
- **Anthropic's own work on music tasks** — any prompting techniques Claude was specifically tuned on, or known weaknesses with notation generation.
- **Evals.** What does anyone use to measure "the AI made better music"? Looks under-served and would be useful to formalize.
- **The notation-format design space** in depth — read the ABC spec, compare expressively against our V2, identify the things V2 is missing that block expressiveness goals.

#### Notation-system note (per Will, 2026-05-04)

V2 emerged from much experimentation and the goal of making AI-emitted scores "more like writing prose." That insight — that the *grammar* shapes the LLM's compositional behavior, not just its ability to parse — is load-bearing and distinctive vs both ABC (designed for human readability of folk tunes) and MIDI-token formats (designed for model efficiency). Future expressiveness work needs to balance: (a) keep the prose-like authoring vibe, (b) add musical primitives V2 lacks (articulations, ornaments, dynamics curves, multi-staff, lyrics, free-meter passages), (c) avoid bloating the grammar to the point where the LLM's output gets format-bound rather than music-bound.

This is its own design problem and probably wants its own focused pass — not just "add more tokens to V2."

### 2. Brainstorming

After research, we sit down and brainstorm what we could try. No commitments before that conversation. Likely outcomes might include better prompting, model choice, multi-pass generation, post-processing, hybrid LLM+specialist-model pipelines, evals — but none of that is decided.

## What's deliberately not in scope yet

- Specific implementation plans
- Effort estimates
- Sequencing
- Pre-commitment to a particular technical direction

We add those after step 2.
