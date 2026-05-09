# Improving AI composition

**Status (2026-05-08):** research phase complete; direction committed. Build plan lives in **`docs/AI_COMPOSITION_API.md`** (sub-sub-project: ABC notation + content CRUD layer, on dev branch `composition-abc`). This doc remains the research catalog and "where we landed and why" record.

The current chat-based composition flow (V2 notation → `compose()` → MIDI regions) ships and works mechanically, but the music it produces is, in Will's words, "quite childish" and sometimes plainly bad. The redesign aims to make it good.

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

#### Deep-dive findings (from full-paper reads, 2026-05-04)

Going past abstracts on the most relevant prior art.

**[DAWZY](https://arxiv.org/html/2512.03289)** — three-layer architecture (User Interaction / Processing / Execution). Three MCP tools exposed: state query (enumerates tracks/items/FX/routing), FX parameterization (converts dB/ms to ReaScript ranges), and beat generation. Critically: **DAWZY does *not* try to make the LLM emit musical notation directly.** "Beat generation" calls Meta's MusicGen-small (audio) locally; melody input is via "Hum-to-MIDI" using BasicPitch (audio → MIDI). The LLM (GPT-5) only writes ReaScript Lua to manipulate state. Open-source baselines (Qwen3-Coder-480B) "achieved only 25–50% success, often failing due to hallucinated or invalid ReaScript functions and mis-indexed parameters" — same class of failures we see in our compose loop. User study: 21 participants, MOS scores out of 5: Enjoyment 4.48, Learning 4.38, Collaboration 4.29, Usability 4.14, Control 3.81. Stated future work: "training or finetuning models specifically for more accurate music software scripting" — they hit the wall we'd expect to hit too. **Implication for us**: even the most architecturally-similar paper deliberately punts on "LLM emits musical notation" and routes around it via specialist audio models. We're more ambitious than they are on this axis, which means we're more exposed to the LLM's compositional weakness.

**[ComposerX](https://arxiv.org/html/2404.18081v1)** — six-agent pipeline operating on ABC notation with GPT-4-Turbo:
- **Group Leader** decomposes user prompt into subtasks.
- **Melody** generates single-line melodies.
- **Harmony** adds harmonic + contrapuntal elements.
- **Instrument** selects/assigns instruments to voices.
- **Reviewer** evaluates across Melodic Structure / Harmony+Counterpoint / Rhythmic Complexity / Instrumentation+Timbre / Form+Structure.
- **Arrangement** compiles final ABC.

Communication is a 4-stage loop: Plan → Compose → Review-and-revise (Melody → Harmony → Instrument in order) → Arrangement. Each agent gets a role-prompt with music-theory rules baked in (e.g. melody agent: "movement of the notes should primarily consist of stable intervals such as whole steps, thirds, and fifths").

Quantitative wins: format correctness 98.2% (GPT-4-Turbo), human listening preference vs single-agent baselines: 0.77 vs CoT, 0.68 vs Role, 0.6 vs Original prompting, 0.57 vs ICL. Multi-agent outputs are ~3× longer than single-agent (1005 chars vs ~340) — the agents collectively produce more music, not just better music. Turing test: 32.2% of multi-agent outputs perceived as human (vs 55.4% for actual humans). Cost: ~$0.80/composition with GPT-4, ~$4.34 per "good case" (18.4% success rate considered "good").

Stated limitations: subtle musical expression (emotional depth, dynamic contrasts), natural-language-to-notation gap (review feedback often lost in translation), instrument range violations (notes outside an instrument's playable range), voice alignment in polyphonic passages (linear text doesn't naturally accommodate parallel voices), cadential resolution (pieces feel unfinished). **Implication for us**: ComposerX's multi-agent pattern is *directly portable* to our setup as a single high-level Claude call that internally orchestrates sub-prompts. The reviewer-revise loop in particular is a known constitutional-AI / self-refine pattern. We get most of the architecture for free with prompt engineering — no model changes.

**[ChatMusician](https://arxiv.org/html/2402.16153v1)** — open-weights specialist (LLaMA-2-7B base + continual pretrain on 4.16B-token MusicPile, ~18% music scores). Uses ABC via the standard LLaMA tokenizer (no custom music tokens — they intentionally treat music as "a second language"). Beats GPT-4 on format correctness (99.6% vs 94.6%), and human listeners preferred its output over GPT-4 in 76% of cases (p = 2.7×10⁻⁶). Open weights at the GitHub repo, fine-tuned via LoRA. Caveat: heavily Irish-music-biased due to dataset composition, and limited diversity in instruction-following data. **Implication for us**: a specialist symbolic-music LLM exists, is open, runs on consumer hardware, and outperforms GPT-4 at music notation specifically. Could be invoked as a subagent / tool from Sonnet-via-Lambda when the user asks for music. Trade-off: extra system to deploy + unfamiliar voice; gains on musicality but loses Sonnet's compositional reasoning.

**[MIDI-LLM](https://arxiv.org/html/2511.03942v1)** (Nov 2025) — Llama-3.2-1B finetuned with 55K MIDI-specific tokens on top of the original 128K vocab. Each note = 3 tokens (onset time, duration, joint instrument-pitch). Beats prior text-to-MIDI baselines on FAD (0.173 vs 0.818) and CLAP (22.1 vs 18.7); generates 2K-token sequences in 10s vs 47s. Open weights on Hugging Face, [live demo](https://midi-llm-demo.vercel.app). Honest about weaknesses: text has minimal influence during infilling (the surrounding MIDI dominates), and ablation showed music-specific pretraining data gave "no noticeable change" vs general FineWeb-Edu. **Implication**: small specialist models for text→MIDI are mature enough to be useful as tools in a hybrid pipeline.

**[NotaGen](https://arxiv.org/html/2502.18008v5)** (Feb 2025, hierarchical GPT-2, 516M params) — most novel contribution is **CLaMP-DPO**: reinforcement learning via Direct Preference Optimization using CLaMP 2 as an automated music-quality judge. Three-stage: pretrain on 1.6M ABC pieces → finetune on 8,948 high-quality classical → DPO with CLaMP feedback. Beats baselines in human A/B tests; DPO measurably improves "musicality" without requiring human labels. **Implication for us**: even if we can't fine-tune Sonnet, the *eval-driven iteration* pattern is reusable — generate N candidates, score them with an automated judge (CLaMP or similar), pick the best, surface to user. Quality boost without model training.

**[Anticipatory Music Transformer (AMT)](https://crfm.stanford.edu/2023/06/16/anticipatory-music-transformer.html)** (Stanford/CMU 2023) — the model behind both MIDI-LLM's tokenization and Hookpad's Aria copilot. Trained to **infill parts of an existing draft composition** rather than generate end-to-end. 128M / 360M / 780M sizes; 360M open. The "anticipation" insight is structural: the model learns to fill gaps in music given context, which maps directly to how composers actually work (sketch a melody, fill in harmony, etc.). **Implication**: every iteration mode in our app — "extend this," "harmonize this," "add drums to this region" — is more naturally served by an infilling model than a from-scratch LLM. AMT or its successors are the right tool for those operations.

**[ABC notation v2.1 spec](https://abcnotation.com/wiki/abc:standard:v2.1)** — extracted the full primitive list. ABC handles: precise rhythmic notation (dotted, broken, tuplets via `(p:q:r)`), complex/modified key signatures, multi-voice composition with independent clefs and transposition, ornaments + articulations + dynamics (`!pp!`, `!trill!`, etc., extensible via `U:` user-defined symbols), lyrics with syllable alignment (`w:`), sophisticated repeat structures + multi-ending voltas, chord symbols above staff, grace notes, mid-tune key/meter changes via inline fields (`[K:G]`), and a macro system (`m:`) for compactness. Compression: ~288 tokens/song average — best of any text-based notation per ChatMusician's analysis. **Implication for V2 vs ABC**: ABC handles every musical primitive we'd want and is already in the LLM's training data. Our V2 currently has fewer primitives but is designed for prose-like authoring. The honest design question is: do we *evolve V2 toward ABC* (gain expressiveness, lose prose feel), *adopt ABC entirely* (fast win on expressiveness + LLM familiarity, lose what we built), *keep V2 for the chat surface and emit ABC under the hood* (best of both, more code), or *prove our V2 produces better music than ABC and double down* (haven't measured).

**[Hookpad's Aria](https://www.hooktheory.com/hookpad/aria)** — content-aware AI copilot inside Hookpad that suggests melodies / chords / both based on context, powered directly by AMT. The product showcase for what AMT enables: it doesn't generate from scratch; it offers in-context suggestions tied to what the user is already writing. The clearest "this is what good iterative AI composition feels like" reference in the wild.

**[LIA Plugin](https://liaplugin.com/blog/best-ai-tools-ableton-2026/)** — third architectural-twin (after DAWZY and us): browser chat → local bridge → Ableton, executes MIDI/effect/mix actions in the live session. "Your projects never leave your computer. LIA Bridge only sends control commands." Privacy-first, full creative control retained, MIDI editable. Free tier + Premium. Validates the chat-controls-DAW pattern is converging on multiple commercial entries.

**[MIDI Agent](https://www.midiagent.com/)** — VST plugin, BYOK across nine LLM providers (OpenAI / Anthropic / Gemini / DeepSeek / Grok / OpenRouter / Ollama / LM Studio / Hugging Face), explicitly recommends "starting new conversations frequently" because of "context rot" (their term for what we observed in our drum-track session). Internals are intentionally opaque; they don't disclose tokenization or prompting. Positions BYOK as a feature. **Implication**: BYOK + multi-provider support is a real product differentiator we should consider for the public beta.

**[Music-generation evaluation metrics survey](https://arxiv.org/html/2509.00051v1)** — categorizes the eval landscape:
- **Subjective**: Mean Opinion Score (MOS, 1–5), head-to-head A/B preference (HTH), Turing tests, listening tests with Likert scales for melody coherence / emotional impact / overall appeal.
- **Objective**: pitch-distribution KL divergence, entropy for diversity, Frechet Audio Distance (FAD), Wasserstein Distance, CLAP for text-alignment.
- **Cognition-level**: musical impression, autobiographical association, personal preference (recent extensions trying to capture perceptual experience).

The survey explicitly flags: "lack of interpretability and reliability of objective metrics undermines the evaluation's ability to draw meaningful conclusions, as widely used measures often misalign with human perception." **Implication**: there's no consensus eval. Anyone building serious AI composition has to invent their own loop. NotaGen's CLaMP-DPO is one example of grafting an eval onto training; we'd need our own subjective-listening protocol to know whether changes are improvements.

#### What we now believe (synthesis)

After the deep dive, several things look like real signal:

1. **Three architectural patterns are converging in the field.** (a) General LLM → DSL/notation → DAW (us, MIDI Agent, the prompt-side of DAWZY). (b) Specialist symbolic-music model → MIDI (ChatMusician, MIDI-LLM, NotaGen, AMT). (c) Hybrid: LLM orchestrates while a specialist model executes the music part (Hookpad uses AMT + UI; nobody yet combines a chat LLM with a specialist music model behind it). The hybrid pattern looks under-explored and matches our existing chat surface naturally — we could use Sonnet for *what to compose* and call AMT/MIDI-LLM/ChatMusician as a tool for the actual notes.

2. **DAWZY validates our architecture but also reveals the limit of "ask the LLM to write notation."** Their explicit choice to route MIDI generation through MusicGen + BasicPitch instead of asking the LLM to emit notation is a tell. Single-shot "general LLM emits notation" hits the same wall ComposerX documents (instrument range violations, voice alignment failures, weak cadences). We're hitting that same wall in our "childish" outputs.

3. **Multi-agent decomposition + reviewer-revise (ComposerX) is portable to us as prompt engineering.** No model training, no new infrastructure. A single Claude turn that internally orchestrates sub-prompts (plan → melody → harmony → review → revise → arrange) could close meaningful quality gap. Cost rises ~3× per composition; quality measurably wins in human listening tests.

4. **Eval-driven iteration (NotaGen-style) is reusable even without fine-tuning.** Generate N candidates with Sonnet, score with an automated judge (could be Sonnet itself in a critique role, could be CLaMP or similar), present best to user. The pattern is the win, not the specific judge.

5. **Anticipation / infilling is the natural model for iterative composition.** AMT-style "fill in this gap" or "extend this draft" is what composers actually do. Our current generate-fresh-region flow is a special case where the gap is the whole region; supporting partial-fill / continuation as first-class operations would unlock features (extend the bass line, harmonize this melody, alternate ending) that aren't possible today.

6. **ABC vs V2 is a live design question, not settled.** ABC has every primitive we'd want, is in Claude's training data, and is supported by an entire ecosystem of tools (renderers, players, converters to MusicXML/MIDI). Our V2's "prose feel" intuition is real but hasn't been measured against ABC for output quality. The honest path is to A/B Sonnet emitting V2 vs ABC on the same prompts and see what the listening preference is.

7. **The market has converged on a few patterns that we should be aware of:**
   - Iterative refinement is table stakes (Udio inpainting, Suno stem editing, Hookpad in-context suggestions).
   - BYOK across providers is a real feature (MIDI Agent leads here).
   - "Privacy: your project never leaves your computer" is a marketing point (LIA emphasizes).
   - Audio-first (Suno, Udio) and notation-first (us, MIDI Agent, etc.) are different products in different lanes; we don't compete with them.

#### Open questions for the brainstorm

- Do we add a multi-agent pipeline (ComposerX-style) inside our Claude calls, or keep single-shot and improve via better prompting?
- Do we evolve V2 toward ABC, swap to ABC entirely, run both, or invest in measuring V2 vs ABC first?
- Do we integrate a specialist symbolic model (AMT, ChatMusician, MIDI-LLM) as a tool, or stay LLM-only?
- Do we add infilling / continuation as a first-class operation alongside generate-from-prompt?
- Do we build an automated judge for eval-driven iteration, or rely on user feedback as the only signal?
- Do we add BYOK (and multi-provider) as a public-beta feature?

These are decisions for the brainstorm step, not now.

#### What's still on the gap list

Mostly tightened up since first pass. Remaining gaps:
- **Hands-on quality assessment** of the commercial tools — claims vs reality on actual listening. Worth doing before committing to any direction. Free trials of LIA / Hookpad / Suno / a couple others, generate the same kind of brief on each, compare.
- **Specifics of how Hookpad integrates AMT** — they're the one product that has a specialist model + UI working together, so studying their UX for the integration pattern would be informative.
- **Anthropic-specific prompting** for music tasks — searches surfaced general prompt-engineering best practices but nothing music-specific. Direct experimentation would tell us more than further searching.

#### Notation-system note (per Will, 2026-05-04)

V2 emerged from much experimentation and the goal of making AI-emitted scores "more like writing prose." That insight — that the *grammar* shapes the LLM's compositional behavior, not just its ability to parse — is load-bearing and distinctive vs both ABC (designed for human readability of folk tunes) and MIDI-token formats (designed for model efficiency). Future expressiveness work needs to balance: (a) keep the prose-like authoring vibe, (b) add musical primitives V2 lacks (articulations, ornaments, dynamics curves, multi-staff, lyrics, free-meter passages), (c) avoid bloating the grammar to the point where the LLM's output gets format-bound rather than music-bound.

This is its own design problem and probably wants its own focused pass — not just "add more tokens to V2." The deep-dive findings above add a concrete next move: **measure V2 vs ABC for output quality on the same prompts**, treating it as a falsifiable hypothesis that V2 is doing something ABC isn't.

### 2. Brainstorming

After research, we sit down and brainstorm what we could try. No commitments before that conversation. Likely outcomes might include better prompting, model choice, multi-pass generation, post-processing, hybrid LLM+specialist-model pipelines, evals — but none of that is decided.

## What's deliberately not in scope yet

- Specific implementation plans
- Effort estimates
- Sequencing
- Pre-commitment to a particular technical direction

We add those after step 2.
