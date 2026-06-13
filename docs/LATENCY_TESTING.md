# Recording Latency — Empirical Testing

**Branch:** `latency-testing`. Started 2026-06-13. Pure data-gathering — no code changes. Will runs the recordings; Claude analyzes the WAVs and keeps this doc current.

## Questions we're answering

1. **Is the problem real?** (Or imagined.)
2. **What is the delay?** (How many ms.)
3. **What type / in what scenarios does it appear?** (Which device, which configuration.)
4. **Pay dirt: is it present in a non-Apple app too?** (Us, or the platform.)

No hypotheses are being tested here. We gather numbers, then decide.

## Measurement

**Acoustic round-trip offset.** Performance's built-in metronome plays clicks out the speakers into the room; the room mic records them back. We measure the gap between each click's intended position (song-time grid) and where its recorded copy actually lands. That gap = full round-trip latency (output + acoustic flight + input).

The comparison is a 2×2: **app × device**. External interface is the control (expected low, app-independent); built-in is the suspected case.

### Fixed conditions (hold constant across all cells)
- **Sample rate:** _TBD_ (pick one, e.g. 48 kHz) — same everywhere.
- **Buffer size:** _TBD_ — and record the **actual** buffer size in effect, not the requested setting (the UI is known to misreport it).
- **Latency compensation OFF** in both apps (Audacity's recording-latency correction set to 0).
- **Same physical mic/speaker placement** for every cell.
- **~10 clicks per cell** so we read a distribution, not a single shot. (How many we ultimately need is a decision we make from the data, not up front.)

### Foil app
**Audacity** — its macOS path (PortAudio → AUHAL) is the closest to ours, so it's the cleanest test of "us vs. the platform."

## Results

Round-trip offset, mean ± spread (ms), n = trials. Buffer/SR noted per run.

| | Built-in mic + speakers | External interface |
|---|---|---|
| **Performance** | _pending_ | _pending_ |
| **Audacity** | _pending_ | _pending_ |

**Derived:** built-in − external (per app) = the built-in-specific penalty, if any.

## Per-run log

(Each recording session: device, app, SR, actual buffer size, raw per-click offsets, notes. Filled as we go.)

_none yet_

## Status / next step

→ **NEXT:** First capture — Performance on the **built-in** device (mic + speakers; no extra gear). See chat for the exact steps. Once analyzed, do the **external** cell as the control, then the two Audacity cells.
