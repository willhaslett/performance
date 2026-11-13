# Rewrite Handoff — Deep Issues to Carry Into the Live-Performance-Only App

**Written 2026-11-13.** The rewrite drops the Producer, the Looper and AI composition. What remains is a live performance design and runtime environment: plugin hosting, tracks/busses/sends/mixer, live MIDI routing, MIDI controls → actions, automation (fades/morphs), state/persistence, Lua/IPC, and LLM chat (without composition). Notes exist only as MIDI data that can trigger actions.

This doc lists **every deep or structural issue we have identified but not fixed** that still matters after the rewrite. That covers engine, threading, persistence, hosting, devices and ops. It draws on:

- the backlog in CLAUDE.md, the docs in `docs/`, and DEV_HISTORY;
- the two unmerged branches;
- the auto-memory;
- a code audit run for this handoff.

Each item is tagged:

- **[V]** — verified in code (file:line given);
- **[D]** — documented earlier and still open;
- **[S]** — suspected, not confirmed.

File:line references are against `main` @ `25c8ae0` plus the uncommitted working tree.

Section 0 covers housekeeping. Sections 1–8 cover the deep issues, roughly in order of risk. Section 9 lists what the rewrite deletes and therefore makes moot. Section 10 is new observations that are not in any backlog.

---

## 0. Housekeeping before the rewrite starts

- **Beta builds are dead right now. [V]** The expiry is `juce::Time(2026, 9, 16)`, which is October 16, 2026 (`src/main.mm:623`). Every 0.0.5 build the friends have now refuses to launch.
- **Uncommitted work on `main`.** This is the "focus as an overlay" change:
  - `effectiveArmed` / `effectiveInputMonitoring` in StateAPI;
  - `EngineSync::onFocusChanged`;
  - `inputMonitoring` now defaults to `false`;
  - default j/k/s/i key bindings;
  - matching test changes.

  It is live-routing logic, so it's relevant to the rewrite. Commit or discard it on purpose.
- **Branches whose notes never reached `main`:**
  - **`out-of-process-plugins`** — only `docs/OUT_OF_PROCESS_HOSTING.md` (research, prior art and a spike plan), plus CLAUDE.md updates. Those updates include "ABC composition merged 2026-05-16" and its two open tails. No code.
  - **`latency-testing`** — only `docs/LATENCY_TESTING.md`. The test plan is set up but no measurements were taken. Its CLAUDE.md diff also carries **the boot-loop bug (§4.1)**, the stale `bin/reset` doc, the stale theme JSONs and a JUCE XML assert. None of that is on `main`.
  - Merge the docs or cherry-pick them so nothing is lost.
- **`audio-device-boundary` is fully merged** into `main`.
- **CLAUDE.md is out of date in places:**
  - It says MIDI hot-plug "doesn't work", but DEV_HISTORY says 4 Hz polling shipped. Verify which is true.
  - It describes `bin/reset` as a surgical delete that keeps devices. The real script is `rm -rf` and preserves only `keys/` and `plugin-cache.xml`.

---

## 1. Plugin hosting — the existential risk

### 1.1 No crash isolation for AUv2 (the big one) [D][V]

**Current state:**
- Every plugin runs in-process inside our `AudioProcessorGraph` (`AudioEngine::addTrackInstrument` / `addEffectToList`).
- Kontakt 8 segfaulted on 2026-04-25 and took the app down with it.
- Full research is in `docs/OUT_OF_PROCESS_HOSTING.md` on branch `out-of-process-plugins`.

**What the research found:**
- **AUv3 is already isolated.** JUCE 8 loads it with `kAudioComponentInstantiation_LoadOutOfProcess`.
- **AUv2 always loads in-process.** JUCE uses `AudioComponentInstanceNew`, which has no options argument. AUv2 is almost every real plugin: this machine has zero registered AUv3s.
- Apple's docs say the out-of-process flag is "just a request" for v2 and "may fall back".

**Status:** research only. **Spike A has never been run.** Spike A means forcing `AudioComponentInstantiate(LoadOutOfProcess)` on a v2 plugin, confirming it really lands in a separate process, `kill -9` the helper mid-render, and checking whether we survive (render returns an error) or die. Mixxx reports this works (PR #16106).

**Options after Spike A:**
- **A — OS path.** Cheap, if it works.
- **B — our own host process.** Shared memory, a lock-free per-block handshake, and IPC for MIDI, parameters, state and editor windows. Bitwig, Reaper and AudioGridder do this; AudioGridder is built on JUCE. Multi-week.
- **C — document and defer.**

**Design constraints for B, whichever way it goes:**
- Group plugins into one helper process (or a few) rather than one per plugin. This keeps the context-switch cost manageable, which is Ardour's objection.
- Never add a whole buffer of latency.
- When a helper is late, output silence. Never stall the audio thread.
- Recovery means re-instantiating the plugin and restoring its last saved state blob. That requires §1.4.

**Working agreement:** see memory `feedback_deep_engine_working_agreement`. Verify rather than assume, ask before accepting any "good enough", test as you go, teach as you go.

The rewrite should **design the engine around this boundary from day one**. Even if B comes later, treat a plugin as something that can vanish, can be late, and can be re-instantiated.

### 1.2 Plugin hang (not crash) on the audio thread [S]
There is no watchdog. A plugin stuck in `processBlock` stalls the whole callback. Out-of-process hosting with a deadline solves this too, and that is an argument for B over A.

### 1.3 Plugin scanning is in-process [D]
`bin/reset` keeps `plugin-cache.xml` because "re-scan can hang on bad plugins". A bad plugin can hang or crash the app at scan time on first launch. Scan out-of-process; JUCE has `PluginDirectoryScanner` plus the dead-man's-pedal pattern, or use a child process.

### 1.4 Plugin state is never autosaved [V]
- The autosave timer calls only `saveFrom` (`PerformanceCoordinator.cpp:649-662`).
- `captureProcessorState` runs only on quit, song switch and explicit save (`:1453`, `:1509`, `:2144`).
- Knob changes made in a plugin editor are lost on any crash. Crash recovery (§1.1) has nothing recent to restore.
- Capturing state has to happen **off** the message thread (CLAUDE.md "Background plugin state capture"). Some plugins take a long time in `getStateInformation`.

### 1.5 Editor windows outlive their processors (use-after-free) [V]
- `editorWindows` is cleared only in `clearAllTracks` and on device setup (`AudioEngine.mm:199`, `:1011`).
- These paths delete the node but leave its open editor (`:672-694`, `:794`, `:853`):
  - `removeTrack`
  - `removeEffectFromList`
  - `removeTrackInstrument`
  - bus removal
- Example: "remove the reverb" from chat while the reverb's window is open.

### 1.6 Plugins are identified by name substring [V]
- `findPluginDescription` returns the first `name.containsIgnoreCase(...)` match (`AudioEngine.mm:566-576`).
- So "Kontakt 7" vs "Kontakt 8", "Reverb", or two vendors' plugins with the same name can load the wrong plugin, and that plugin then receives another plugin's state blob.
- EffectNode identity itself is now UUID-based (`AudioEngine.h:207-210`); the name problem moved here.
- Fix: use a stable identifier (format + manufacturer + AU type/subtype/manufacturer code, i.e. `PluginDescription::createIdentifierString`). This is also a prerequisite for VST3 (§7).

### 1.7 Plugin load failure is silent and permanent; possible orphaned nodes [V]/[S]
- **[V]** On failure the async callback just logs and returns (`AudioEngine.mm:720-723`, `:753-756`):
  - `LoadStatus` stays `Pending` forever;
  - there is no retry;
  - the UI shows nothing.
- **[S]** Two async loads can race on the same track (A→B→A song switch, preset swap). The second `instrumentNode = addNode(...)` (`:726`) then orphans the first node, which keeps using CPU and memory.

### 1.8 Plugin is wired in before its state is restored [V]
- The plugin is connected (`addNode` + `rebuildConnections`) before `setStateInformation` runs in `onLoaded` (`EngineSync.cpp:150-157`, `:233-238`).
- It can briefly sound with its default patch.
- Its state is set while the audio thread is already rendering it.
- Fix: restore state, then `prepareToPlay`, then connect.

### 1.9 Plugin latency / PDC is never considered [S]
No code reads `getLatencySamples()`. JUCE only compensates parallel paths when the graph rebuilds, so a plugin that changes its latency at runtime goes unnoticed. This matters for parallel sends and busses.

### 1.10 Plugin state stored as base64 TEXT [D]
This is item R9 in `INCIDENT_2026-04-18_PERSISTENCE.md`. Plugin state should be a BLOB. It also inflates every undo snapshot (§4.2).

---

## 2. Threading model / control path

### 2.1 MIDI bindings run actions on the CoreMIDI thread (critical) [V]

**Path:**
1. `MIDIEngine::handleIncomingMidiMessage` calls `songRuntime->handleNoteOn` / `handleControl` directly (`MIDIEngine.cpp:165-176`).
2. `SongRuntime::dispatchControl` calls the binding lambda.
3. The lambda calls `executeAction` (`PerformanceCoordinator.cpp:2876`).

There is no thread hop on this path. The MIDI-learn callbacks *do* hop with `callAsync`, but dispatch doesn't.

**Consequences:**
- StateAPI mutations run on the MIDI thread. So does `StateEventBus::emit`, which calls subscribers synchronously (`StateEvents.h:44-54`): EngineSync → AudioEngine graph mutation, plus GUI components.
- Lua actions also run there, using the single `lua_State` that chat, IPC and the GUI use on the message thread.
- Automation `interpolate` pushes into `active` while the timer iterates it.
- The binding map is cleared and rebuilt on the message thread during a song switch (`:1511`) while the MIDI thread may be iterating it.

Moving a bound fader during chat activity or a song switch can corrupt the Lua VM, the state or the graph. **These are exactly the features the rewrite keeps.**

**Fix:** the rewrite needs an explicit threading model. Control events go into a lock-free FIFO, and one owner thread drains it, either the message thread or a dedicated control thread that owns state. Write down who may touch StateAPI and from which thread, and enforce it with asserts.

### 2.2 AutomationEngine invalidates its own iterator [V]
- `timerCallback` iterates `active` and runs callbacks inside the loop (`AutomationEngine.cpp:66-89`).
- Sequential morph steps `push_back` from `onComplete` (`ActionInterpreter.cpp:63`, `:73`, `:137`). If the vector reallocates, `it` is invalid and `erase(it)` is undefined behaviour.
- `cancelAll()` called from inside a callback clears the vector mid-iteration (`PerformanceCoordinator.cpp:2134`, `:2240`).
- `morphChain` / sequential `morph` can crash or skip steps.
- There is no per-target ownership: two fades on the same gain both write to it.

### 2.3 Automation runs at 60 Hz on the message thread [V]
- Fades write gain through StateAPI at about 60 Hz (`PerformanceCoordinator.cpp:189`).
- `GainProcessor` applies the gain with no ramp (`GainProcessor.h:23-26`), so you get **zipper noise**, and mute and solo **click**.
- Fades stall whenever the message thread blocks: autosave (§4.3), undo copies (§4.2), Lua (§2.4), a plugin editor, or a modal.
- A performance tool should give each gain and parameter a target plus a ramp handled per sample or per block on the audio thread, driven by a clock that isn't the GUI timer.
- Plugin parameter morphs call `params[i]->setValue()` directly from the message thread. Most plugins tolerate this, but it bypasses state, so morph end-states are not persisted.

### 2.4 Lua has no execution limit [V]
There is no `lua_sethook` and no timeout in `src/scripting/`. An LLM-generated `while true do end` freezes the message thread: UI, autosave and automation stop, while audio keeps running. Add an instruction-count hook with a deadline, and consider a separate Lua state for LLM tool calls.

### 2.5 IPC server [V]/[S]
- **[V]** `/tmp/performance.sock` is created with the default umask, so any process running as the same user can execute Lua.
- **[V]** The Lua base library still exposes `dofile` and `loadfile`, which can read files.
- **[V]** The accept loop is single-threaded and uses a blocking `read` with no timeout (`IPCServer.cpp:63-81`), so one idle client wedges IPC.
- **[S]** Possible quit-time deadlock: `stop()` joins the listener thread while that thread is blocked in `callFunctionOnMessageThread` (`:113`).

---

## 3. Graph mutation, song switching, device changes

### 3.1 Every routing change tears down all connections, synchronously [V]
- `rebuildConnections()` removes every connection, then re-adds them one by one (`AudioEngine.mm:1186-1188`).
- `UpdateKind` is never passed, so each change does a synchronous render-sequence rebuild.
- The audio thread can render a half-wired graph, which means a **dropout or click on every routing change**.
- Triggers:
  - adding a plugin, a send or a track;
  - toggling input monitoring;
  - **changing track focus** (`setTrackInputMonitoring` → `rebuildConnections`, `:931`).
- Fixes:
  - batch changes with `UpdateKind::none` and finish with one `rebuild()`;
  - diff connections instead of wiping and re-adding them;
  - ideally, live-MIDI routing shouldn't be a graph topology change at all: gate it in a MIDI router node.

### 3.2 Song switch = full teardown + rebuild, silence until plugins load [V]
- Sequence:
  1. `loadSong` captures every plugin's state synchronously;
  2. `clearEngine` removes every node, each with a synchronous rebuild;
  3. every node is recreated, with async plugin instantiation.
- No preloading, no "keep playing while the next song loads", and reverb/delay tails are cut.
- For a live tool, **song switching is a performance gesture** (CLAUDE.md lists ⌘O palette and MIDI next/prev song). The architecture question is whether to follow MainStage's concert model:
  - preloaded per-song subgraphs, or shared plugin instances across songs;
  - an instant switch that keeps tails ringing.
- This is the biggest engine-design decision the rewrite should make up front. It interacts with §1.1 (process grouping) and memory budget.

### 3.3 Per-project engine sample rate vs. song switching [V]/[S]
- `SongState.sampleRate` (from the device-boundary refactor) means two songs at different rates force a device reconfigure mid-set, which stops audio.
- **[S]** `setAudioDeviceSetup` also changes the system device's rate for other apps.
- For a performance runtime, consider one engine rate per rig or setlist instead of per song.

### 3.4 Device-boundary SRC: input path is best-effort [D]
- `RateBridgeProcessor` drives output by pull, so it is drift-safe.
- The input direction is "best-effort". The drift strategy (drift-corrected ratio vs. periodic resync) was listed as an open question in `AUDIO_DEVICE_BOUNDARY.md` §8.
- It only matters when device rate ≠ engine rate with live audio input, for example a Bluetooth mic. Verify it under a long-running test.
- There is also a per-chunk `MidiBuffer` allocation on the non-passthrough path (`RateBridgeProcessor.h:139`).

### 3.5 Transport / beat clock requires an audio device [D]
- The beat clock advances in `GraphWrapper::processBlock`.
- Decide whether the rewrite keeps a beat clock at all. Beat-relative `delay`, tempo-synced morphs, Score and the action track all depend on it. If it stays, give it a device-independent fallback.

---

## 4. State, undo, persistence, crash handling

### 4.1 Deleting the focused track → permanent boot loop (high severity, still unfixed on `main`) [V]
1. `StateAPI::removeTrack` (`StateAPI.cpp:861`) doesn't clear `focusedTrackId` or `selectedTrackIds`.
2. On the next load, the dangling ID goes through the fail-loud `track()` helper, which hits `FATAL` and aborts.
3. The crash handler (§4.4) then **saves the bad state back**, so the app is unbootable until the state is wiped.

The repro DB was stashed at `/tmp/performance-crash-repro-focus-danglingref.db`; check whether it survived.

This is one instance of a class: **referential integrity on delete.** Bindings, sends, morph targets, focus, selection and score steps all hold UUIDs. `ACTION_INSTANCES_REFACTOR.md` covered cascade-delete for actions only.

The rewrite should:
- (a) cascade or clear every reference when an entity is deleted;
- (b) make **load** resilient: drop dangling refs with a warning instead of asserting. Fail-loud is right for runtime programming errors, not for persisted data.

### 4.2 Undo snapshots the entire AppState on every mutation [V]
- `pushUndo()` does `undoStack.push_back(state)`, a full copy (`UndoHistory.h:13-19`).
- That copy includes every song, every base64 plugin blob and every MIDI event.
- It happens on:
  - every `setTrackGain` / `setBusGain` / `setMasterGain`;
  - every 60 Hz automation tick;
  - every incoming CC on a bound fader;
  - every GUI fader drag event. GUI faders use `beginTransaction`; automation and CC do not.
- Up to 50 + 50 full copies are held in memory.
- After one fade, the 50 undo steps are all fade frames.
- Continuous mutation also keeps pushing back the 3-second autosave debounce.
- Fix:
  - continuous values (gain, parameters) should not snapshot at all, or should coalesce per gesture;
  - consider structural sharing or command-based undo;
  - decide what undo even means in a live runtime.

### 4.3 Autosave is a full DB rewrite + backup on the message thread [V]
- `PersistenceLayer.cpp:775-835` does three things:
  1. `sqlite3_backup` of the whole DB;
  2. `clearAllData()`;
  3. re-INSERT of every song, plugin blob and preset.
- It runs 3 s after the last change, mid-performance. The stall grows with the library and delays MIDI binding dispatch, which runs through the message thread once §2.1 is fixed.
- Remediation is R7 (per-entity mapper) followed by R8 (event-driven incremental save), from the incident retro. Also move the save off the message thread by snapshotting, then writing on a worker thread.

### 4.4 Crash handler does heavy unsafe work in a signal handler [V]
- `main.mm:597-610` calls `coordinator->save()` from the signal handler. Inside a signal handler, that means:
  - `captureProcessorState`, possibly into the plugin that just crashed;
  - a full SQLite rewrite;
  - malloc and locks;
  - quite possibly on the audio or MIDI thread.
- Likely outcomes:
  - a re-crash;
  - a deadlock, so the app hangs instead of exiting;
  - **saving corrupted state**, which is the §4.1 amplifier.
- Fix:
  - log with `write(2)` only;
  - rely on frequent normal autosaves (§1.4, §4.3) and out-of-process hosting;
  - at minimum, never emergency-save during boot or load.

### 4.5 Remaining persistence hardening from the incident retro [D][V]
Source: `docs/INCIDENT_2026-04-18_PERSISTENCE.md`. R2 (typed IDs) has landed. Still open:
- **R3** — `Result<T, Error>` throughout persistence. The `saveHadError` flag is still used.
- **R4** — declarative state→engine field application in EngineSync. `onTrackCreated` and `onEntityUpdated` still cherry-pick fields, and the uncommitted focus work just touched both again.
- **R5** — `PRAGMA foreign_key_check` + `integrity_check` on open. Not present.
- **R6** — end-to-end tests for every user flow.
- **R7 / R8** — mapper layer + incremental save (§4.3).
- **R9** — BLOB plugin state (§1.10).
- **R11** — silent-failure audit across subsystems.
- **R12** — state-default vs. engine-default drift audit.
- **R13** — real schema migration with versioning. Currently it's ad-hoc `ALTER TABLE` calls. The "no migration code" rule is pre-beta only; once strangers have DBs, this becomes mandatory.

A rewrite is the natural moment to do R7, R8 and R13 properly instead of retrofitting them.

### 4.6 Remaining items from the "architecture cleanup" memory [D]
From memory `project_state_management.md`. Re-verify each one.
- Some API reads still query the engine instead of state: gains, effect lists, plugin name.
- Master output effects bypass the registry and may not persist.
- Structural operations not fully event-driven.
- Track preset load was disabled; "re-enable track preset load" is listed under *Unverified behaviors*.

### 4.7 Score is unimplemented [V]
- `saveInitialState`, `loadInitialState` and go-to-step replay are stubs (`PerformanceCoordinator.cpp:2165`, `:2170`, `:2183`).
- CLAUDE.md still lists Score as a core concept.
- The rewrite should decide whether Score is in or out.

---

## 5. Audio device, latency, I/O

### 5.1 Built-in Mac mic: ~50 ms stream latency [D]
- The cause is `kAudioStreamPropertyLatency` = 2399 samples, from macOS's voice-processing chain on the built-in mic.
- Logic gets about 3.7 ms on the same machine, so the path exists; we just don't reach it through JUCE's AUHAL layer.
- Candidate fixes:
  - an AVAudioEngine input path for that device;
  - a different/private stream;
  - document it and route around it.
- Listed as a **public-beta blocker**.
- The empirical round-trip measurement (branch `latency-testing`, a 2×2 of app × device against Audacity) **was never run**.
- Without recording, the remaining question is live monitoring latency on audio-input tracks. Recording alignment (input-latency compensation) mostly goes away with the Producer and Looper, **unless the rewrite keeps any capture.**

### 5.2 ~80 ms round-trip on built-in audio; buffer-size dropdown lies [D]
- Requested 128, ran at 512, and the UI still shows 128.
- Unconfirmed causes:
  - an aggregate device when input ≠ output;
  - JUCE not setting the HAL buffer frame size the way Logic does;
  - a plugin reporting latency.
- Investigation was shelved. For a live tool, **key-to-sound latency on an instrument track** is the number that matters, and nobody has measured it in isolation.
- The UI needs to report the device's real configuration honestly.

### 5.3 MIDI hot-plug [D]
CLAUDE.md says unplugging or replugging a device needs a relaunch, but DEV_HISTORY claims 4 Hz polling shipped. Verify it with real hardware. For a stage tool: a USB cable that gets knocked out and plugged back in must recover on its own, with bindings intact.

### 5.4 Stuck and chopped notes on routing changes [V]/[S]
- **[V] logic, [S] audible effect.** `setTrackInputMonitoring` (`AudioEngine.mm:912-930`) does two things in a row:
  1. queues `allNotesOff` into the MIDI collector;
  2. **immediately** calls `rebuildConnections()`.
- The old instrument may lose its live-MIDI connection before the next audio callback delivers the noteOffs, which leaves stuck notes.
- Meanwhile CC123 goes to every instrument still connected, which cuts their held notes.
- The rewrite needs **per-note routing memory**: each noteOff goes wherever its noteOn went, including across focus changes, song switches and binding changes. This replaces the old "stuck notes on stop" Looper bug (which goes away) with the general live-routing version.
- Also needed: a panic (all-notes-off plus sustain-off on every destination) as a bindable action.

### 5.5 Smaller audio-thread RT-safety issues [V]
- `MidiMessageCollector` takes a lock shared with the MIDI thread. This is standard JUCE but still a lock on the audio thread.
- MidiBuffer growth in note-flush paths.
- Retain only what survives the rewrite.

---

## 6. LLM / chat surface (kept, minus composition)

- **Destructive-op gating [D].** Confirmation or dry-run for deletes and other destructive tool calls. Listed as an open tail in LAMBDA_CHAT_PROXY. More important once the referential-integrity bugs (§4.1) exist.
- **State introspection from memory [D].** The model said "no plugins installed" without querying. A prompt rule was added; watch whether "what tracks / what's bound" questions show the same failure.
- **Track-preset-first creation [D].** "Create a synth track" yields DLS piano labelled "Synth".
  - The C++ machinery exists; what's missing is the `listTrackPresets` / `applyTrackPreset` / `saveTrackPreset` Lua verbs and a prompt rule.
  - It's still relevant without composition.
  - It merged to main unresolved (memory `project_track_preset_first.md`).
- **Lua execution safety [V]** — see §2.4 and §2.5. LLM-authored Lua is untrusted input to a live process.
- **Model and runtime config [D]:**
  - default model and system prompt are baked into the binary;
  - bring-your-own-key is deferred;
  - per-install monthly caps are enforced in the Lambda.
- **Embedded-chat issues in DEV_HISTORY "Known Issues" [D]** — re-verify:
  - bindings created via chat use wrong-case track names;
  - custom action creation fails through the chat→tool→IPC string-escaping path.
- The system prompt's large composing section and the ABC/RegionContent/`createEvent` tempo surface all get deleted.

---

## 7. Platform and format

- **VST3 hosting [D].** Deferred to 0.2.x. It unblocks Dragonfly and most new free plugins. It needs the plugin-identifier generalisation (§1.6) first. Scope is about 1–2 days in-process. Out-of-process hosting (§1.1) has to cover it too.
- **CLAP** — track it; JUCE hosting support is still maturing.
- **Bundled plugin pack [D]** — licensing "ruled out" list is load-bearing. There is no offline-install path.

---

## 8. Ops, build, tests

- **Unpinned sol2 [V].** `GIT_TAG develop` (`CMakeLists.txt:35`) means builds are not reproducible. Pin it.
- **Unbounded logging [V]:**
  - `/tmp/performance.log` is unbuffered, written from every thread including real-time-adjacent ones, and never rotated;
  - `rebuildConnections` logs every track on every rebuild;
  - LogPane appends forever.
  - Over a 2-hour gig, this grows without limit.
  - Logging from the audio or MIDI threads needs a lock-free queue.
- **Stale factory theme JSONs [D]** — startup warning spam.
- **Benign JUCE XmlElement assert on plugin-cache load [D]** — a noisy assert that could hide a real problem.
- **Test gaps [D]** — zero tests for AutomationEngine, MIDIEngine (routing, learn), LuaEngine and IPCServer. No graph mock for audio-thread behaviour. These are the subsystems the rewrite keeps.
- **Unverified behaviours list in CLAUDE.md [D]** — re-check the ones that survive:
  - plugin state restore after relaunch;
  - rapid song switching under MIDI load;
  - autosave under stress.

---

## 9. Made moot by the rewrite (don't carry forward)

These are open items that live entirely in deleted scope. They're listed so nobody spends time on them, and so the deletion is deliberate.

- **Looper:**
  - bootstrap-stuck-after-reset;
  - per-track gesture queue / undo;
  - cycle-wrap bugs;
  - looper stuck notes on stop;
  - Producer↔Looper tempo coupling;
  - `AppMode` / pane-mode model.
- **Producer:**
  - regions, takes, arrangement scan;
  - quantize / beat-math rule;
  - MIDI and audio region editors;
  - synthetic noteOffs at region end (`Arrangement.cpp:583`);
  - ProducePane size refactor;
  - LCD interactivity;
  - events-track toggle.
- **Recording and assets:**
  - RecordFIFO / AudioWriterThread / peaks;
  - input-latency compensation on commit;
  - audio assets browser and loose-file registry;
  - Bounce and its punts (OfflineRenderer).
- **Composition:**
  - ABC parser/writer;
  - RegionContent;
  - tempo / time-signature event CRUD;
  - TempoMap / TimeSignatureMap runtime;
  - mid-playback tempo.
- **Audio-thread data races in deleted code:** GraphWrapper's unsynchronised reads of `TrackState` regions, and `trackMidiSources` / `trackAudioFileNodes` maps mutated while audio runs. If any per-track MIDI-source map survives, make it RT-safe.
- **Metronome**, unless a beat clock survives (§3.5).

**Deletion-time caution:** the beat clock, live-MIDI injection and the focus/`inputMonitoring` routing are entangled with `GraphWrapper`, `InternalSequencer` and `Arrangement`. Map the dependencies before deleting so that live-input routing (which stays) doesn't get removed with them.

---

## 10. Things not in any backlog that a live runtime will need

These are new observations, not items we identified earlier. They shape the engine design, so they belong in the rewrite conversation.

- **CPU headroom / overload visibility.** No DSP-load meter and no xrun counter. Performers need to see danger before it happens.
- **Tempo sync with other gear.** MIDI clock in and out and/or Ableton Link. `DAW_BRIDGE_PLAN.md` covers DAW control but not sync for a live rig.
- **Setlist / concert model.** Ties to §3.2: song order, preload policy, and next/prev song as bindable actions with zero-gap switching.
- **Graceful degradation.** What the performer hears and sees when a plugin dies (§1.1), a device disappears (§5.3), or the LLM proxy is unreachable. Each should fail quietly and keep the show going.

---

## Suggested sequencing for the rewrite

1. **Decide the engine shape first.** Settle the threading model (§2.1), the song-switch / preload model (§3.2), and the plugin process boundary (§1.1). **Run Spike A** before committing the hosting design.
2. **Build the control path on that model:**
   - MIDI → FIFO → owner thread;
   - audio-thread smoothed gains and parameters (§2.3);
   - per-note routing memory (§5.4);
   - an incremental graph-update discipline (§3.1).
3. **Build the state layer with the lessons baked in:**
   - referential integrity and tolerant load (§4.1);
   - no snapshot-per-gain undo (§4.2);
   - incremental off-thread save with plugin state captured in the background (§1.4, §4.3);
   - Result-based persistence;
   - versioned migrations.
4. **Do the hosting work:**
   - stable plugin identifiers (§1.6);
   - state-before-connect (§1.8);
   - editor lifecycle (§1.5);
   - load-failure UX (§1.7);
   - out-of-process scanning (§1.3);
   - then VST3.
5. **Harden the safety and ops surface:**
   - Lua limits, IPC permissions;
   - crash handler;
   - log rotation;
   - pinned dependencies;
   - tests for the kept subsystems.
6. **Measure latency** (§5.1–5.2) on the new engine before the public beta.
