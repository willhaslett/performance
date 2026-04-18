# Persistence Data-Loss Incident — 2026-04-18

**Severity:** High — 100% data loss for first-session users.
**Resolved at commit:** `d3e4f7d` (root cause) + surrounding work through `354d886`.
**Tests added to guard against recurrence:** 9 across StateAPI / Persistence / EngineSync.

This document is both the incident retro and a **forward-looking hardening plan**. The one-line bug was trivial; the five concentric layers of undetection that let it live for months are the real story. The remediation list at the end is treated as load-bearing — items there are the debt a business cannot be built on top of without addressing.

---

## TL;DR

A one-line typo in `PerformanceCoordinator::createDefaultSong` passed a plugin display name as a preset UUID. Every save of a first-session user's recordings failed a foreign-key check, the entire transaction rolled back, and the DB looked empty on next launch. `restoreSession` re-created the default song, which masked the loss as "it's fine, DLS is still there."

The bug was hidden by **no error checking in the save path** (silent failure), **stringly-typed IDs** (no compile-time help), **round-trip-only tests** (no flow-level coverage), and **a restore-on-empty behavior that auto-heals the symptom** (invisibility).

---

## The incident

**What the user saw.** Launch app. Record audio + MIDI. Playback confirmed. Quit. Relaunch. Regions gone.

**What actually happened.** First launch created the default song (`Untitled` with Electric Piano + Audio In). DLS loaded successfully. `captureProcessorState` saved the plugin blob as base64 text. User recorded. **Every autosave failed silently** with `SQLITE_CONSTRAINT_FOREIGNKEY` (ext=787) on `tracks.preset_id`, rolled back the entire transaction. The DB stayed at its previous state. On quit, the shutdown-save failed the same way. On relaunch, `persistence->loadInto` read zero songs, `restoreSession` fell into the "zero songs → create default" branch, and the user saw a fresh Untitled — indistinguishable at a glance from "it persisted."

**Detection.** Surfaced only because the user reported "regions gone on relaunch" during beta-prep testing. There's no telemetry-level signal that would have caught this in production before a user noticed.

**Resolution timeline (compressed).**

1. Audited test suite for persistence gaps. Wrote 4 failing tests.
2. Tests caught two latent bugs (WAL-mode backup, binary-blob truncation). Fixed them — but determined they didn't explain the user's symptom.
3. Added save-side error checking — `exec`/`stepWrite` now log and trip a `saveHadError` flag; `saveFrom` checks it and rolls back.
4. Error checking immediately exposed `SQLITE_CONSTRAINT_FOREIGNKEY` on every save.
5. Hypothesized and fixed: `INSERT OR REPLACE` on the `plugins` table triggered an implicit DELETE that failed when tracks still referenced the plugin. Fixed by reordering the save — `clearAllData()` deletes all rows first, then plain `INSERT` into empty tables.
6. Tests passed. Production still failed.
7. Added expanded-SQL logging to `stepWrite`.
8. One log line revealed the root cause: `INSERT INTO tracks ... preset_id='DLSMusicDevice' ...` — a plugin name where a preset UUID was expected.
9. One-line fix in `PerformanceCoordinator::createDefaultSong`.
10. Audio regions surfaced a further bug — `loadSong` didn't call `loadAudioFilesIntoEngine`. One-line fix.
11. Along the way, fixed a related regression in `EngineSync::onTrackCreated` not applying `inputMonitoring`.

**Total elapsed:** ~5 hours of investigation + fixes.

---

## 5 whys

1. **Why did users lose their recordings?** Every save failed to commit.
2. **Why?** `INSERT INTO tracks` violated `tracks.preset_id` FK — the value was `"DLSMusicDevice"`, a plugin name, not a preset UUID.
3. **Why?** `createDefaultSong` called `setTrackPlugin(trackId, p.id, p.name)`. The signature grew a `presetId` third parameter at some point and this caller wasn't updated.
4. **Why wasn't this caught by tests?**
   1. `saveFrom` returned void — tests could not check whether saves actually succeeded.
   2. No test exercised `createDefaultSong → save → relaunch`.
   3. Round-trip tests used fresh `StateAPI` objects with no plugin/preset references, sidestepping the FK entirely.
5. **Why did it stay hidden for months in production?** Because the persistence layer had zero error checking across ~22 write statements. `sqlite3_step` returning `SQLITE_CONSTRAINT` was ignored. COMMIT then succeeded (the failing row was just missing), the DB looked "normal but empty" on reload, and `restoreSession → createDefaultSong` silently replaced the lost state with a new default — making the loss invisible to anyone not looking for their specific data.

**The pattern worth internalizing:** five concentric circles of missed defense. The compiler. The tests. The runtime. The user-visible state. The detection telemetry. *Every single one* was open; the bug walked straight through them. Fixing any one would have caught this within hours instead of months.

---

## Architectural critique, in rough order of risk

Written with an outside-expert hat on. These are *implementation* concerns, not *design* concerns — the broader architecture (SQLite + in-memory state store + periodic save + event bus + EngineSync) is sound.

1. **Stringly-typed IDs everywhere.** Every entity ID is `std::string`. The compiler cannot distinguish `PluginId` from `PresetId` from `TrackId` from any other UUID-shaped string. The root-cause typo was a direct manifestation of this. **Single highest-leverage change** — a newtype pattern (`struct PresetId { std::string v; };` with explicit constructors) makes the bug class a compile error across the entire codebase.

2. **Silent-failure culture in persistence.** Pre-incident: zero error checks across the save path. `exec()` returned void. `saveFrom` returned void. Every `sqlite3_step` was unchecked. Post-incident: checks exist, `saveHadError` flag trips rollback, `saveFrom` returns `bool`. We're 70% of the way to production-grade error handling but not all the way — the flag pattern should be replaced by proper `Result<T, Error>` propagation so callers can surface *what* failed.

3. **Hand-written SQL for every statement.** ~1000 lines of `prepare/bind_text/bind_int/bind_double/step/finalize` with manually-tracked column indices. High cognitive load, high bug potential. Not an ORM argument — a thin per-entity mapper (serialize/deserialize) would centralize the risk surface without introducing heavyweight dependencies.

4. **Schema vs. C++ struct sync is ad-hoc.** `StateModel.h`, the `CREATE TABLE` SQL, and the per-field bind/read code are three parallel definitions that must stay in sync. Adding a field means editing three places with no compile-time cross-check. The null-byte truncation bug we found today was a consequence: `processor_state` is declared `TEXT` but (until today's fix) stored as text via `bind_text(-1)`, while production was base64-encoding binary. Three places declaring three subtly different things.

5. **`INSERT OR REPLACE` triggering implicit DELETE.** The first FK violation we chased was a direct consequence of `INSERT OR REPLACE` semantics (delete-then-insert). Safer patterns (explicit UPSERT, UPDATE-or-INSERT) don't have this footgun. We inherited it because we're hand-rolling the SQL.

6. **Blanket delete-and-reinsert on every save.** Every autosave (every ~3 seconds during editing) `DELETE FROM` every table and re-writes every row. Works at our scale, scales poorly, and is fragile — any one row's constraint violation poisons the entire save. A differential save (only write what the event bus says changed) would be faster and more resilient.

7. **No schema health check at startup.** We could run `PRAGMA foreign_key_check` when opening the DB. Would surface any violation present in the on-disk data before any save attempts. Five minutes of code, real upside.

8. **State→Engine sync cherry-picks fields.** `onTrackCreated` applies a hand-picked subset of state fields; `onEntityUpdated` applies a different (superset) list. The input-monitoring regression we fixed today was a pure consequence — the engine's default `true` quietly overrode persisted `false` because `onTrackCreated` didn't re-apply that field. These two paths should be derived from a single declarative field list, or both should be "apply everything unconditionally." Cherry-picking is a latent bug generator.

9. **No verification that state actually synced to engine.** `stateAPI->replaceState(...)` fires one event. EngineSync reacts. Nothing verifies the engine received and applied everything. If a field gets dropped silently (as happened with `inputMonitoring`), there's no contract-level check that would catch it. A reconcile/audit pass would help.

---

## What we'd do differently from scratch

**Caveating explicitly: the architecture is right.** SQLite as persistence, in-memory state store, periodic save, event bus, engine-as-view-of-state — all correct for this app. None of these need to change. The problems are entirely in *how* the persistence and engine-sync layers are implemented.

Changes we'd make starting over:

- **Typed IDs from day one.** Every entity has its own ID type. Mixing them is a compile error.
- **Every persistence function returns a result type.** No void. No silent failure. Errors propagate, get logged, and can be surfaced to the user.
- **One serialization point per entity.** A mapper layer or generated code from the C++ structs. Schema, binds, and reads derive from one source of truth.
- **Integration tests modeled on user flows, not just unit round-trips.** `createDefaultSong → save → reopen` is the template. Every top-level flow gets one.
- **`foreign_key_check` on every open; `integrity_check` periodically.** Cheap safety net.
- **Incremental save path.** The event bus already tells us what changed; persist only that.
- **EngineSync's create and update paths are the same declarative list.** They can't drift because they're the same code, just with different triggers.

---

## Honest severity assessment

- **This was a severe incident.** 100% data loss for first-session users is a ship-stopper. If 4 friends had received the pre-fix build, all 4 would have hit it on their first recording, and at least some of them would have been annoyed enough to stop testing.
- **The fix was a single line.** The discovery was expensive because the architecture actively hid the problem.
- **We do not need to rewrite anything.** Today's fixes close the immediate wound. The architectural debt is real but bounded.
- **Some of the debt needs attention before we open the doors wider.** See the remediation list below — items marked P0/P1.
- **None of the fixes should be reverted.** The save-path error checking, the rollback semantics, the `clearAllData → plain INSERT` ordering, the expanded-SQL diagnostic logging — all stay.

---

## Remediation plan — prioritized

Every item from the critique, labeled with priority and effort estimate. This is the document of record for the hardening work the persistence and engine-sync layers still need. When triaging future work against 0.1.0 / 0.2.x scope, consult this list.

### Priority legend

- **P0 — ship-blocker for 0.1.0.** Must land before the 4-friends round.
- **P1 — strongly recommended before opening to strangers (0.2.x).** Failure here materially risks repeat incidents of the same class.
- **P2 — good investment, bounded effort, not urgent.** Compounds over time.
- **P3 — named debt.** Worth tracking; no immediate action.

### Already done this session (for reference)

- **`saveFrom` returns `bool`** and logs on failure. All callers check.
- **`stepWrite` helper** with expanded-SQL + extended-errcode logging on failure.
- **`saveHadError` flag** trips `ROLLBACK` on any write failure.
- **`clearAllData` reorder** — delete everything child-to-parent first, then plain `INSERT` (no more `INSERT OR REPLACE` → implicit DELETE → FK violation cascade).
- **WAL-safe backup** via `sqlite3_backup_*` — the previous file-copy was always an empty schema-only snapshot.
- **Binary-safe blob round-trip** — `col_str` uses `sqlite3_column_bytes`; `processor_state` binds use `.data()+.size()` instead of `.c_str()+(-1)`.
- **`EngineSync::onTrackCreated` applies `inputMonitoring`** on audio input track creation.
- **`loadSong` calls `loadAudioFilesIntoEngine`** so audio regions hydrate on song load.
- **Expanded SQL + extended errcode in the save-failure log line** — future FK-class bugs will be one-log-line diagnosable.
- **Regression tests locked in** — FK-pattern, multi-cycle plugin-referencing saves, `createDefaultSong → save → relaunch`, save-failure rollback preserves prior state, backup captures last committed save, take-with-MIDI-events round-trip, audio-input track `inputMonitoring` on load.

### P0 — before 0.1.0 ships

- **R1. Second-machine install + telemetry round-trip verification.**
  Tracked separately as §1 of the 0.1.0 plan. Not strictly a hardening item, but confirms today's fixes hold up in a fresh environment.
  *Effort: 1 hour.*
- **R2. Typed IDs for the state model.**
  Wrap `std::string` in per-entity newtype structs (`PluginId`, `PresetId`, `TrackId`, `BusId`, `SendId`, `EffectId`, `RegionId`, `TakeId`, `DeviceId`, `ActionId`, `SongId`). Explicit constructors; no implicit conversions. Catches the bug class that caused this incident at compile time, forever. Mechanical refactor — update `StateModel.h`, `StateAPI.h`, the Lua bindings at the boundary, and the persistence layer; compile-fix any mismatches.
  *Effort: 1 focused day. Closes an entire bug family.*

### P1 — before opening to strangers (0.2.x)

- **R3. Proper `Result<T, Error>` return types throughout persistence.**
  Replace the `saveHadError` flag with explicit propagation. Every `exec`, `prepare`, `bind`, `step` returns a result the caller checks. Errors carry enough context (statement name, sqlite errmsg, extended errcode) to diagnose without the expanded-SQL crutch.
  *Effort: 1 day. Foundation for R5/R6.*
- **R4. Declarative field application in EngineSync.**
  Define the list of fields that sync state → engine *once*, with a function per field. Both `onTrackCreated` and `onEntityUpdated` iterate that list. No cherry-picking, no drift. Removes the regression class that caused today's `inputMonitoring` bug.
  *Effort: ~half day.*
- **R5. `PRAGMA foreign_key_check` + `PRAGMA integrity_check` on DB open.**
  Run both at `PersistenceLayer::open` time before any other query. Log loudly on any violation. Future DB corruption surfaces at open instead of at next save.
  *Effort: 30 minutes.*
- **R6. End-to-end integration tests for every top-level user flow.**
  We have one today (`createDefaultSong → save → relaunch`). We need them for: record audio → save → reload → playback; record MIDI → same; create bus + send → save → reload; song switch; load a second song; undo across recording; etc. The template exists; this is a sustained investment.
  *Effort: ongoing; ~2–3 days for an initial useful batch.*

### P2 — good investment, not urgent

- **R7. Per-entity mapper layer.**
  One `serialize(Track, sqlite3_stmt*)` / `deserialize(sqlite3_stmt*, Track&)` pair per entity type. Centralizes schema-to-struct mapping. Cuts ~1000 lines of hand-written SQL to ~300 of declarative mapping. Reduces the number of places a schema change has to touch from 3 to 1.
  *Effort: 2–3 days. Touches every read/write path.*
- **R8. Incremental save driven by event bus.**
  Events already tell us exactly what changed. Persist only that. Faster autosaves; per-row failure isolation; less wasted disk I/O. Builds cleanly on R7.
  *Effort: 3–5 days. Requires R7 foundation to be clean.*
- **R9. Use SQLite's `BLOB` type for `processor_state` (not base64-encoded `TEXT`).**
  Smaller, simpler, avoids the text/binary confusion that led us to the null-byte-truncation bug. Schema migration required (existing `TEXT` columns need to be re-read, decoded, and re-written as blobs).
  *Effort: 1 day including migration.*
- **R10. Regenerate `BuildVersion.h` on every incremental build.**
  Today we lost ~30 minutes to a stale version string that made us unsure which binary was running. Fix: make the cmake step that generates `BuildVersion.h` always run, not conditionally.
  *Effort: 30 minutes.*

### P3 — named debt, track but don't act

- **R11. Audit for other silent-failure patterns** across the codebase — EngineSync, AudioEngine, AutomationEngine, IPCServer. Today we know persistence had them; unknown whether other subsystems do. Defer until we have a symptom.
- **R12. Audit for other "state-default drifts from engine-default" patterns.** `inputMonitoring` was one. `outputTarget` is a candidate. Any flag with different defaults in `StateModel` vs. `AudioEngine` structs is a potential silent-drift bug on load.
- **R13. Formal schema migration tool.** Today migrations are `ALTER TABLE ADD COLUMN` calls with no version tracking. Fine at our schema-version=1 stage, becomes a problem when we bump versions. Worth a design pass before shipping 0.1.0 to anyone who might have old DBs.

---

## Signals to watch for — debt recurrence warnings

If any of these surface in testing or telemetry, the debt is biting again and remediation priorities should escalate:

- Repeated `SAVE FAILED` or `step failed` log lines in production sessions.
- A user report of "my work isn't persisting."
- Any new regression test that fails against a recently-passing suite (especially in `EngineSyncTests` — that suite is the canary for state/engine sync drift).
- Schema mismatches surfacing during upgrade (DB from older version can't be loaded, or loads with dropped fields).
- Autosaves taking noticeably long on large songs (would motivate R8 incremental saves).

---

## What does NOT need to change

Enumerated so future readers don't accidentally redo work that's correct:

- **SQLite as the persistence choice.** Right answer for an embedded desktop app. Don't switch.
- **In-memory state store as runtime SSOT.** Right. Persistence is a side-channel.
- **Event bus between state and engine.** Right. The implementation bugs (R4, R9) are localized; the pattern itself is sound.
- **Engine as pure view of state.** Right. One writer (EngineSync), many readers.
- **Per-save full transaction with ROLLBACK on failure.** Right (and now actually implemented correctly).
- **Backup file written on every save.** Right (and now actually written correctly via `sqlite3_backup_*`).
- **Plugin catalog as a DB-level first-class entity.** Right.

---

## Commits for reference

```
354d886 Docs: capture today's persistence hardening + bounce spike + AI refinements
6b213b6 loadSong: load persisted audio region files into the engine
d3e4f7d Fix createDefaultSong passing plugin name as preset UUID (FK regression root)
1c50750 Persistence: clear all tables first, then plain INSERT — fix FK regression
7521920 EngineSync: apply inputMonitoring on audio input track creation
8571ec2 Persistence: error-check every save write, rollback instead of silent corruption
9fac7d3 Persistence: fix WAL-mode backup + binary-safe blob round-trip
59ce652 tests: add persistence coverage, exposes backup-file bug
```
