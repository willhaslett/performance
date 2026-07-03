# Audio Device Boundary — Scoping Doc (fixed engine rate vs. tactical resample)

**Status:** Scoping / decision doc. No code committed. Written 2026-07-02 to
size the "fixed engine rate" refactor (**Option B**) against the tactical
"resample at the file-read boundary" fix (**Option A**) for the Bluetooth
wrong-speed bug, and to see which lingering audio-engine TODOs a device-boundary
refactor would subsume.

Read alongside: `CLAUDE.md` (Backlog → the Bluetooth bug + audio-latency
items), `docs/ARCHITECTURE.md` (audio graph), `src/engine/AudioEngine.mm`,
`src/engine/AudioFileNode.h`, `src/engine/GraphWrapper.h`,
`src/rendering/OfflineRenderer.cpp`.

---

## 1. The triggering bug

Selecting Bluetooth headphones for output makes recorded/imported audio play
at the wrong speed and garble. Root cause, confirmed in code:

- `AudioFileNode::processBlock` (`src/engine/AudioFileNode.h:82-97`) anchors the
  file read position from the transport beat each block (`filePos = seconds *
  fileSampleRate`), then reads **`numSamples` file frames** (the output block
  size) and copies them **1:1** into the output buffer.
- When the file's stored rate ≠ the output device rate, each block plays
  `numSamples/fileRate` seconds of content over `numSamples/deviceRate` seconds
  of real time → speed error of `deviceRate/fileRate`. The per-block re-anchor
  then skips/repeats source frames at block boundaries → garble.
- Bluetooth forces a non-48k device rate (A2DP 44.1/48k; **HFP/SCO 16k or 8k**
  whenever a Bluetooth mic is active), so a 48k-captured file mismatches
  immediately.

## 2. The one fact that decides everything

**`AudioFileNode` is the only thing on the audio thread that reads stored audio
frames.** Verified: it is the sole holder of a `juce::AudioFormatReader` in
`src/engine/` (`grep AudioFormatReader src/engine/` → only `AudioFileNode.h`).

Everything else in the graph is **rate-agnostic**:

- `MidiSourceNode` renders live (MIDI events → hosted synth).
- Plugins/effects render live; JUCE re-prepares them at the current rate.
- Live input monitoring is passthrough at the device rate.
- The internal sequencer works in beats/seconds, not samples.

These are all re-prepared at the device rate on `rebuildGraph` and produce
correct output at whatever rate they're handed. **The only asset in the entire
engine carrying an intrinsic, frozen sample rate is a recorded/imported audio
file.** That is where — and the only place where — a device rate change creates
a correctness mismatch.

## 3. Current model: device-follows-engine

`src/engine/AudioEngine.mm`:

- `initialise()` → `deviceManager.initialiseWithDefaultDevices(2,2)` — takes the
  device's **current** rate; we never request a fixed rate.
- `setupGraph()` (`:219-253`) preps the graph + `GraphWrapper` at
  `device->getCurrentSampleRate()` / `getCurrentBufferSizeSamples()`, then drives
  it via a `juce::AudioProcessorPlayer` added as a device callback.
- `rebuildGraph()` (`:257-320`) tears down IO nodes and re-preps **everything at
  the new device rate** on any device change (`changeListenerCallback:344`).
- `OfflineRenderer` (`:39`) renders at `engine.getCurrentSampleRate()` — i.e.
  the device rate — so **bounce inherits whatever the device is doing** (bounce
  while on SCO → 16k export). This is a second, independent bug.

There is **no sample-rate converter anywhere** in the pipeline. JUCE hands us
the device's actual rate in the callback and does not resample for us.

---

## 4. Option A — resample at the file-read boundary (tactical)

Keep device-follows-engine. Teach `AudioFileNode` to convert
`fileSampleRate → outputSampleRate` per block. Because the node re-anchors
position from the beat each block, the natural fit is **stateless
fractional-index interpolation**: read `ceil(numSamples · fileRate/deviceRate)`
source frames from the beat-derived position, then for each output sample `i`
sample the source at `srcPos0 + i·(fileRate/deviceRate)` with a cubic
(Catmull-Rom) or windowed-sinc kernel. Phase-continuous across blocks because
position comes from the continuous beat. Preserve existing boundary fades and
mono→stereo copy.

**Scope:** one file (`AudioFileNode.h`) + tests. Also pin the bounce rate
(`OfflineRenderer`) independent of device — small, separate.

**Touches:** file-playback path only. Does **not** touch live monitoring,
plugins, MIDI, or the device callback.

**What it fixes:** the reported bug, completely and correctly. Conversion
quality is a kernel choice, orthogonal to A/B — a good kernel here is the same
operation pro DAWs use for file→session conversion.

**What it leaves on the table:** the engine rate still lurches on device change
(brief glitch as the whole graph re-preps); plugins/latency behave slightly
differently on different devices; bounce still needs its own rate pin.

**Reusability:** the resampler written for A is exactly the `file→rate`
conversion a B world also needs (`file→engine` instead of `file→device`). A is
**not throwaway** if we later do B.

---

## 5. Option B — fixed engine rate + explicit device boundary (architectural)

Pick a canonical **engine rate** (e.g. 48 kHz, or a per-project setting). Run
the entire graph at that rate **always**. Convert at two well-defined
boundaries, so the middle never changes rate:

```
 files ──file→engine SRC──┐
 live in ─dev→engine SRC──┤                         ┌─engine→dev SRC─ device out
                          ├──►  GRAPH @ engineRate ─┤
 plugins/synths (native) ─┘                         └─(record tap) engine→? store
```

This is how Logic/Pro Tools/Ableton keep processing stable: one project rate,
convert once per boundary.

### 5a. The hard part: the engine↔device boundary in JUCE

JUCE gives us the device's **actual** rate in the callback and does **not**
insert an SRC. So we own the converter. Two sub-approaches:

**B1 — Force the device to the engine rate.**
`deviceManager.setAudioDeviceSetup()` with `sampleRate = engineRate`. If the
device supports it (nearly all interfaces do 48k; A2DP does 44.1/48k), CoreAudio
runs the device at engine rate and no SRC is needed. **Fails for SCO 16k** (the
device physically cannot do 48k), and macOS may or may not honor a 48k request
on a 44.1k-preferred device — **needs empirical verification per device**. So B1
alone is insufficient; it must fall back to B2 whenever the device refuses the
engine rate.

**B2 — Custom resampling device callback (the real work).**
Replace `AudioProcessorPlayer` with our own `AudioIODeviceCallback` that:
- pulls audio from the graph at `engineRate`,
- resamples `engineRate → deviceRate` for output and `deviceRate → engineRate`
  for input,
- bridges the **block-size mismatch** via ring buffers — at 48k-engine /
  44.1k-device the frames-per-callback don't divide evenly, so this is
  **asynchronous SRC**: pull a *variable* number of engine frames to satisfy
  each fixed device callback.

Async SRC + ring buffer on the real-time thread is the classic hazard zone:
underruns/overruns, long-run clock drift between engine and device clocks,
added latency that must be measured and reported, and priming/latency at
start. This code runs for **every sound the app makes**, including live
monitoring — a bug here degrades everything, not just file playback.

### 5b. Ripple into other paths

- **Recording** (`PerformanceCoordinator::startRecording`, `AudioRecordFIFO`,
  `AudioWriterThread`): input arrives at device rate. Decide whether takes are
  stored at engine rate (convert on capture — clean, one rate on disk) or at
  device rate (convert on playback — but then we're back to per-file SRC). A
  fixed-rate world wants **store-at-engine-rate**, which means an input SRC on
  the capture path and a change to the record commit math.
- **Import** (`importAudioFile`): already transcodes — retarget from device rate
  to **engine rate**. Small, and actually simpler/stabler than today.
- **Bounce** (`OfflineRenderer`): render at engine rate (or an explicit export
  rate), never the device rate. Fixes the SCO-export bug for free.
- **Latency**: with a stable engine rate, plugin latency + device
  `getInput/OutputLatencyInSamples` compensation becomes well-defined and
  device-change-stable. This is where the **input-latency-compensation** TODO
  and the **built-in-mic 50ms** investigation naturally live.

### 5c. Risk summary for B

| Risk | Severity |
|---|---|
| Async SRC + ring buffer on the RT thread, all audio | High — hardest code we'd own |
| Clock drift (engine vs device) over long sessions | High — needs drift-corrected SRC or resync |
| Latency accounting + reporting must be rebuilt | Medium |
| Record/import/bounce commit-math changes | Medium |
| Glitch/priming on device change (vs A's re-prep glitch) | Low–Medium |
| Testing: RT audio, no graph mock today (known gap) | Medium |
| macOS device-rate-request behavior is empirical (B1) | Medium — must verify per device class |

### 5d. Suggested phasing (if B is chosen)

1. **Spike B1** — force engine rate on the device; measure which device classes
   honor it (interface, built-in, A2DP, SCO). If most honor it, the SRC path is
   only needed for the stubborn minority. *(De-risks the whole thing cheaply.)*
2. **Engine-rate constant + graph decoupled from device rate** — graph always
   preps at engine rate; device callback still 1:1 **only when rates match**
   (equivalent to today for matched rates).
3. **Output async SRC + ring buffer** (B2, output only) — the core RT component,
   behind the "rates differ" path. Test with a forced-mismatch harness.
4. **Input async SRC** — for recording/monitoring at mismatched rates.
5. **Store-at-engine-rate** on capture; retarget import; pin bounce rate.
6. **Latency compensation** — input+output comp on record commit; honest
   latency reporting; fold in the built-in-mic investigation.

Phases 1–2 are cheap and reversible. Phase 3 is the real commitment.

---

## 6. What B subsumes from the lingering audio TODOs

A device-boundary refactor is the umbrella for a cluster of open items:

- **Bluetooth wrong-rate playback** (this bug) — fixed by A *or* B.
- **Bounce follows device rate** (`OfflineRenderer:39`) — export at 16k on SCO.
  Fixable standalone (pin the rate) or by B.
- **Buffer-size dropdown lies** (requests 128, gets 512, UI shows 128) — honest
  device-config reporting belongs to this layer.
- **Input/output latency compensation on record commit** — well-defined only
  with a stable engine rate + honest device latency.
- **Built-in Mac mic 50ms stream latency** (AVAudioEngine vs AUHAL) — a
  device-path question that lands in the same layer.

**Not** part of this layer (separate projects): out-of-process AU plugin hosting
(stability), stuck-notes-on-stop (looper event ordering), TempoMap runtime
(sequencer).

---

## 7. Recommendation / decision criteria

- **If the goal is "fix the reported bug, professional-grade":** Option A with a
  high-quality kernel is correct and sufficient — because files are the only
  rate-locked asset, resampling at the file-read boundary puts the conversion
  exactly where the mismatch physically is. Plus pin the bounce rate. Low risk,
  contained, reusable.
- **If the goal is "device-independent processing + latency correctness as a
  platform property":** Option B is the right long-term architecture, but it is
  a real-time-critical, all-audio refactor (async SRC + ring buffer) whose
  distinguishing benefit is processing stability, **not** better file playback.
  It should be chosen on those merits and phased (spike B1 first), not adopted
  as a bugfix.

**A and B are compatible.** Doing A now costs nothing if B follows: the
file-rate resampler carries forward verbatim. The honest sequencing is A now
(+ bounce-rate pin), and commit to B only when device-independent processing is
itself the goal — starting with the cheap B1 spike to see how much of the async
SRC we can avoid.

## 8. Open questions

1. Does macOS honor a 48k `setAudioDeviceSetup` request on 44.1k-preferred
   devices (A2DP, some interfaces)? (Fast-path viability — empirical, per
   device; answered during Phase 3 click-test.)
2. Drift strategy for async SRC: drift-corrected ratio vs periodic resync?
   (Decided in Phase 2 against the mock-device drift test.)

---

## 9. Decision & Implementation Plan — locked 2026-07-02

**Decision: build Option B now**, on branch `audio-device-boundary`, gated by
strong tests + a click-test matrix; do not merge unless it earns it. Locked
choices:

- **Engine rate is per-project.** `SongState` carries its own sample rate
  (44.1/48/88.2/96k), default 48k. **No migration** — no important projects
  exist yet; testers reset. New field flows through StateAPI + persistence.
- **Store audio at engine rate.** Recordings convert device-rate input → engine
  rate on capture; import transcodes to engine rate. Disk holds one rate per
  project; playback is 1:1 when device == engine.
- **Resampler = JUCE `WindowedSinc`** (`juce::Interpolators`) + `AbstractFifo`
  ring buffer. No hand-rolled DSP.
- **Fast path:** request the engine rate on the device (`setAudioDeviceSetup`);
  run the async SRC only when the device refuses (SCO-class). Capable devices
  pay zero SRC cost.
- **Bounce** renders at the engine rate (or an explicit export rate), never the
  live device rate.

### Test engineering (the safety net)

- **Pure SRC component** (`DeviceRateBridge` — WindowedSinc + ring buffer, no
  device dep). Unit tests: frequency preservation (Goertzel peak unchanged),
  length ratio, block-boundary continuity, aliasing floor.
- **Mock-device async-SRC test** — fake `AudioIODevice` clocked at rate Y vs
  engine X, driven for a long simulated duration; assert ring-buffer fill stays
  bounded (no underrun/overflow) → proves ratio + drift handling.
- **Offline graph-at-engine-rate** — extend `OfflineRenderer` assertions;
  record/import round-trips preserve frequency + duration.
- **Click-test matrix** (hardware-only): built-in 48k · interface 48k ·
  interface 44.1k · A2DP BT · **SCO BT (AirPods + mic)** · device-switch
  mid-playback · record on each · bounce on each.

### Phases (milestone check-in at each ✅)

1. **Per-project engine rate through state + persistence** (no audio-thread
   change). `SongState.sampleRate`, StateAPI get/set, DB column, tests. ✅
2. **`DeviceRateBridge` pure component + unit tests** (no wiring). ✅
3. **Graph decoupled from device rate + fast path.** Graph always preps at the
   song's engine rate; on device setup/change, request engine rate; if honored,
   1:1 callback (≡ today). If not, fall back to running at device rate *for now*
   (safe interim until Phase 4). ✅
4. **Output async SRC** — wire `DeviceRateBridge` into a custom device callback
   for the device≠engine case. Click-test SCO output. ✅
5. **Input async SRC + store-at-engine-rate** — record path converts to engine
   rate; import retargets; commit math updated. ✅
6. **Bounce at engine rate + latency compensation** — `OfflineRenderer` rate
   pin; input/output latency comp on record commit; fold in the built-in-mic
   investigation. ✅

Each phase builds + tests green before the next. Phases 1–3 are low-risk and
reversible; Phase 4 is the real-time commitment.
