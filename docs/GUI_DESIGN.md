# GUI Design — Clean Sheet

Work-in-progress design thinking for the app's UX. Not a spec — a conversation artifact. Nothing here is locked in. The goal is to converge toward a vision that doesn't require rewrites but is headed toward a surprisingly good UI.

## User Journeys

The five things a user routinely does, in the order they discover them.

### 1. "I just want to play a sound."

First launch. They don't care about the architecture. They want to press a key and hear a piano. Everything between "open app" and "sound comes out" is friction.

**Today's journey:** open app → silence → hunt for Settings → find output device dropdown → select speakers → close Settings → realize there's no track → figure out how to add one → find plugin list → load instrument → play.

**The gold standard (Logic, Ableton):** open app → template with a piano already loaded → play.

**What to nail:** time to first sound under 60 seconds. Ideally under 10. This isn't about dumbing down — it's about not punishing curiosity. The power is still there; the on-ramp is just shorter.

**Open questions:**
- Do we auto-select the system default audio output on first run?
- Do we pre-load a default instrument in the Sandbox?
- Should there be a "welcome" state vs. just an empty Sandbox?

---

### 2. "I'm building a song."

This is where they spend 80% of their time. Adding tracks, loading plugins, tweaking effects, recording parts, experimenting. It's creative and messy. They add things, remove things, try things, undo, try again.

The key emotion: **flow**. They should never feel like they're fighting the tool. Every action should have one obvious gesture. The most common operations (add track, load plugin, record, undo) should be effortless. The less common ones (add bus, configure sends, set up effects chain) should be discoverable but not in the way.

**What to nail:** the rhythm of create → tweak → listen → adjust. If any step in that loop has a speed bump, the creative flow breaks.

**Open questions:**
- Is the Track menu the right place for adding tracks? Or should there be an inline affordance in the Producer?
- How discoverable are effects chains and sends for a new user?
- Is the plugin browser (Library sidebar tab) pulling its weight vs. the PluginSlot right-click picker?

---

### 3. "I'm setting up my performance."

This is the unique thing. No other tool does this. The musician has their song and now they want to program their live show: which knob does what, what happens when they press this button, what's the sequence of transitions in the set.

The mental model is **programming a setlist**, not configuring a MIDI router. They think: "After the intro, I crossfade to the strings. Then at the bridge, I morph the pad sound. At the end, everything fades out." The score IS the show plan.

Today this lives in the Performer view (ControllersPane + SongMappingsPane). The flow is: learn a control → drag it to a mapping → assign an action → put some mappings in the score for ordered transitions.

**What to nail:** the mapping should feel like describing the show to someone, not filling out a form. This is where Claude as a creation partner could be transformative — "map the top row of pads to crossfade between each track over 4 bars" is faster and more expressive than dragging 8 controls one at a time.

**Open questions:**
- Is the Atemporal / Score split intuitive or confusing? Would a different framing (e.g., "Always Active" / "Setlist") be clearer?
- Should the Score be visualized as a timeline rather than an ordered list?
- How do multi-song sets work? Is the score per-song or per-set?

---

### 4. "I'm performing."

Hands on keys and controllers. Eyes maybe on the screen, maybe not. Everything has to work from muscle memory and physical controls. The screen shows what you need at a glance (levels, current position, what's coming next in the score) but doesn't demand interaction.

**What to nail:** zero surprises. No dialogs, no popups, no "are you sure?" during a show. Song switching is instant. MIDI is rock-solid. If a plugin crashes, the rest keeps playing. The UI during performance should feel like a **dashboard**, not a workstation — information out, not interaction in.

**Open questions:**
- What does the performance-time screen look like? Is it the Producer, the Performer, a dedicated "Stage" view, or something else entirely?
- What information is glanceable: levels, current song, score position, next transition, tempo, time?
- Should there be a "performance mode" that locks the UI (prevents accidental clicks) and shows only the dashboard?
- How does the performer switch songs mid-set? (File → Open Song works for building; mid-show needs something faster — MIDI binding? Dedicated next/prev?)

---

### 5. "I'm refining after a rehearsal."

"The crossfade was too slow." "The reverb was too wet." "I want to add one more transition before the outro." Quick, targeted edits. Not rebuilding from scratch — surgical changes to an existing setup.

**What to nail:** finding the thing to change should be fast. Right now, finding a specific mapping or action requires scanning the Performer view. The more complex the song, the harder this gets. Some kind of search/filter, or better visual organization (grouping mappings by controller region, by song section, by action type) would help.

**Open questions:**
- How do you find a specific mapping or action quickly in a complex song?
- Should there be a "diff" view showing what changed since last save / last performance?
- Can Claude help here? "What actions use Track 3?" / "Show me all crossfade timings."

---

## UX Principles

### A. Two modes, one app.

Building and Performing are different activities with different needs. Building is exploratory (big screen, mouse, visual feedback). Performing is committed (controllers, muscle memory, glanceable status). The app should serve both without making either feel like a second-class citizen.

The mode switch should be one gesture, and it should feel like the app *transforms* — not like you're navigating to a different screen.

**Current state:** toolbar has Producer / Performer / Mixer / Sidebar as flat nav buttons. Producer and Performer are workspace modes (configure multiple slots); Sidebar and Mixer are supplementary panels (independent toggle). This distinction isn't visible in the toolbar — recognized tension, not yet resolved.

**Design tension:** are Producer and Performer "modes" (mutually exclusive, one active) or "views" (independently placeable in any slot)? The slot system says views; the user's mental model might say modes. Need to decide — or find a design that makes the distinction irrelevant.

---

### B. Claude is the universal shortcut.

Anything you can do by hand, you should be able to describe to Claude. "Add a reverb bus and send all the pad tracks to it at -12dB" is faster than doing it manually. This isn't a separate feature — it's the primary way power users build complex setups.

The chat needs to feel like talking to a stage tech who knows the rig, not like programming a terminal.

**Current state:** chat is a pane content that can be placed in any slot. The authoring model says "chat is the primary creation surface" — but the UI doesn't privilege it. It's just another view in the slot system, easy to hide or forget about.

**Design tension:** if chat is the primary creation tool, should it have a more prominent, persistent presence? A floating input bar always visible (like Spotlight)? An inline command palette? Or is the current "open it when you need it, close it when you don't" the right model?

---

### C. Progressive disclosure, not progressive complexity.

First launch: one track, one sound, play. Second session: add effects, record something. Third session: set up mappings, build a score. Tenth session: compound morph actions, multi-song sets, custom automation scripts.

The complexity is always available but never forced. The app feels simple until you need it to be powerful.

**Current state:** the app front-loads a lot of concepts. Empty Producer + empty Performer + empty Sidebar + Mixer with no tracks = a lot of empty panels. A new user doesn't know which one to start with.

**Design tension:** how do we guide without constraining? A wizard feels heavy. Tooltips feel patronizing. The "right" answer might be: start with less visible (one pane, not four) and let the user expand as they discover.

---

## The Key Design Surface

When a user is on stage performing, **what do they see?**

That screen real estate — levels, score position, active song, upcoming transition — is the most constrained and most important design surface in the app. Every pixel has to earn its place.

Getting the performance dashboard right is what separates "a good DAW with mappings" from "an instrument I trust on stage."

**Not yet designed.** This may be the single most impactful design decision ahead. Options range from:
- The Producer view (timeline + levels) as-is, just with a "lean back" density
- A dedicated "Stage" view optimized for glanceability — large meters, song name, score status, nothing else
- No screen at all — the UI is irrelevant during performance, everything is on the controllers, and the screen just shows a logo or ambient visualization

---

## Navigation Model

**Current state:** four toolbar buttons (Sidebar, Producer, Performer, Mixer) that toggle views on/off. Keyboard shortcuts exist but aren't discoverable without documentation.

**Recognized tension:** Producer and Performer are "workspace modes" that configure multiple slots. Sidebar and Mixer are "supplementary panels" that toggle independently. Both types look the same in the toolbar.

**Options under consideration:**
- **Mode selector + panel toggles** — visually separate the two types. Mode selector (Producer | Performer) on the left; panel toggles (Sidebar, Mixer, Chat) on the right. Mode buttons are mutually exclusive; panel buttons are independent.
- **Single flat bar, current design** — accept the inconsistency for now. It works, it's discoverable, refine later based on user feedback.
- **Contextual toolbar** — the toolbar content changes based on what's active. In Producer mode, show transport controls and track operations. In Performer mode, show score controls and mapping operations. The mode switch is in a fixed position; everything else adapts.

No decision made. Gathering feedback first.

---

## Status

This document is a seed for design discussion. Nothing is decided. The current implementation is pragmatic: it works for testing but doesn't represent a final design vision. User feedback from the first beta round will inform which of these questions matter most.
