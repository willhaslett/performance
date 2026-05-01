# Perfuce — Product Plan

**Status:** Living draft. Will go stale; that's the point — read it every quarter, push back on anything that doesn't ring true anymore. As of 2026-05-01.

This document is for product thinking, not implementation. It's how we keep ourselves honest about what we're building and, more importantly, what we are not. Every time someone files a feature request and the answer feels obvious, this is the doc to check it against.

---

## What this doc is (and isn't)

It is: a positioning statement, a description of the user we're building for, and a small set of bets we're making about where their problems aren't being solved. It's the test we use to say *yes* or *no* to a feature.

It isn't: a roadmap, a feature list, or a marketing brief. The roadmap lives in CLAUDE.md's backlog. Features earn their way in by passing the smell tests at the end of this doc — not by appearing here.

---

## Who we're building for

A solo musician who performs live in front of a small audience, alone with a laptop, a MIDI controller, maybe a microphone or instrument input, and ideas. Some specific things about this person:

- They are technically literate enough to set up a USB interface and load a plugin, but they do not write code, do not enjoy reading manuals, and treat their computer like an instrument: it should respond, not be configured.
- They've outgrown a Boss RC-505 or similar hardware looper because they want richer sounds (real piano patches, synths, layered textures) but they haven't moved into Ableton because every Ableton rig feels like an engineering project.
- They will spend an evening figuring out a binding once and then expect it to *stay set up forever and never surprise them*. Reliability is the most important feature; latency is the second; expressiveness is the third.
- They are not trying to mix a record. They are trying to make something happen in front of people.

This person exists. They are why we are building this.

---

## What's broken about their day today

Every time a competent solo performer tries to assemble the rig we want them to have, the same things go wrong:

- **MIDI mapping is a nightmare.** Most DAWs treat MIDI learn as a per-control single-shot, with no concept of named, parameterised actions. So the performer ends up with 47 individual mappings, no way to see them as a system, and a hard time changing a binding without breaking the show.
- **Performance setup is design-time work in a tool built for design-time.** Logic, Ableton, MainStage — all of them want you to "build a project," then "perform from the project." The boundary between editing and performing is a constant context switch.
- **Hardware loopers don't grow.** A Boss RC-505 is a beautiful thing for vocal performers; it falls apart the moment you want to layer a real piano sound, a synth pad, and a beat with different feels.
- **Software loopers (Ableton's track-arming, Mainstage) require expertise to work at all.** They reward studio-time learning and punish stage-time mistakes.
- **AI tools, when they exist for music, are aimed at producers making finished tracks** (Suno, Udio). They aren't aimed at a performer who needs a four-bar pad they can drop into their loop right now.

Each of those is a real problem. None of them have a great solution. There is a real gap between "Boss pedal" and "Logic project," and the gap is bigger than the existing tools admit.

---

## The bet

We are building a small, opinionated tool that does four things well, and explicitly does not try to be a DAW:

1. **Production, just enough.** A producer pane that handles the recording, basic arranging, and basic mixing a performer needs to assemble the material they'll perform with. Not a finishing studio — a sketch surface.
2. **Looping that actually works for a multi-source performer.** The Looper pane. Boss-pedal ergonomics, software flexibility, no hardware ceiling.
3. **AI composition that produces useful starting material on demand.** Not "make me a song." "Give me a four-bar pad in F that sounds like Eno." The chat pane with the composer-mode toggle.
4. **Performance setup that's actually easy.** The mapping system, the action algebra, the chat-driven binding creation. This is the differentiator nobody else has.

If we do these four things and stay disciplined about not doing anything else, we have a real product.

---

## What "just enough" production means

We are not chasing Logic on production. The list of features serious producers actually use is in the thousands, the polish required to compete is enormous, and the workflow is fundamentally a different intent than ours (engineering a record vs. preparing for a show). So we have to draw a clean line.

**Yes, we do this:**
- Record audio and MIDI tracks against a click. Clean takes, undoable.
- Edit at the region level: move, split, copy, loop, mute, basic gain.
- Per-track plugin chain with at least one instrument and a few effects.
- Per-track mute, solo, level, pan, simple sends.
- Tempo and time signature for the project.
- Bounce a stereo file when you need to show somebody.
- Quantize that snaps to global beats and doesn't lie.
- Punch in / out, region recording in cycle.

**No, we do not do this:**
- A piano roll editor. (You'll record cleanly or re-record.)
- An audio waveform editor. (Trim with region edges; for surgical work, edit the file in something else and reload.)
- Automation lanes per parameter beyond the basic morph/fade verbs we already have.
- Per-region quantize percentage / swing / groove templates / humanise.
- Track freezing, track folders, group editing.
- Comping, takes-as-versions, advanced take management.
- Multi-out routing, surround, anything beyond stereo.
- A mixer that competes on visual sophistication. Faders, meters, sends. Done.

Every "no" above frees us to do the four bets well. Every time someone proposes adding one of the "no" items because it would be cheap, the answer is to point at the gap that gets closer to closing if we stay focused.

---

## The looper bet

Why this matters: solo performers are looping. They've always been looping. The hardware market for loopers (Boss, Headrush, Ditto, Pigtronix) is healthy because nothing else fits the workflow. But the hardware ceiling is real — sample-accurate sync to a click, multi-track layering with different sounds per track, the ability to *hear* and *see* what you're about to do before you commit, polished plugin sounds — none of that lives in a stomp box.

The reason most software loopers haven't won: they require studio-time understanding to perform stage-time. Ableton's session view, MainStage's set lists — they're designed by power users, for power users.

What we're building: Boss-pedal ergonomics on top of software flexibility. The performer thinks in **gestures** (tap to record, tap to overdub, tap to undo, tap to clear) and the system handles the rest. The first tap establishes the loop length. Subsequent gestures replace, overdub, undo. There is no "session view" to understand. There is no clip launcher to wire up.

If a Boss player tries our looper and within five minutes is doing what they were doing on the pedal, *plus* using their actual piano sound, *plus* using their actual synth pad, we've won them.

---

## The AI composition bet

Why this matters: every performer has a "I need a thing right now" moment. A four-bar pad. A simple kick pattern. A bass line in the right key. The current way to fix it is to either (a) have a giant library of pre-built clips (Splice, your own) or (b) play it yourself, badly, and re-record.

The AI tool space (Suno, Udio, AIVA, Boomy) is built for producing finished or near-finished tracks. They don't compose into your project. They generate and you consume.

What we're building: a chat surface with a composer mode that can take a request like "give me a four-bar pad in F minor, slow attack, swelling" and write actual notes onto an actual track in your actual project. The performer hears it, decides if it's useful, keeps or scraps it. The AI is not the artist; it is a really fast session player who shows up with material when you ask.

This is genuinely different from the rest of the AI music space because the unit of output isn't a song — it's a region in a project that the performer continues to own and shape.

There's a second-order effect we haven't fully exploited: the AI also knows the project. So "harmonize this melody," "give me a pad that fits what's already on the keys track," "extend this loop another four bars in the same feel" all become real requests. That's a category of tool that doesn't exist anywhere.

The risk: AI generation quality is not there yet for most genres. We have to be honest about which musical idioms it serves well and which it doesn't, and not oversell.

---

## The performance-setup bet — this is the moat

Look at how a serious solo performer assembles a rig today. They:

- Set up a Logic / Ableton / Reaper project with their tracks and plugins.
- Spend hours creating MIDI mappings, one control at a time.
- Build macros or scripts to coordinate things across tracks.
- Wire it all together with a custom Stream Deck setup or a hardware pedalboard.
- Find out at the show that one of the bindings is off-by-one and play through gritted teeth.

The reason this is the moat: nobody is building tools for *the act of setting up a performance rig*. DAWs treat it as an afterthought. MainStage exists but is showing its age and doesn't reach the levels of expressiveness modern performers want. Hardware solutions don't compose with software in a sane way.

What we're building, that already exists in some form:

- **Named, parameterised actions.** "Fade out track X over 8 bars with this curve" is one thing the performer thinks about. We model it as one thing — an action — not a stack of MIDI events.
- **Compositional actions.** "Crossfade from track A to track B" is a parallel composition of fadeOut + fadeIn. The performer doesn't have to understand that; they just see "crossfade" and bind a pad to it.
- **Chat-driven binding creation.** "Make CC 22 trigger a fade to silence on the pad track over 4 bars." Gets created. Persisted. Works at the show.
- **Score steps.** A sequence of named state transitions ("verse → chorus → drop") that the performer steps through with a single button. No hunting for the right control mid-song.

What we still need to build out, that lives in this space:

- A bindings dashboard that shows the current rig at a glance — every control, what it does, when it last fired. So the performer can debug their setup without remembering what they wired up six weeks ago.
- Friction-free binding creation from inside the chat surface, so a performer can describe what they want in English and get a working binding committed.
- Clear visual / audible affordances during a show so the performer knows what's about to happen, what just happened, and what state things are in.
- Saved performance "modes" or "presets" — a one-click recall of a known-good setup.

This is the area where we have the chance to be genuinely better than anything else, including Logic. It's also the area we under-invest in if we let production polish or looper polish eat the calendar.

---

## What we are not — explicitly

Saying these out loud so we recognise them when somebody asks for them:

- We are not a finishing studio. If the user wants to mix a record, they should use Logic.
- We are not a notation editor.
- We are not a video tool.
- We are not a DAW for collaborators. There's one user.
- We are not a full sample / loop library. We integrate with what the user already owns.
- We are not a hardware emulation. We're a different shape than a Boss pedal; people who want a Boss pedal should buy a Boss pedal.
- We are not a multi-platform product yet. Mac, AU plugins, Logic-shaped audio expectations.

---

## Smell tests for features

When someone (a tester, a friend, ourselves) suggests a feature, run it through these:

1. **Does it serve the bet?** Production-just-enough, looper, AI composition, or performance setup. If it doesn't fit one of those four, it probably doesn't belong.
2. **Is the performer's hands busy when they need this?** If yes, the feature has to work as a binding, not a button.
3. **Does it require the performer to remember something at show time?** If yes, redesign until it doesn't.
4. **Could a hardware pedal do this already?** If yes, we should match its ergonomics, not add complexity above it.
5. **Would Logic users notice it's missing?** If yes — and the answer is "of course, Logic has had it for 20 years" — then *no*. We're not Logic.
6. **Does it get cheaper or more expensive with our other bets?** Features that compose with the others (e.g. AI generates a region → looper plays it → bound to a pedal) are weighted up. Features that exist in isolation are weighted down.
7. **Is the smell of "I'd be embarrassed for a Logic person to see this"?** If yes — *yes*. We have to be embarrassed in the right places. Resist the urge to fix that embarrassment by adding the feature; either make our absence intentional or accept the missing piece as out of scope.

---

## The "aha" moment

What we want to happen the first time a real performer opens our app:

1. Within five minutes, they've made a noise — connected MIDI, played their controller, heard a sound out of a real plugin.
2. Within twenty minutes, they've recorded a loop in the looper, overdubbed onto it, and undone the overdub.
3. Within an hour, they've bound a pedal to one of our actions and felt it work.
4. The next time they open the app, everything is exactly where they left it.

If those four things happen, they'll come back. If any of them break, they won't.

---

## How we use this doc

- Re-read every quarter. If something here is wrong, fix it then. If something is right but we've drifted, course-correct then.
- Cite it in feature discussions. "Per the product doc, we don't do X" is a valid argument.
- Cite it in cuts. "Per the product doc, this feature serves none of the bets" is also valid.
- It's not the only doc — engineering rules live in CLAUDE.md, architecture lives in `docs/ARCHITECTURE.md`. This is the why-we-bother doc.
