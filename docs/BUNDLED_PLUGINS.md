# Bundled Plugins — First-Launch Install Pack

The app ships as a "creative playground" but assumes nothing about the
tester's plugin library. Without a ready-to-play synth + reverb + delay
out of the box, a friend with a guitar or keyboard has a multitrack
recorder, not a playground. This document captures the plugin bundle we
ship, how it gets onto the tester's machine, how they remove it, and
what license/legal obligations we accept by shipping it.

Triggered by product reassessment during the 0.1.0 run-up: the
architecture is ready, the app is not — "not enough there to reward an
hour" was the honest read. Plugin bundle is the missing piece.

> Read this doc before adding or removing plugins from the bundle,
> touching the install/uninstall flow, or changing the signing pipeline.
> The license landscape is narrow and easy to violate by accident; the
> ruled-out list below is load-bearing.

## Why this work

Three observations framing the need:

1. **macOS ships no meaningful instruments.** DLSMusicDevice (our
   current default) is a polite GM soundfont synth and that's it. Any
   creative exploration requires the user already owns plugins.
2. **"Install these plugins first" is not a setup step musicians will
   tolerate.** Distributing a DMG that lets them open the app and then
   fails to produce any interesting sound until they download 10 other
   things is a non-starter.
3. **Permissively licensed, redistributable audio plugins are plentiful
   but the legal landscape is a minefield.** Most "free" plugins are
   freeware without redistribution rights (Valhalla, Klanghelm, TAL,
   u-he free, Arturia/NI Intro products). A previous attempt to build
   the mda suite from source went badly; the solution is to ship
   pre-built binaries, not to rebuild the world.

The fix: a first-launch "install the plugin pack?" prompt that copies
pre-built, codesigned `.component` bundles into the user's
`~/Library/Audio/Plug-Ins/Components/` directory, plus a clean
uninstall path in Settings. No DRM, no phone-home, no license keys.
Attribution handled via an about/licenses screen; GPL source obligations
handled via a `LICENSES/` directory shipped alongside the plugins.

## Design — the model after

### First-launch flow

1. **Detection.** On app launch, check for our bundle marker at
   `~/Library/Application Support/com.performance.app/bundled-plugins.json`.
   If the marker exists and all listed `.component` files are present,
   we're done — skip silently.
2. **Prompt.** If the marker is missing, show a themed modal:

   > **Install free plugin pack?**
   > Your friends don't have a plugin library. This app is more fun
   > with one. We've assembled 20 plugins (7 instruments, 13 effects)
   > from free, open-source projects, ready to play. ~150 MB.
   >
   > [Install] &nbsp;&nbsp; [Maybe later]

3. **Install.** On consent, copy each `.component` bundle from the app's
   `Resources/bundled-plugins/` into
   `~/Library/Audio/Plug-Ins/Components/`. Progress bar shown during the
   copy. Write `bundled-plugins.json` manifest recording each plugin's
   name, version, and destination path. Trigger an AU rescan so the new
   plugins appear immediately in the app's in-catalog list.
4. **Skip.** "Maybe later" marks a `skipped-at` timestamp in the
   manifest. App doesn't re-prompt unless the user explicitly asks.
   Menu: `Help → Install Plugin Pack…` always re-opens the dialog.

### Uninstall flow

Lives in `Settings → Plugins`:

```
┌───────────────────────────────────────────────────────┐
│ Bundled Plugins                                       │
│                                                       │
│ Installed automatically when you first launched       │
│ the app. Remove any you don't want.                   │
│                                                       │
│ ☐ Surge XT                      synth      [Remove]   │
│ ☐ Dexed                         FM synth   [Remove]   │
│ ☐ Odin 2                        synth      [Remove]   │
│ ☐ mda ePiano                    EP         [Remove]   │
│ ... (etc.)                                            │
│                                                       │
│ [Remove all bundled plugins]                          │
└───────────────────────────────────────────────────────┘
```

Each row's [Remove] deletes the `.component` file we placed and updates
the manifest. "Remove all" is one confirmation dialog, then same.
Never touches a plugin we didn't install — the manifest is the only
list of removable items.

### Manifest shape

```json
{
  "installedAt": "2026-04-21T10:30:00Z",
  "appVersion": "0.1.0",
  "plugins": [
    {
      "name": "Surge XT",
      "auIdentifier": "aumu SrgX Vmbg",
      "version": "1.3.4",
      "installedPath": "~/Library/Audio/Plug-Ins/Components/Surge XT.component",
      "sourceUrl": "https://github.com/surge-synthesizer/releases-xt/releases/tag/1.3.4",
      "license": "GPL3"
    },
    // ...
  ]
}
```

The path lets us verify existence at launch and safely remove what we
placed. `sourceUrl` + `license` power the attribution screen.

### Code signing & notarization

Every `.component` we ship must be codesigned with our Developer ID.
The app bundle itself gets the same — our `scripts/build-release.sh`
pipeline already handles this for the `.app`. Extending it:

1. `scripts/sign-bundled-plugins.sh` — iterates
   `Resources/bundled-plugins/*.component`, signs each with
   `codesign --timestamp --options runtime -s "Developer ID Application: William Haslett"`.
2. The plugin `.component` bundles are part of `Performance.app/Contents/Resources/bundled-plugins/`.
   They get notarized as part of the app's notarization — Apple treats
   the whole `.app` as one notarization unit.
3. At first-launch install time, the files we copy out are already
   notarized individually, so macOS Gatekeeper doesn't quarantine
   them when other apps try to load them.

Signing each plugin is ~1 second; signing 20 plugins is fast. Unsigned
or ad-hoc-signed plugins would either get quarantined in Gatekeeper or
silently fail to load — non-negotiable for a shippable pack.

### GPL compliance

Ship a `LICENSES/` directory in `Performance.app/Contents/Resources/`
with one file per bundled plugin:

```
LICENSES/
  mda.txt            # MIT
  Surge XT.txt       # GPL3 + source URL + commit hash
  Dexed.txt          # GPL3 + source URL + commit hash
  ...
  Airwindows.txt     # MIT
  README.md          # index + About → Plugins linkage
```

An **About → Plugins** screen (or a button in Settings → Plugins) shows
the list with author, version, license, source URL. Meets both MIT's
attribution requirement and GPL3 §6's source-availability requirement
(the link is sufficient — we don't have to ship source tarballs).

### JUCE license note

Many of our bundled plugins use JUCE under its **GPL option**, not its
commercial license. Our app, by contrast, will eventually need a JUCE
commercial license (planned for when we sell) because we're closed
source. These are **orthogonal**:

- Bundled plugins are separate binaries; the plugin author's choice of
  JUCE-GPL is complete and we inherit it by redistributing their
  compiled form.
- Our app's JUCE license only governs our own code, not the plugins we
  ship alongside.
- We do **not** need a commercial JUCE license just to redistribute
  GPL'd, JUCE-using plugin binaries.

What we **can't** do: ship a BSD-licensed plugin binary that was built
against commercial JUCE (e.g. any Socalabs plugin). The author held the
commercial license; we don't. Source-level BSD doesn't transfer binary
rights when the dependency is closed. This is the single biggest
license trap in the landscape — noted below in the ruled-out list.

## The bundle

20 plugins, ~220 MB uncompressed, ~130–160 MB compressed. Biased toward
"friend with a guitar, voice, or keyboard gets something surprising in
the first 10 minutes."

### Instruments (7)

| Plugin | License | Source | Rationale |
|---|---|---|---|
| **mda ePiano** | MIT/GPL2+ | [mda-vst](https://sourceforge.net/projects/mda-vst/) / [hollance/mda-plugins-juce](https://github.com/hollance/mda-plugins-juce) | Instant EP sound, tiny, always-works fallback |
| **mda JX10** | MIT/GPL2+ | same | Classic analog-poly |
| **mda DX10** | MIT/GPL2+ | same | FM pad/bass, lighter than Dexed |
| **mda Piano** | MIT/GPL2+ | same | Not great, but "there's an acoustic piano out of the box" |
| **Dexed** | GPL3 | [asb2m10/dexed](https://github.com/asb2m10/dexed) | Full DX7 patch compatibility; huge preset ecosystem |
| **Surge XT** | GPL3 | [surge-synthesizer/releases-xt](https://github.com/surge-synthesizer/releases-xt/releases) | Flagship synth; ships factory patches + wavetables |
| **Odin 2** | GPL3 | [TheWaveWarden/odin2](https://github.com/TheWaveWarden/odin2) | Hybrid synth with different character from Surge |

### Effects (13)

| Plugin | License | Source | Rationale |
|---|---|---|---|
| **Surge XT Effects** | GPL3 | (bundled with Surge XT) | One rack covers most FX categories: reverb, delay, distortion, chorus, flanger, rotary, EQ, vocoder |
| **Dragonfly Hall Reverb** | GPL3 | [michaelwillis/dragonfly-reverb](https://github.com/michaelwillis/dragonfly-reverb/releases) | Flagship reverb for guitars/vocals |
| **Dragonfly Room Reverb** | GPL3 | same | Tighter spaces |
| **Dragonfly Plate Reverb** | GPL3 | same | Vocal go-to |
| **Dragonfly Early Reflections** | GPL3 | same | Small-space/ambience |
| **mda DubDelay** | MIT/GPL2+ | mda | Classic dub-style delay |
| **mda Leslie** | MIT/GPL2+ | mda | Rotary speaker, makes any EP sound alive |
| **mda RingMod** | MIT/GPL2+ | mda | Creative FX |
| **mda Talkbox** | MIT/GPL2+ | mda | Obvious "surprise" for a guitar/voice tester |
| **mda Stereo** | MIT/GPL2+ | mda | Width utility |
| **mda Combo** | MIT/GPL2+ | mda | Small-amp simulator for DI guitar |
| **mda Bandisto** | MIT/GPL2+ | mda | Multiband distortion |
| **Airwindows Consolidated** | MIT | [airwindows.com](https://www.airwindows.com/) | 200+ Chris Johnson character FX in one `.component` — biggest variety-per-MB in the set |

### Ruled out (do not revisit without a license re-check)

| Reason | Plugins |
|---|---|
| Freeware, non-redistributable | Valhalla (all), Klanghelm, TAL, u-he free, Arturia/NI/Spectrasonics freebies, iZotope Neutrino, OrilRiver |
| Source is BSD but binaries were built against commercial JUCE | All Socalabs plugins |
| Closed-sourced by vendor, only old GPL fork usable | OB-Xd 2.11+ (discoDSP) |
| No macOS AU build (Linux-only or VST3-only) | LSP Plugins, Calf Studio Gear, Geonkick, ZynAddSubFX/Zyn-Fusion (no official AU), Charlatan |
| Proprietary commercial | Geist / Geist 2 (FXpansion) |

### Deferred (good candidates, wrong fit for 0.1.0)

| Plugin | Why deferred |
|---|---|
| **Vitalium** (MIT fork of Vital) | Ships without factory wavetables/presets — blank-canvas UX fails the "surprise in 10 min" test |
| **Helm** | Last active build 2019; arm64 notarization risk |
| **sfizz** (SFZ sample player) | Needs sample content to sing; Pianobook and other free SFZ packs aren't blanket-redistributable |
| **Cardinal** (DISTRHO VCV Rack) | 400 MB alone; modular learning curve too steep for a first-run surprise |
| **OB-Xf** (GPL fork of OB-Xd) | Requires us to build from source, sign, notarize — ~1 day we'd rather spend elsewhere for 0.1.0 |

## Build pipeline

Open questions flagged in the research:

1. **mda AU binaries from SourceForge are pre-notarization (2020).** They
   will likely need a rebuild from `hollance/mda-plugins-juce` (MIT) or
   the Elk Audio VST2 port. Budget ~half a day to wire up a CMake build
   + pipe through our signing script. Output: 12 `.component` bundles,
   signed with our cert.
2. **Every other plugin** on the list has an actively maintained official
   macOS AU release. We download the `.component`, verify checksum,
   codesign with our cert (re-signing a notarized third-party binary is
   allowed), commit to `Resources/bundled-plugins/` in-tree.
3. Version-pinning matters — the manifest records the exact release tag
   we built from. When upstream releases a new version, we bump
   explicitly.

## Build sequence

Each step a commit; app builds + tests pass at every step. Roll back
rather than patch-forward.

1. **License + attribution scaffolding.** Add `LICENSES/` directory to
   the app bundle's Resources. Ship the license texts for every plugin
   in the bundle (even before the binaries land). Attribution screen
   stub in Settings → Plugins.
2. **mda rebuild.** Build `hollance/mda-plugins-juce` as AU, sign with
   our Developer ID, commit the 12 `.component` bundles we're shipping
   into `Resources/bundled-plugins/`. Verify they load in Performance
   itself and in Logic (sanity check the signing).
3. **Download + sign third-party plugins.** Surge XT, Dexed, Odin 2,
   Dragonfly family, Airwindows Consolidated. Re-sign each `.component`
   with our cert. Commit to `Resources/bundled-plugins/`. Verify size
   budget still ≤ 160 MB compressed.
4. **First-launch install dialog.** Detection based on manifest; themed
   modal; progress bar; copy loop to
   `~/Library/Audio/Plug-Ins/Components/`; manifest write; AU rescan.
5. **Settings → Plugins tab.** Per-row [Remove]; "Remove all bundled
   plugins" action; source-link + license display per plugin (drives
   the About attribution screen).
6. **`Help → Install Plugin Pack…` menu item.** Re-opens the dialog
   after a "Maybe later," or if the user removed everything and wants
   to reinstall.
7. **Distribution proof with the full pack.** Install the DMG on a
   fresh user account on a second machine; confirm Gatekeeper accepts
   the notarized bundle including the plugins; confirm first-launch
   install flow completes cleanly; confirm the plugins load without
   quarantine.

## Out of scope

- **Plugin updates.** If Surge XT ships 1.4, we don't push it to
  existing installs automatically. Next release of our app bumps the
  bundled version and the install dialog offers the new one. Out of
  scope for 0.1.0.
- **Per-plugin "disable from our AU scan only" toggle.** If the user
  wants to hide a plugin from our catalog without uninstalling it from
  their system, that's the existing AU-scan-filter territory, separate
  feature.
- **Content packs** (wavetables for Vitalium, sample content for sfizz,
  Surge XT's extra patches). Once the plugin infrastructure is in
  place, these are additive.
- **Bundled plugin presets curated for Performance.** Opinionated
  starter presets that wake up a tester's session fast is a real future
  win, but out of this refactor's scope.

## Open questions

- **Install size ceiling.** 150 MB compressed for a plugin pack on top
  of a ~50 MB app feels on the high side. If testers push back, the
  easiest cut is Airwindows Consolidated (~60 MB) or Odin 2 (~30 MB).
  Worth measuring DMG size before distributing and reassessing.
- **mda rebuild or use SourceForge binaries as-is?** Depends on whether
  re-signing the 2020 SourceForge `.component` bundles with our
  Developer ID is enough for modern macOS, or whether they need a fresh
  build first. Test empirically during step 2.
- **Surge XT's own license dialog.** Surge displays its own "GPL
  notice" on first use. Verify that's not surprising inside our host
  and doesn't conflict with our attribution UX.
- **Install at first launch vs. install on first need.** Current plan
  is "on first launch." An alternative is "show the install option when
  the user creates their first instrument track." Leaner first-run, but
  more friction at creative moment. Lean toward first-launch for
  predictability; revisit if testers find it presumptuous.
