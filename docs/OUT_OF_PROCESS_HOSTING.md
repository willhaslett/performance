# Out-of-Process Plugin Hosting — Crash Isolation

**Status:** Research / feasibility. Branch `out-of-process-plugins`. Opened 2026-06-13.

**Goal:** A plugin crash (segfault, bad sample load, infinite loop) must not take down the host. Live performance cannot tolerate "Kontakt died → whole session gone." Logic survives this because each plugin runs in its own subprocess; we currently do not.

**The incident this exists to prevent:** 2026-04-25, Kontakt 8 segfaulted and took the app down. We survived *only* because JUCE's `applicationCrashHandler` (`src/main.mm:554`) caught the signal and ran `coordinator->save()` before the process died. The performer still lost their in-flight session and had to relaunch. Not acceptable for a live tool.

This doc is written to be read by someone with a gist-level understanding of threading. Sections marked **[Teaching]** build the mental model from the ground up; skip them if you already have it.

---

## [Teaching] Process vs. thread, and why crashes spread

- A **process** is a running program with its own private chunk of memory (its *address space*). The operating system enforces a hard wall: one process physically cannot read or scribble on another process's memory. When a process crashes — say it dereferences a null pointer and the CPU faults — the OS kills *only that process*. Its neighbors don't even notice unless they were talking to it.
- A **thread** is a strand of execution *inside* a process. A process can have many threads running at once (the GUI thread, the audio thread, worker threads). Crucially, **all threads in a process share the same memory.** That's what makes them cheap and fast to coordinate — and also what makes them dangerous: if any one thread corrupts memory or faults, the *entire* process (every thread) dies with it.

Today, **every plugin you load runs as code executing on threads inside our process, sharing our memory.** Kontakt isn't a separate program we talk to — it's a chunk of Native Instruments' code that we loaded into ourselves and call as a function. So when Kontakt's code does something illegal, it's *our* process that faults. There's no wall. This is **in-process hosting**.

**Out-of-process hosting** puts each plugin in its own separate process, behind the OS's memory wall. We talk to it through **IPC** (inter-process communication) instead of a direct function call. If it crashes, the OS kills only the plugin's process; we get an error back and keep running. That wall is the entire prize.

---

## [Teaching] The real-time audio thread — the constraint that makes this hard

Out-of-process hosting sounds like "just call the plugin over IPC." The reason it's genuinely hard is the **audio thread**.

Audio is produced by a special high-priority thread that the OS calls on a fixed clock: every *buffer* of samples, it must hand back the next slice of sound. At 44.1 kHz with a 128-sample buffer, that's **once every ~2.9 milliseconds**, forever, with no slack. Miss the deadline once and the speaker gets silence for that slice — an audible click or dropout. In a live performance, dropouts are unacceptable.

To never miss the deadline, the audio thread must obey **real-time safety**: it may never do anything that could pause for an unpredictable amount of time. Specifically it must not:
- take a **lock** another thread might be holding (it would wait, unbounded),
- **allocate memory** (`malloc` can block on the system heap),
- do **file or network I/O**,
- make **system calls that wait**.

In-process, rendering a plugin is just a normal function call on the audio thread — microseconds, no boundary, no blocking. **Out-of-process, "render this plugin" now means: ship the input audio to another process, have it compute, and get the output back — across the process wall, every 2.9 ms, without the audio thread ever blocking.** You cannot use an ordinary "send request, wait for reply" IPC call, because waiting is exactly what's forbidden. You need **shared memory** (a region both processes can see) plus a **lock-free handshake** (atomic flags, no locks) so the two sides can rendezvous each buffer — and you must decide what the audio thread plays when the other process is late or dead (answer: silence for that slot, never a stall).

Building that bridge correctly *is* the hard part of this project. The good news, below, is that Apple already built one — the question is whether we can use it.

---

## What we found (verified against primary sources)

The big surprise: **the framework already does out-of-process hosting — but only for one class of plugin, and it's not the class that hurts us.**

macOS Audio Units come in two generations:
- **AUv2** — the classic C-API Audio Unit (a `.component` bundle). This is the overwhelming majority of plugins in the wild, including most big commercial instruments (Kontakt has historically shipped AUv2).
- **AUv3** — the modern App-Extension-based Audio Unit. By Apple's design these run **out-of-process by default** (in a system "extension service" process).

### Source 1 — Apple's SDK header (`AudioToolbox/AudioComponent.h`)

- `kAudioComponentInstantiation_LoadOutOfProcess` exists, available since macOS 10.11. ✅ It's real.
- But Apple frames out-of-process as **the AUv3 mechanism**: "A version 3 audio unit … can be loaded into a separate extension service process, and this is the default behavior for these components."
- For AUv2: `kAudioComponentFlag_CanLoadInProcess` is "**always true for V2 audio units**."
- And the kicker: "These options are **just requests** to the implementation. **It may fail and fall back to the default.**"

So Apple's own docs say the out-of-process path is built for v3, and asking for it is a hint the system may ignore.

### Source 2 — JUCE 8 host source (`juce_AudioUnitPluginFormatImpl.h:483`)

JUCE 8 (this repo vendors it at `lib/JUCE`, ~JUCE 8) already branches on plugin generation:

```cpp
static void createAudioUnit (VersionedAudioComponent versionedComponent, AudioUnitCreationCallback callback)
{
    if (versionedComponent.isAUv3)
    {
        AudioComponentInstantiate (versionedComponent.audioComponent,
                                   kAudioComponentInstantiation_LoadOutOfProcess,   // <-- isolated
                                   ^(AudioComponentInstance audioUnit, OSStatus err) { callback (audioUnit, err); });
        return;
    }

    AudioComponentInstance audioUnit;
    auto err = AudioComponentInstanceNew (versionedComponent.audioComponent, &audioUnit);  // <-- in-process, NO options arg
    callback (err != noErr ? nullptr : audioUnit, err);
}
```

- **AUv3 → `AudioComponentInstantiate(LoadOutOfProcess)`** → loaded out-of-process → **already crash-isolated for free.**
- **AUv2 → `AudioComponentInstanceNew`** → the legacy synchronous call that *doesn't even take an options argument* → **always in-process → NOT isolated.** JUCE deliberately does not attempt out-of-process for v2.
- JUCE detects v3 via the OS flag: `(desc.componentFlags & kAudioComponentFlag_IsV3AudioUnit) != 0` (`:409`).

### Source 3 — this machine's plugin census

- `pluginkit -mAv -p com.apple.AudioUnit` → **(no matches): zero registered AUv3 extensions.**
- `auval -a` shows the installed AUs are Apple's system units (`aufx … appl`, `aumu … appl`) — all classic **AUv2**.

In other words: on a real machine, **essentially everything loaded is AUv2 — the in-process, unisolated path.** The "free" v3 isolation covers approximately nothing of what's actually in use.

### How we host today (`src/engine/AudioEngine.mm`)

- Instruments: `AudioEngine::addTrackInstrument()` (`:706`) → `formatManager.createPluginInstanceAsync(...)` → JUCE's AU format (the branch above) → instance added to the `juce::AudioProcessorGraph` via `graph->addNode(...)`, stored as `Track::instrumentNode`.
- Effects: `AudioEngine::addEffectToList()` (`:739`), same pattern, stored as `EffectNode::node`.
- State: `getStateInformation`/`setStateInformation` serialize plugin state to base64 blobs in the song model (`PerformanceCoordinator::captureProcessorState`, `EngineSync::restorePresetState`).

So every plugin instance lives directly in our `AudioProcessorGraph`, rendered on our audio thread, in our process. Exactly the in-process model the [Teaching] section describes.

---

## What this means

The project is **not** "wire up out-of-process hosting from scratch." It's narrower and sharper:

> AUv3 plugins are already isolated by the framework. The plugins that actually crash us are AUv2, they load in-process, and **there is no documented Apple API that reliably loads an arbitrary AUv2 out-of-process** — Apple's own framing is that the out-of-process option is a v3 feature that "may fall back to the default" for v2.

That leaves a fork, and the cheap branch hinges on one experiment we have not yet run.

### The one experiment that decides everything (Spike A)

**Question:** If we *force* the out-of-process path for an AUv2 plugin — call `AudioComponentInstantiate(component, kAudioComponentInstantiation_LoadOutOfProcess, …)` on a v2 component instead of `AudioComponentInstanceNew` — does the OS actually load it in a separate process (giving us isolation), or does it silently fall back to in-process (giving us nothing)?

Apple's docs lean **pessimistic** ("may fail and fall back"), and JUCE's authors evidently concluded it wasn't worth trying for v2. But we have not *measured* it, and the working rule on this project is **verify, don't assume.** It's a few hours of throwaway code to get a definitive yes/no, and the answer is worth weeks of direction:

- **If forcing OOP on v2 works** (it really loads in a separate process and surviving a `kill -9` of that process is possible) → the fix is a small, surgical change to how we instantiate, plus recovery handling. Cheap, big win.
- **If it falls back to in-process** (the likely outcome) → the only way to isolate AUv2 is Option B below.

Spike A also needs to answer the recovery half: when the plugin's process dies mid-render, does the bridged `AudioUnitRender` return an *error* (survivable) or does our process die anyway? Isolation is only useful if we survive *and* can recover (re-instantiate, restore the saved state blob).

### The options, with honest trade-offs

- **Option A — Lean on the OS (cheap, coverage uncertain).** Force `LoadOutOfProcess` for v2 too. Decided entirely by Spike A. Best case: small patch. Risk: likely doesn't isolate v2 at all; partial/unpredictable coverage; we'd be depending on undocumented behavior Apple may change.
- **Option B — Build our own out-of-process host (expensive, full coverage, full control).** Spawn a child "plugin host" process, load the plugin there with JUCE in the child, and bridge audio + MIDI + parameters + state across the process wall with shared memory and a lock-free ring buffer (the hard real-time bridge from the [Teaching] section). This is real systems engineering — likely multi-week — and the only path that reliably isolates the AUv2 plugins that actually matter. It's also the approach that doesn't depend on Apple honoring an undocumented request.
- **Option C — Hybrid / document-and-defer.** Take the free AUv3 isolation, and for v2 either build B later or, short term, tell performers "run the AUv3 build of your plugin where one exists, and use an interface-grade rig." Weakest on the actual risk (the v2 plugins are the ones that crash), but lowest effort now.

My read going into the checkpoint: **run Spike A first regardless** — it's cheap and decisive, it might hand us Option A, and if it fails it converts "should we do the expensive Option B?" from a guess into a justified decision. We classify, *then* commit. We do **not** start building B blind.

---

## Plan

1. **Spike A — force-OOP-on-v2 feasibility** (throwaway branch/code; do not merge):
   - Census: enumerate installed AUs, tag each v2/v3 via `kAudioComponentFlag_IsV3AudioUnit`, so we know the real population (and check a big commercial v2 like Kontakt if we can get one on a test machine).
   - Force `AudioComponentInstantiate(LoadOutOfProcess)` on a known AUv2; confirm via the OS whether it actually landed in a separate process (e.g. inspect for an `AUHostingService`/`audiocomponent` helper process bound to it).
   - Render audio through it and verify it works across the bridge.
   - `kill -9` the helper process mid-render; observe whether we survive (render returns error) or die.
   - Write up the verdict here. **Checkpoint with Will.**
2. **Decision** — based on Spike A's verdict, choose A / B / C with Will. (Fork — do not self-decide.)
3. If B: design the host-process architecture (shared-memory ring buffer, lock-free render handshake, MIDI/param/state IPC, process lifecycle + crash recovery, editor-window hosting), then build incrementally with tests at each layer.

## [Teaching] Glossary

- **IPC** — inter-process communication: any mechanism for two processes to exchange data (shared memory, pipes, sockets, XPC). Needed because they don't share memory.
- **Shared memory** — a region of RAM mapped into two processes at once, so both can read/write it directly without copying. The fast path for audio across a process boundary.
- **Lock-free** — coordination using atomic CPU instructions instead of locks, so no thread can ever be left *waiting* on another. Required on the audio thread.
- **Ring buffer** — a fixed-size circular queue; the standard structure for streaming audio between a producer and consumer without allocating.
- **XPC** — Apple's high-level IPC framework for managing helper processes; what Apple's own AU sandboxing uses under the hood.
- **AUv2 / AUv3** — the two generations of macOS Audio Unit. v2 = classic in-process C API; v3 = modern App-Extension, out-of-process by default.
- **Real-time safety** — the discipline of never doing anything on the audio thread that can block for an unbounded time.
