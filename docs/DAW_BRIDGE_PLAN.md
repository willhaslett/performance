# DAW Integration & Sequencer Plan

Forward-looking design for external DAW control. Not yet implemented beyond the InternalSequencer (Phase 1, partial).

## Landscape (research summary)

**External control paths per DAW:**
- **Logic**: MCU over virtual MIDI only. No API, no OSC. AppleScript dead.
- **Ableton Live**: Max for Live + OSC bridge for full control. Ableton Link for tempo sync. Remote Scripts (undocumented Python) for MIDI-triggered control.
- **Reaper**: Native OSC (configurable), ReaScript (1500+ API functions), HTTP interface. Best external story.
- **Bitwig**: DrivenByMoss + OSC. Native Controller API (JavaScript) is solid but MIDI-input-driven.

**Universal protocols:**
- MCU: works everywhere, gives transport + 8-channel mixer + banking. No clip triggering.
- OSC: no standard schema, per-DAW mapping. Low latency, float-native.
- Ableton Link: open-source C++ tempo/phase sync. Cross-app, reliable. Not a control protocol.
- MIDI 2.0 Property Exchange: early, no DAW adoption yet. Watch, don't build on.
- Rewire: dead.

## Design: three-tier DAW bridge

```
Performance App
    ↓
DAWBridge protocol (internal C++ interface)
    ↓ implementations:
┌─ MCUBridge (virtual MIDI, works with any DAW)
├─ OSCBridge (configurable address space, native in Reaper)
├─ M4LBridge (Max for Live relay for Ableton)
├─ ReaScriptBridge (ReaScript IPC for Reaper)
└─ InternalSequencer (our own, no external DAW)
```

`DAWBridge` defines operations in our domain — implementations translate to whatever the DAW speaks. Caller never knows the transport.

## DAWBridge interface (draft)

```
Transport: play, stop, record, setTempo, getTempo, getPosition, setLoop
Tracks: armTrack, muteTrack, soloTrack, setTrackGain, getTrackGain
Clips: triggerClip, stopClip, getClipState (Ableton/Bitwig only via deep integration)
Mixer: setTrackPan, setTrackSend
Markers: gotoMarker, nextMarker, prevMarker
Sync: link (Ableton Link for tempo/phase sync)
```

Clip triggering is DAW-specific (MCU can't do it). The interface should make it optional — callers check capability.

## Implementation plan

**Phase 1: Internal Sequencer (no external DAW)**
- Define `DAWBridge` as a C++ abstract class in `src/daw/DAWBridge.h`
- Implement `InternalSequencer` as the first backend — our own transport, tempo, beat clock
- Add a transport bar to the UI (play/stop/record/tempo/position)
- Drive tempo from internal clock, sync audio callback to beat position
- This gives us a metronome and beat-synced automations without any DAW

**Phase 2: Ableton Link**
- Embed the Link library (header-only, Apache 2.0)
- `LinkBridge` syncs our internal tempo/phase to Link-enabled apps
- No control of other apps — just tempo lock

**Phase 3: MCU bridge**
- `MCUBridge` sends/receives MCU protocol over CoreMIDI virtual port
- Any DAW sees us as a Mackie control surface
- Gets transport + mixer for free in Logic, Reaper, Live, Bitwig

**Phase 4: OSC bridge**
- `OSCBridge` sends/receives OSC over UDP
- Ship with Reaper .ReaperOSC mapping file
- Community can write mappings for other DAWs

**Phase 5: Deep integration (if warranted)**
- M4L device for Ableton (clip triggering, full LOM access)
- ReaScript for Reaper (direct API calls)
- Each is a separate adapter behind `DAWBridge`

## Constraints
- `DAWBridge` lives in `src/daw/` — clean boundary, no tentacles into `src/engine/` or `src/api/`
- The audio engine continues to own all plugin hosting and audio routing
- The sequencer is a consumer of `StateAPI` (writes tempo/position to state) and `AudioEngine` (for metronome/click)
- If the bridge fails or disconnects, the app continues functioning — degraded, not broken
