# Dev History

Changelog, test inventory, completed work, and known issues. Archived from CLAUDE.md to keep the orientation doc lean. Authoritative source for "what changed" is still `git log`; this file captures context that doesn't fit in commit messages.

## Audio assets browser + cycle-chunk export (2026-07-18)

Shipped phase 1 of the audio-asset system (commit `010247a`). Goal: browse/move audio independently of placement, and carve a good chunk out of a long import to drop into the looper as a right-sized loop.

- **Assets pane** (Right slot, ⌘B): distinct project audio (dedup by path across `regions`/`loops` + the loose-file registry), grouped by origin (Recorded/Imported/Other), rows are drag sources.
- **Drag-to-place:** asset → Looper track = becomes that track's loop (defines the cycle if none exists); asset → Producer timeline = audio region at the drop beat. `LooperPane`/`ProducePane` gained `DragAndDropTarget`; DnD mirrors the region-drag idiom (custom mouse for source, JUCE DnD for cross-pane).
- **Provenance:** `TakeState.origin` set at record/import (persisted). **Loose files:** `SongState.audioAssets` + `audio_assets` table — the lightweight registry, *not* the deferred file-owned/ref-counted one. Peaks recomputed on load.
- **Bounce Cycle:** cycle-mode-only Producer transport button (enabled only for a single selected audio region the cycle overlaps) → `exportRegionCycleChunk` slices the region file to the cycle window, writes a canonical WAV, registers it as a loose asset.
- **Gotchas fixed mid-build:** (1) the Producer draws/edits the cycle via the *sequencer loop range*, but `song->cycleStart/End` lags it — the export now reads the sequencer range and persists it, so the asset-add's `Song/Updated` → `syncTempoFromState` can't snap the cycle back to a stale value. (2) Looper waveform for a loop longer than the cycle now clips at the cycle (scaled by the repetition's beat span) instead of cramming the whole file into the lane. (3) Assets pane retries rebuild while big-file peaks are still computing on load (they finish seconds later with no event).

## Bus sends — routing fixes + pre/post-fader (2026-07-13)

Surfaced by a tester report that bus tracks "don't make sound." Two bugs fixed (commit `1d8101b`), then per-send fader mode + mute added (commit `be9cd13`).

- **Silent sends from file-playback audio tracks.** An `AudioInput` track's playback branch wires `audioFileNode`/live-input straight to the output gain but never set `sourceNodeId`, and `isAudioInput` was only true when an input channel was assigned. So a send from a file-playback audio track (input unassigned, no effects) tapped an unset node and JUCE silently dropped the connection — the track played to master but its send was dead. Fix: the no-effects audio-track send tap now mirrors the output path (tap `audioFileNode` always + live input when monitored). Not unit-testable (graph-audio path; the known graph-mock gap) — verified by ear.
- **Phantom send stuck on the bus.** `EngineSync::onEntityDeleted` handled track/bus/effect but not `"send"`, and the engine had no `removeSend`. A deleted send stayed in the engine's `track.sends` and got re-wired on every `rebuildConnections()`, so live audio stayed on the bus — and, because sends were pre-fader, it survived muting the source. Fix: `AudioEngine::removeSend` + the engine `SendNode` now carries its state id (deletion has no trackId to work with) + the missing delete case.
- **Per-send pre/post-fader + mute.** Sends were implicitly pre-fader (surprising for an aux reverb — mute/fader didn't touch them). Now `SendState.preFader` (default false = **post-fader**, tapped from `outputGainNode` so it follows mute/solo/fader) and `SendState.muted` (mutes the sendGain stage). Post-fader tap is uniform across track types; pre-fader keeps the raw-source tap. Persistence: new `sends.pre_fader/muted` columns + additive `ALTER TABLE`, so existing dev DBs load without a reset (existing sends default to post-fader). UI: send-pill right-click menu (Pre-Fader / Post-Fader / Mute / Remove Send, ticked-item idiom); muted pill dims. Mid-piece note: the `addConnection` returns on send/bus audio edges are still unchecked — a rejected edge logs nothing. Left as-is since both bugs are now understood, but it's the first place to instrument if a send goes silent again.

## Pre-beta checklist — completed items (archived from CLAUDE.md)

- Code signing + notarization pipeline
- Beta expiry check (compile-time date in `main.mm`)
- Universal binary (arm64 + x86_64)
- Build info in toolbar (commit hash + `-dirty` + tag, refreshed on every build)
- Themed context menus via LookAndFeel
- Startup song chooser (themed modal card)
- Default song template (DLS Electric Piano + Audio In)
- Plugin scan overlay ("Scanning plugins..." shown while AU scan runs)
- Debounced autosave (3-second quiet period after last state change)
- File → Open Song submenu
- First-run audio device auto-selection — persists macOS default on empty config; defensive fallback via `getDefaultDeviceIndex` for aggregate/mic-denied edge cases
- **Persistence integrity (2026-04-18)** — data-loss regression fixed: first-session recordings now actually persist through quit/relaunch. Saves are error-checked and roll back cleanly on constraint violation; WAL-mode backup works; `createDefaultSong` no longer poisons `tracks.preset_id` with a plugin name; `onTrackCreated` applies `inputMonitoring` on load; `loadSong` hydrates audio region WAV files + waveforms. *Ship blocker resolved.*
- "Show Log File" menu item (View → Reveal Log in Finder) + Export Logs button on the LogPane (bundles all rescued `.prev` logs + current session into a Save As dialog) as a manual-fallback if telemetry shipping ever fails.
- Feedback channel — individual outreach per tester (text / email / iMessage). Documented in `performance-testing/.../welcome.md`.

## Bounce — shipping punts (carried forward)

Bounce to stereo file shipped for 0.1.0 (File → Bounce…, cycle-aware, faster-than-realtime via `OfflineRenderer`). Explicit punts that production-grade would eventually address:

- Automation values freeze during render (AutomationEngine pauses rather than ticking per-buffer).
- Constant tempo (start-of-render tempo applied throughout). Proper variable tempo needs TempoMap runtime evaluation — a prerequisite, tracked in Backlog.
- Constant time signature (same reason; TimeSignatureMap prerequisite).
- Hard cutoff at end beat (no tail-time option for reverb/delay decays).
- Master output only (no stem / per-track bouncing).
- Plugin compatibility varies — some AUs glitch when driven faster than realtime despite `setNonRealtime(true)`. Known risk; document per-plugin as testers hit it.

## Test Suite

Single file: `tests/PerformanceTests.cpp`. JUCE `UnitTest` framework. All tests isolated (fresh state per case, temp DB files cleaned up). `MockAudioEngine` captures call log for EngineSync verification.

143 tests across 11 classes:
- **StateAPI tests (51)**: Unit — in-memory state mutations/queries. Songs, tracks, busses, effects, sends, bindings, devices, config, events, selection, score steps. Comprehensive.
- **Persistence tests (11)**: Integration — save/load round-trips. Full state, multi-song, empty DB, processor state, score steps, global bindings, custom actions, device control groups, audio config.
- **Integration tests (10)**: Coordinator→state→EngineSync→engine path. Track/bus CRUD, gains, effects, song switching, persistence through coordinator.
- **EngineSync tests (11)**: Unit — state events → MockAudioEngine calls. Song load, track rename, effect CRUD, instrument changes, audioEnabled, song switching, binding changes.
- **Audio device config tests (4)**: Integration — device name persistence, config round-trip, audioEnabled persistence and sync.
- **Sequencer tests (10)**: Unit — transport play/stop, tempo clamping, beat position advance, loop wrap, time signature, beat/transport callbacks, capabilities.
- **Arrangement tests (6)**: Unit — region CRUD, MIDI event scanning, range filtering, recording lifecycle with take capture.
- **UndoHistory tests (9)**: Unit — push/undo/redo, max steps trimming, suspend/resume, clear, empty-state edge cases.
- **SongRuntime tests (11)**: Unit — MIDI dispatch exact match, wildcard any-device, wildcard any-channel, combined wildcards, note on/off velocity normalization, pitch bend normalization, remove/clear bindings, multiple handlers.
- **Arrangement extended tests (6)**: Unit — move region (same/cross track), duplicate, split with note splitting, quantize snap with per-note shift, looped region repetition.
- **Persistence extended tests (11)**: Integration — stub binding round-trips, duplicate device control rejection, region looped/quantize fields, **takes with MIDI events round-trip, WAL-mode backup file validity (via sqlite3_backup_*), multi-cycle save/reopen, binary-safe processor state blobs, multi-cycle save with plugin-referencing tracks (FK regression), createDefaultSong full round-trip (FK regression), save failure rolls back and preserves prior state, full coordinator shutdown→relaunch with recorded region**.

**Coverage gaps** (components with zero tests):
- AutomationEngine — interpolations, easing, delay
- MIDIEngine — MIDI routing, learn mode, device resolution
- LuaEngine, IPCServer — scripting and IPC layers
- Error handling — no tests for invalid inputs or error conditions
- GUI — not worth unit testing custom paint code

## Production Readiness (completed)

1. Beach ball → spinner overlay — semi-transparent overlay with message during save/load/song switch.
2. Directory permission prompt — explicit AU plugin paths, no getDefaultLocationsToSearch.
3. Audio buffer size / sample rate control — Settings window (Cmd+,) with Audio tab, also in sidebar, persisted.
4. MIDI device hot-plug — 4Hz polling, auto add/remove callbacks.
5. Error boundary — JUCE crash handler with emergency save.

(Remaining items moved to **Backlog** in `CLAUDE.md`.)

## Known Issues

**Functional:**
- AUShelfFilter crashes on instantiation — plugin bug.
- AUPitch: preset state restore doesn't take effect — AU bug.
- juce_String.cpp:327 assertion on startup — non-fatal, JUCE internals.
- No error handling on failed plugin loads (user sees nothing).
- State changes sometimes not visible until restart — watch for missed rebuildConnections/restoreBindings calls. (Partially addressed by the 2026-04-18 persistence hardening — saves now roll back loudly on FK violation instead of committing partial data, and `EngineSync::onTrackCreated` now applies `inputMonitoring` at load time. Other "not visible until restart" patterns may remain; investigate case-by-case.)
- Transport requires active audio device: beat clock runs in `GraphWrapper.processBlock`, so play/stop/position don't advance without an audio output device linked. Fix: fallback timer-based clock. Low priority — live use always has a device.
- DocumentWindow close button renders red despite custom LookAndFeel. JUCE's internal button drawing ignores TextButton colour overrides for the built-in close/minimize/maximize buttons. Fix: fully custom title bar component or custom LookAndFeel that overrides the specific draw method. Low priority cosmetic issue.

**Embedded Claude:**
- Bindings created via Claude/Lua may use wrong track names (case mismatch). GUI is more reliable.
- Custom action creation via Claude fails despite API working via direct IPC. Needs investigation (string escaping through chat→tool→IPC pipeline).

## Milestone v0.0.1 — Sequencer + production tools complete

MIDI + audio recording/playback, region management (select/move/copy/delete/mute), take folders, persistence, transport controls, auto-scroll, waveform display, multi-track recording, per-song tempo and time signature, preset morphing, metronome, flexible pane system, CC fader mapping. Git tag `v0.0.1`.

## Recent Additions (changelog)

- **Action track** — one per song, auto-created. Beat-triggered actions on the timeline. 3D spheres with duration tails, overlap-aware beehive layout. Double-click to create, drag to reposition, right-click to edit/delete. Persisted in `action_events` table.
- **Compound morph action** — bundles multiple sub-actions (parallel or sequential). `morphChain` for preset A → dwell → preset B transitions. MorphEditor UI with growing action slots.
- **Musical typing** — on-screen keyboard (Cmd+K). Logic-style key mapping, octave/velocity controls, sustain, draggable panel.
- **Track/region selection** — multi-track (Cmd/Shift click), multi-region selection. Track selection highlights header + selects all regions.
- **Region operations** — non-destructive quantize (right-click submenu), trim (drag edges), split at playhead (Cmd+T), join selected regions.
- **Smart grid snap** — all playhead clicks, region drags, and trims snap to division boundaries via `snapBeatToGrid()`. Shift+H/L steps by measure.
- **Cycle playback** — drag ruler to set cycle region, 'c' to toggle, 'u' to set from selection. Draggable cycle edges with resize cursor. Loop wrapping in GraphWrapper with targeted note flush (per-note bitset tracking, inline flush before scan to avoid same-sample noteOff/noteOn race). Playhead jumps to cycle start on play.
- **Ruler gutter** — dedicated playhead-setting area with adaptive bar numbers and multi-resolution tick marks. Grid clicks deselect only.
- **Note flush** — targeted noteOff using per-channel bitset of active notes. Loop flush via MidiSourceNodes only (not live MIDI path, which would race with new noteOns).
- **Fader improvements** — click-to-jump, handle center reaches full +6/-60dB range, fader/meter vertical alignment.
- **Preset name display** — editor window shows correct preset name (resolved from state on open, updated on save/load).
- **Region looping** — 'l' toggles loop, ghost copies with dashed borders and dimmed note previews. Loops until next region (or explicit loopEndBeat). Ghost right-edge resizable. Right-click ghost: Trim/Convert/Unloop. Looped regions repeat during playback via rep loop in scanMidiEvents.
- **Undo/redo** — `UndoHistory` with `deque<AppState>` (max 50 snapshots). `pushUndo()` before 39 undoable StateAPI mutations. Transactions (beginTransaction/endTransaction) for fader drags. Suspended during recording (pre-recording snapshot pushed, resumed on stop). Post-restore: arrangement pointer + audio reload + tempo sync + automation cancel. Cmd+Z / Cmd+Shift+Z.
- **Menu bar** — File/Edit/Track/View/Transport. Right-aligned gray shortcut text via NSAttributedString with tab stops. All commands dispatch through KeyBindingManager.
- **Song deletion** — right-click song in sidebar (except Sandbox). Switches to Sandbox first if deleting current song.
- **Keybinding system** — `KeyBindingManager` with 36 commands across File/Edit/Transport/View/Region/Track. User overrides stored in config, loaded on startup. `KeyBindingEditor` modal: categorized outline, click-to-capture, conflict detection, restore defaults. `handleGlobalKey` dispatches through manager — all shortcuts work globally (except when text editor focused). Pane toggle shortcuts: ⌘Y Produce, ⌘U Mappings, ⌘I Chat, ⌘⇧L Logs, ⌘O Mixer, ⌘P Sidebar.
- **Sidebar tabs** — Songs/Library/Actions/Devices. Tab-based navigation replaces flat tree. Active tab + content share lighter background. RegistryTree transparent. MIDI device entries are informational only (no selection).
- **DB backup** on every save (state.bak.db). Schema version tracking.
- **Performance Map rewrite** — two-pane Controllers + Song Mappings layout. Collapsible device tree, drag-and-drop between all areas (controllers → atemporal/score, atemporal → score, score → atemporal, score reorder with insertion line). Learn mode with port-aware device resolution, duplicate flash, IAC filtering. Inline name editing with up/down arrow navigation, group field with smart picker menu. Stub bindings (no action) persist. Score grows vertically, atemporal scrolls. `InlineEditor` re-entrancy guard (`isCommitting`) prevents double-commit on focusLost.
- **MIDI device auto-registration** — `PerformanceCoordinator::refreshMidiDevices` registers connected devices on startup and hot-plug. No manual registration step. IAC Driver buses filtered.
- **Input monitoring** — `inputMonitoring` flag on audio input tracks. Disconnects live audio input from track signal chain when off. "I" pill button in arrange view, toggle in mixer.
- **Solo and Mute** — runtime flags on TrackState. Mute via `GainProcessor.setMuted()` (preserves user gain). Solo via `AudioEngine::updateMuteStates()` (mutes all non-soloed when any soloed). M/S pill buttons: mixer bottom row, arrange view row 2.
- **Theme redesign** — semantic color tokens, unified surface hierarchy (bgApp → bgSurface → bgSlot), neutral track headers (no per-track color), two-row track headers with pill buttons, subdued pill colors, spacing/typography scales. All hardcoded colors migrated to Theme.h tokens.
- **Fail-loud assertions** — `PERF_ASSERT` macro, asserting helpers `song()`/`track()`/`bus()`/`device()` in StateAPI, assertions in EngineSync and Arrangement. Silent failures replaced with crashes for programming errors.
- **Audio region persistence** — takes table now stores file_path, record_tempo, sample_rate, channel_count.
- **Persistence hardening (2026-04-18)** — resolved a long-latent data-loss regression where first-session recordings disappeared after quit. Root cause: `createDefaultSong` passed the DLS plugin's display name as the `presetId` argument to `setTrackPlugin`, which put a non-UUID string into `tracks.preset_id`. Every save then failed `SQLITE_CONSTRAINT_FOREIGNKEY` (ext=787) on the preset_id FK, rolled back the entire transaction, and left the DB empty. The failure had been silent for months. Supporting fixes all landed together:
  - `saveFrom` now returns bool. `stepWrite` helper replaces bare `sqlite3_step` in the save path, captures SQLite errors, logs the expanded SQL on failure, and trips a class-level `saveHadError` flag.
  - `saveFrom` checks the flag before COMMIT — any write error triggers ROLLBACK. DB is guaranteed to stay in its last-committed state on any failure.
  - `exec()` now returns bool; `BEGIN` / `COMMIT` / `ROLLBACK` are all checked.
  - Save path reordered: new `clearAllData()` deletes every row in child-to-parent order first; catalog writes (plugins, presets, actions) are plain `INSERT` into empty tables instead of `INSERT OR REPLACE` (which triggered implicit DELETEs that cascaded into FK violations).
  - WAL-mode backup rewritten to use `sqlite3_backup_*`; the previous `juce::File::copyFileTo` copied only the main `.db` file and not the `-wal` sidecar, producing empty `state.bak.db` files.
  - `col_str` uses `sqlite3_column_bytes` for explicit length; all `processor_state` binds pass `.data()+.size()` instead of `.c_str()+(-1)`. Binary-safe round-trip for blobs (production base64-encodes, so this is defense-in-depth).
  - `EngineSync::onTrackCreated` now applies `inputMonitoring` on audio input track creation — previously the engine's default (`true`) silently overrode a persisted `false` on reload.
  - `PerformanceCoordinator::loadSong` calls `loadAudioFilesIntoEngine` at the end — previously audio regions came back visually on relaunch but silent and waveformless because the WAV wasn't loaded into the engine's AudioFileNode.
- **Offline bounce spike** — `bounce(path, startBeat, endBeat)` Lua binding renders the arrangement faster-than-realtime to a stereo WAV. Engine pauses during render (`AudioEngine::pauseDeviceProcessing`), `GraphWrapper::processBlock` is driven in a tight loop on a caller-owned thread. DLS renders ~76× realtime. Works with heavy plugins. Punts documented in CLAUDE.md §4: constant tempo, automation frozen, no tail time, master output only.
- **Safe defaults + dB-native Lua API** — gain setters clamp to [0.0, 2.0] in StateAPI (matches fader range); below -60dB snaps to exact 0.0 so the fader floor is true silence. `setTrackGainDb` / `setBusGainDb` / `addSendDb` / `setSendGainDb` convert dB→linear internally. System prompt codifies safe defaults for Claude-created tracks / busses / sends (audio input tracks have no input + monitoring off; new busses silent; new sends at -12dB).
- **Embedded Claude refinements** — system prompt rewritten, bundled as BinaryData so it ships with the binary. Silent Lua-lookup failures (track/bus/plugin not found) replaced with `std::runtime_error` throws that propagate to Claude as tool errors. ChatView hides raw Lua / tool-error bubbles (users see only assistant text). Three-dots typing indicator while Claude is working. Ambiguity guidance for plugin-name matches.

## LOC

~25,400 lines of source code (headers + implementation + tests). See `find src tests -name "*.h" -o -name "*.cpp" -o -name "*.mm" | xargs wc -l`.
