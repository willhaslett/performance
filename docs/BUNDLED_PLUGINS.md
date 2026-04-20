# Bundled Plugins — First-Launch Download Pack

The app ships as a "creative playground" but assumes nothing about the
tester's plugin library. Without a ready-to-play synth + reverb + delay
out of the box, a friend with a guitar or keyboard has a multitrack
recorder, not a playground. This document captures the plugin bundle we
offer, how it gets onto the tester's machine on first launch, how they
remove it, and what license/legal obligations we accept by shipping it.

Triggered by product reassessment during the 0.1.0 run-up: the
architecture is ready, the app is not — "not enough there to reward an
hour" was the honest read. Plugin bundle is the missing piece.

> Read this doc before adding or removing plugins from the bundle,
> touching the install/uninstall flow, or changing the publish
> pipeline. The license landscape is narrow and easy to violate by
> accident; the ruled-out list below is load-bearing.

## Why this work

Three observations framing the need:

1. **macOS ships no meaningful instruments.** DLSMusicDevice (our
   current default) is a polite GM soundfont synth and that's it. Any
   creative exploration requires the user already owns plugins.
2. **"Install these plugins first" is not a setup step musicians will
   tolerate.** Distributing an app that lets them open it and then
   fails to produce any interesting sound until they download 10 other
   things is a non-starter.
3. **Permissively licensed, redistributable audio plugins are plentiful
   but the legal landscape is a minefield.** Most "free" plugins are
   freeware without redistribution rights (Valhalla, Klanghelm, TAL,
   u-he free, Arturia/NI Intro products). A previous attempt to build
   the mda suite from source went badly; the solution is to ship
   pre-built binaries, not to rebuild the world.

The fix: a first-launch "install the plugin pack?" prompt that
**downloads** a pre-built, notarized plugin set from our S3 CDN,
extracts each `.component` into the user's
`~/Library/Audio/Plug-Ins/Components/` directory, and tracks what we
placed so we can cleanly uninstall. The plugin bundle is not part of
the app DMG — that stays ~50 MB, fast to download and distribute.

## Design — the model after

### Distribution shape

Two separate surfaces:

- **Our publish side** (infrequent — once per plugin, once per version
  bump): acquisition scripts pull or build each plugin, sign each
  `.component` with our Developer ID, notarize each with Apple, then
  upload to an S3 bucket along with a `manifest.json`.
- **User download side** (per install): on first launch, the app
  fetches `manifest.json`, shows a themed dialog, and on consent
  downloads the archives in parallel, verifies SHA-256 checksums,
  extracts `.component` bundles, copies them to the user's Components
  folder, writes a local install manifest.

Plugins never ride inside the app bundle. The DMG stays small; plugin
updates don't require an app release.

### S3 bucket shape

Single bucket `performance-plugins` in its own `PerformancePlugins`
CDK stack. **Private** — `BlockPublicAccess.BLOCK_ALL`. Access is
brokered by a `PluginsProxy` Lambda (sibling to the telemetry + chat
proxies in `PerformanceTelemetry`) that validates the app's shared
bearer token and returns `manifest.json` with 1-hour presigned S3
GET URLs filled in for each archive. Keys:

```
manifest.json                                         # returned by Lambda
plugins/surge-xt-1.3.4-macos.zip                      # versioned + immutable
plugins/dexed-1.0.1-macos.zip
plugins/airwindows-consolidated-2026-04-19-7f5a66c-macos.zip
plugins/mda-suite-<date>-macos.zip
```

Versioned URLs with `Cache-Control: public, max-age=31536000, immutable`
on every object under `plugins/`. Once a versioned file is written, it
never changes. The manifest is the only indirection; when we bump
Surge to 1.3.5 we upload a new versioned archive and update the
manifest.

### Manifest shape (server-side, in S3)

```json
{
  "version": 1,
  "updatedAt": "2026-04-21T00:00:00Z",
  "plugins": [
    {
      "name": "Surge XT",
      "category": "instrument",
      "version": "1.3.4",
      "description": "Flagship open-source synth...",
      "license": "GPL-3.0",
      "licenseFile": "NOTICE-Surge-XT.txt",
      "sourceUrl": "https://github.com/surge-synthesizer/surge",
      "archiveUrl": "plugins/surge-xt-1.3.4-macos.zip",
      "archiveSize": 196000000,
      "archiveSha256": "abc123...",
      "components": ["Surge XT.component", "Surge XT Effects.component"]
    },
    ...
  ]
}
```

The archive contains one or more `.component` bundles and an
`index.json` manifest inside describing them (for integrity during
extraction). `components` lists the `.component` names the app expects
to extract; used by the installer to verify and by the uninstaller to
track what it placed.

### First-launch flow

1. **Detection.** On app launch, check for our local install marker at
   `~/Library/Application Support/com.performance.app/bundled-plugins.json`.
   If the marker exists and lists plugins we already placed, we're done
   — skip silently.
2. **Prompt.** If the marker is missing, fetch the remote manifest,
   show a themed modal:

   > **Install free plugin pack?**
   > Your friends don't have a plugin library. This app is more fun
   > with one. 19 plugins (6 instruments + 13 effects) from free,
   > open-source projects — ready to play. About 200 MB download.
   >
   > [Install] &nbsp;&nbsp; [Maybe later]

3. **Download + install.** On consent: download each archive in
   parallel, verify SHA-256, extract each `.component`, copy to
   `~/Library/Audio/Plug-Ins/Components/`, record in the local install
   manifest (per-plugin path + version). Progress UI per plugin (not a
   single opaque progress bar). Fail-loud per plugin — if Surge fails
   but Dexed succeeds, install Dexed and surface the Surge error with a
   retry option. Trigger an AU rescan so the new plugins appear in the
   catalog immediately.
4. **Skip.** "Maybe later" writes a `skipped-at` timestamp to the local
   marker. App doesn't re-prompt. `Help → Install Plugin Pack…` always
   re-opens the dialog.

### Uninstall flow

Lives in `Settings → Plugins`:

```
┌───────────────────────────────────────────────────────┐
│ Bundled Plugins                                       │
│                                                       │
│ Downloaded and installed on first launch. Remove any  │
│ you don't want.                                       │
│                                                       │
│ Surge XT                        synth      [Remove]   │
│ Surge XT Effects                effect     [Remove]   │
│ Dexed                           FM synth   [Remove]   │
│ mda ePiano                      EP         [Remove]   │
│ ... (etc.)                                            │
│                                                       │
│ [Remove all bundled plugins]                          │
└───────────────────────────────────────────────────────┘
```

Each row's [Remove] deletes the `.component` file we placed and updates
the local manifest. "Remove all" is one confirmation dialog, then same.
Never touches a plugin we didn't install — the local manifest is the
only list of removable items.

### Local install manifest (client-side, in user Application Support)

```json
{
  "installedAt": "2026-04-21T10:30:00Z",
  "appVersion": "0.1.0",
  "remoteManifestVersion": 1,
  "plugins": [
    {
      "name": "Surge XT",
      "version": "1.3.4",
      "installedPath": "~/Library/Audio/Plug-Ins/Components/Surge XT.component",
      "sourceUrl": "https://github.com/surge-synthesizer/surge",
      "license": "GPL-3.0"
    },
    ...
  ]
}
```

### Code signing & notarization

Every `.component` on S3 is both **codesigned with our Developer ID**
and **individually notarized** by Apple. The notarization ticket is
stapled into the bundle so the user's Mac doesn't need to phone home
at plugin-load time.

Per-plugin publish steps:

1. `codesign --force --timestamp --options runtime --sign "Developer ID Application: ..." plugin.component`
2. `ditto -c -k --keepParent plugin.component plugin.zip`
3. `xcrun notarytool submit plugin.zip --keychain-profile AC_PASSWORD --wait`
4. `xcrun stapler staple plugin.component`
5. Rezip the stapled bundle for upload.

Takes ~1-3 min per plugin. Runs **once per plugin per version** — never
per user, never per app release, never per S3 re-upload.

### GPL compliance

The app ships a `LICENSES/` directory in `Performance.app/Contents/Resources/`
with one `NOTICE-*.txt` file per source project — already baked in via
BinaryData from `runtime/bundled-plugins/LICENSES/`. The Settings →
Plugins tab surfaces author, version, license, source URL per plugin.

This satisfies both MIT attribution and GPL-3.0 §6 source-availability
(the upstream source URL is sufficient — we don't have to ship source
tarballs ourselves).

### JUCE license note

Many of our bundled plugins use JUCE under its **GPL option**, not its
commercial license. Our app, by contrast, will eventually need a JUCE
commercial license (planned for when we sell) because we're closed
source. These are **orthogonal**:

- Bundled plugins are separate binaries; the plugin author's choice of
  JUCE-GPL is complete and we inherit it by redistributing their
  compiled form.
- Our app's JUCE license only governs our own code.
- We do **not** need a commercial JUCE license just to redistribute
  GPL'd, JUCE-using plugin binaries.

What we **can't** do: ship a BSD-licensed plugin binary that was built
against commercial JUCE (e.g. any Socalabs plugin). The author held the
commercial license; we don't. Source-level BSD doesn't transfer binary
rights when the dependency is closed. This is the single biggest
license trap in the landscape — noted below in the ruled-out list.

## Infra architecture (CDK)

Two stacks:

```
PerformancePlugins stack:
  S3 bucket performance-plugins-<acct>   BlockPublicAccess.BLOCK_ALL,
                                         RETAIN, tagged Project=Performance
    └─ manifest.json                     short TTL
    └─ plugins/*.zip                     immutable, versioned

PerformanceTelemetry stack (existing) adds:
  PluginsProxy Lambda + function URL
    validates bearer token, reads manifest.json from the private bucket,
    issues 1-hour presigned S3 GET URLs for each archive, returns the
    enriched manifest
```

Private bucket prevents drive-by scraping of our S3 egress; the bearer
token in the app binary is casual rate-limiting rather than a hardened
access gate (the plugins themselves are freely available upstream).
Project=Performance tag routes costs into the $50/mo budget alarm
already defined on the telemetry stack.

**Upgrade paths known-but-not-built:**

- **OAC-fronted CloudFront distribution** when egress ≥ $20/mo or
  global latency shows up in telemetry. The URL scheme the Lambda
  hands out stays the same, CloudFront just sits in front.
- **Per-install download quota** in DynamoDB (same table as chat)
  only if a single install starts hammering us.
- **Multi-region replication** only if a tester population outside
  North America complains. Not before.

**Infra discipline we keep from day one** (cheap now, expensive to
retrofit):

1. Every resource tagged `app=performance`, `env=prod`. Enables cost
   allocation and future staging/prod split.
2. Versioned object keys (`surge-xt-1.3.4-macos.zip`), never "latest".
   Immutable URLs cache forever; the manifest is the only indirection.
3. Resource names follow `performance-<subsystem>-<role>` (e.g.
   `performance-plugins-bucket`). Documented in `infra/README.md`.
4. Per-subsystem CloudWatch billing alarm so spikes are attributable.

## The bundle

15 plugins, ~250 MB uncompressed archives, ~180 MB compressed zips on
S3. App DMG stays ~50 MB (unaffected). Biased toward "friend with a
guitar, voice, or keyboard gets something surprising in the first 10
minutes."

### Instruments (6)

| Plugin | License | Source | Rationale |
|---|---|---|---|
| **mda ePiano** | MIT/GPL2+ | [hollance/mda-plugins-juce](https://github.com/hollance/mda-plugins-juce) | Instant EP sound, tiny, always-works fallback |
| **mda JX10** | MIT/GPL2+ | same | Classic analog-poly |
| **mda DX10** | MIT/GPL2+ | same | FM pad/bass, lighter than Dexed |
| **mda Piano** | MIT/GPL2+ | same | Basic acoustic piano — there if you need one |
| **Dexed** | GPL-3.0 | [asb2m10/dexed](https://github.com/asb2m10/dexed) | Full DX7 patch compatibility; huge preset ecosystem |
| **Surge XT** | GPL-3.0 | [surge-synthesizer/releases-xt](https://github.com/surge-synthesizer/releases-xt/releases) | Flagship synth; factory patches + wavetables |

### Effects (9)

| Plugin | License | Source | Rationale |
|---|---|---|---|
| **Surge XT Effects** | GPL-3.0 | (bundled with Surge XT) | One rack covers most FX categories: reverb, delay, distortion, chorus, flanger, rotary, EQ, vocoder |
| **mda Delay** | MIT | mda | Simple stereo delay with feedback + tone |
| **mda Overdrive** | MIT | mda | Soft overdrive; warmth and grit |
| **mda Dynamics** | MIT | mda | Compressor + limiter + gate |
| **mda Ambience** | MIT | mda | Small-space reverb (rooms, chambers) |
| **mda RingMod** | MIT | mda | Creative FX, metallic character |
| **mda Stereo** | MIT | mda | Width utility |
| **mda Bandisto** | MIT | mda | Multiband distortion |
| **Airwindows Consolidated** | MIT | [baconpaul/airwin2rack](https://github.com/baconpaul/airwin2rack/releases/tag/DAWPlugin) | 200+ Chris Johnson character FX in one `.component` — biggest variety-per-MB in the set |

> **Note on mda picks.** `hollance/mda-plugins-juce` is a partial port of
> the original mda VST2 suite to modern JUCE. DubDelay, Leslie, Talkbox,
> and Combo were never ported there (README lists them under "Plug-ins
> that have not been converted yet"), so they're not in the bundle.
> Surge XT Effects covers the rotary and tape-delay slots those would
> have filled.

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
| **Dragonfly Reverb family** (Hall / Room / Plate / Early Reflections) | Upstream dropped Audio Unit format in 3.2.10 — current release ships CLAP / LV2 / VST2 / VST3 only. Blocked until we add VST3 hosting (see `Open questions / future work` below). |
| **Odin 2** | Only release is a 2020 `NightlyDevel` nightly, `.pkg`-only installer, stale for 5 years. Revisit if upstream releases a tagged universal build. |
| **Vitalium** (MIT fork of Vital) | Ships without factory wavetables/presets — blank-canvas UX fails the "surprise in 10 min" test |
| **Helm** | Last active build 2019; arm64 notarization risk |
| **sfizz** (SFZ sample player) | Needs sample content to sing; Pianobook and other free SFZ packs aren't blanket-redistributable |
| **Cardinal** (DISTRHO VCV Rack) | 400 MB alone; modular learning curve too steep for a first-run surprise |
| **OB-Xf** (GPL fork of OB-Xd) | Requires us to build from source, sign, notarize — ~1 day we'd rather spend elsewhere for 0.1.0 |

## Publish pipeline

Scripts under `scripts/bundled-plugins/`:

```
build-mda.sh           → clones hollance/mda-plugins-juce, builds AU universal,
                         copies the 11 we ship to staging/
download-surge-xt.sh   → fetches Surge XT macOS full DMG, expands the outer
                         pkg, extracts 2 .component bundles + the factory
                         resources tree (Surge XT/ → patches_factory,
                         wavetables, skins, …) to staging/ and
                         staging/support/surge-xt/
download-dexed.sh      → fetches Dexed macOS ZIP, extracts to staging/
download-airwindows.sh → fetches Airwindows Consolidated macOS DMG, mounts,
                         extracts to staging/

sign-all.sh            → codesigns every .component in staging/ with our
                         Developer ID
notarize-all.sh        → submits each .component to Apple notarytool, waits,
                         staples approval ticket into the bundle
package-all.sh         → zips each plugin's .component(s) into versioned
                         archives under staging/archives/, computes SHA-256
publish.sh             → uploads staging/archives/*.zip to S3 plugins/,
                         regenerates manifest.json with sizes + checksums +
                         version pins, uploads that too

fetch-all.sh           → runs build-mda + all download-* + sign-all +
                         notarize-all + package-all + publish (one-shot)
```

Each script is idempotent; scratch lives under `.cache/` (gitignored).
`publish.sh` is the only destructive one — it uploads. Gated behind a
`--confirm` flag.

## Build sequence

Each step a commit; app builds + tests pass at every step. Roll back
rather than patch-forward.

**Step 1 landed 2026-04-20:** License scaffolding + `Settings → Plugins`
stub. `runtime/bundled-plugins/manifest.json` + `LICENSES/NOTICE-*.txt`
baked into BinaryData; Plugins tab reads the manifest and shows all 19
plugins with name / category / license / description.

Remaining:

2. **Acquisition scripts.** Write `build-mda.sh` (already in-tree) +
   `download-*.sh` for the 4 non-mda sources. Each produces unsigned
   `.component` bundles in `.cache/staging/`. Verify by eye that the
   right bundles land.
3. **Signing + notarization pipeline.** `sign-all.sh` (already in-tree)
   + new `notarize-all.sh` + `package-all.sh`. Run once end-to-end;
   verify each archive contains stapled, notarized bundles that load
   cleanly in Logic.
4. **CDK addition + `publish.sh`.** Add the `performance-plugins` S3
   bucket to the existing stack (tagged, with billing alarm).
   `publish.sh` uploads + regenerates remote manifest. Deploy.
5. **App-side install flow.** Themed first-launch dialog; manifest
   fetch from S3; parallel download + checksum verify + extract +
   install; progress UI; local install manifest write; AU rescan.
   Fail-loud per-plugin with retry.
6. **Settings → Plugins tab: per-plugin Remove + Remove all.** Reads
   local install manifest; each Remove deletes the `.component` and
   updates the manifest; Remove all confirms once and wipes what we
   placed.
7. **`Help → Install Plugin Pack…` menu item.** Re-opens the dialog
   after a "Maybe later," or if the user removed everything and wants
   to reinstall.
8. **Distribution proof with the full flow.** Install the DMG on a
   fresh user account on a second machine; confirm Gatekeeper accepts
   the app; confirm first-launch download completes; confirm every
   plugin loads without quarantine warnings.

## Out of scope

- **Plugin auto-updates.** If Surge XT ships 1.4, existing installs
  stay on 1.3.4 until the user explicitly opts into an update
  (probably via Settings → Plugins → "Check for updates"). Not in 0.1.0.
- **Per-plugin "disable from our AU scan only" toggle.** If the user
  wants to hide a plugin from our catalog without uninstalling it from
  their system, that's the existing AU-scan-filter territory, separate
  feature.
- **Content packs** (wavetables for Vitalium, sample content for sfizz,
  Surge XT's extra patches). Once the plugin infrastructure is in
  place, these are additive via the same pipeline.
- **Opinionated starter presets curated for Performance.** Real future
  win (wakes up a tester's session fast), but out of this refactor's
  scope.
- **Tiered bundles.** "Essentials" vs "Full" pack. If 200 MB feels
  heavy in testing we'll add it; not speculating.

## Open questions

- **Offline install story.** If a tester has no internet on first
  launch, they can't get plugins. "Maybe later" puts it off; but there
  might be friends doing a first launch on stage before a show with no
  wifi. Offer a fallback "download from our website manually" path?
  Probably yes for 0.2.x; not 0.1.0.
- **S3 egress at scale.** At $0.09/GB, 1000 installs × 200 MB = $18
  one-time. Fine at our scale; add CloudFront when egress trends past
  $20/mo.
- **Apple's notarytool latency in bulk.** 19 plugins × ~2 min each =
  ~40 min for a full republish. Acceptable for rare updates, but
  worth monitoring if we iterate the bundle frequently during testing.
- **Surge XT's own license dialog.** Surge displays a "GPL notice" on
  first use. Verify that's not surprising inside our host and doesn't
  conflict with our attribution UX.
- **Install at first launch vs. install on first need.** Current plan
  is "on first launch." An alternative is "show the install option
  when the user creates their first instrument track." Leaner
  first-run, but more friction at creative moment. Lean toward
  first-launch for predictability.

## Future work — VST3 support (0.2.x)

The app currently hosts only Audio Unit plugins. Adding VST3 via
JUCE's `VST3PluginFormat` unblocks:

- **Dragonfly Reverb family** — upstream dropped AU in 3.2.10, ships
  VST3 + CLAP + LV2 + VST2. Getting 4 reverbs back is the immediate
  win.
- **Many more free VST3 plugins** — most new GPL3 / MIT synths and
  effects ship VST3 as their primary format. AU-only is increasingly
  macOS-ghetto.
- **Future-proofing.** The industry is converging on VST3 + CLAP.

Rough scope: format registration is trivial (JUCE drop-in); the real
work is generalizing plugin-scan paths, plugin-identifier storage
(`format:uuid`), and any UI that assumes "AU" in copy. Probably 1–2
focused days. Deferred behind the 0.1.0 ship gate.

**Not** adding:

- **VST2** — Steinberg SDK no longer distributed; every plugin that
  ships VST2 also ships VST3.
- **CLAP** — newer open standard; worth tracking. JUCE 8 is still
  rolling out hosting support. Defer further.
