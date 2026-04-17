# Theme reference

Full design-token inventory for the Performance app. The authoritative
source is `src/gui/Theme.h`; this doc mirrors it in a human-readable form
and captures the semantic intent + design principles behind each token.

Rules and high-level usage guidance live in CLAUDE.md under
`### Theme` — read that first.

## Color tokens

### Surfaces (darkest → lightest)

| Token | Value | Use |
|---|---|---|
| `bgApp` / `bgPanel` | `0xff161616` | App background, sidebar, pane headers |
| `bgSurface` | `0xff1e1e1e` | Track lanes, mixer strips, track headers |
| `bgSurfaceRaised` | `0xff333333` | Regions in the timeline — one step above the lane |
| `bgRecessed` | `0xff121212` | Deepest inset (candidate for removal; see Active Work) |

### Interactive controls (pills, plugin slots, LCD, pickers, text fields)

| Token | Value | Use |
|---|---|---|
| `bgControl` | `0xff2d2d2d` | Resting state of any interactive control |
| `bgControlHover` | `0xff333333` | Hover state for any interactive control |
| `bgSelection` | `0xff262626` | Selected track row highlight in content panes — sits between `bgSurface` and `bgControl` so pills on selected rows still contrast |
| `bgListActive` | `0xff333333` | Active/selected entry in a list view (sidebar, palettes) — brighter than `bgSelection` because list contexts have no embedded controls to contrast with |
| `bgOverlay` | `0xff2a2a2a` | Modal / popup backdrop |
| `bgDisabled` | `0xff252525` | Disabled strip / header fill |

### Passive inset surfaces (not clickable)

| Token | Value | Use |
|---|---|---|
| `bgSlot` | `0xff2a2a2a` | Meter grooves, fader/slider troughs |
| `overlayDim` | `0x40000000` | Semi-transparent dim over content (drag source, etc.) |

### Text

| Token | Value | Use |
|---|---|---|
| `textPrimary` | `0xffd8d8d8` | Main body text, track names |
| `textSecondary` | `0xffaaaaaa` | Labels, descriptions |
| `textDim` | `0xff666666` | Disabled, placeholder, type indicators |
| `textKeyHint` | `0xff909090` | Keybinding hints — between `textDim` and `textSecondary`, intentionally readable but not loud |
| `textOnColor` | `0xffffffff` | Text on colored backgrounds only (active pills, transport buttons) |
| `controlHandle` | `0xffffffff` | Fader handles, grabbable controls |

### Borders

| Token | Value | Use |
|---|---|---|
| `border` | `0xff444444` | Standard divider lines |
| `borderSubtle` | `0xff333333` | Lighter separators within panels |

### Accent

| Token | Value | Use |
|---|---|---|
| `accent` | `0xff2a6aaa` | Selection indicator, focus, drag-target line |
| `accentDim` | `0xff1a4a6a` | Subtle accent |

### Transport

| Token | Value | Use |
|---|---|---|
| `transportPlay` | `0xff2a6a2a` | Play button active bg |
| `transportRec` | `0xff6a2a2a` | Record mode active bg |
| `transportRecDot` | `0xffcc4444` | Record dot when idle |
| `transportCycle` | `0xff8a8a40` | Cycle active + cycle guide lines |
| `transportCycleOff` | `0xff505050` | Cycle button inactive |
| `playhead` | `0xccffffff` | Playhead vertical line (semi-transparent white) |

### Meter / activity

| Token | Value | Use |
|---|---|---|
| `meterGreen` | `0xff44cc44` | Safe level |
| `meterAmber` | `0xffccaa44` | Warning zone |
| `meterRed` | `0xffcc4444` | Clipping zone |
| `sendPeak` | `0xff1a6e1a` | Send knob peak-activity glow (darker green) |
| `activityOn` / `activityOff` | `0xff44cc44` / `0xff1a3a1a` | MIDI activity indicator |
| `statusError` | `0xff994444` | Load failures |

### Track pills (M/S/R/I active colors)

| Token | Value | Use |
|---|---|---|
| `pillMute` | `0xff8a7a3a` | M active — muted gold |
| `pillSolo` | `0xff3a6a8a` | S active — muted steel |
| `pillArm` | `0xff8a4040` | R active — muted red |
| `pillInput` | `0xff8a4040` | I active — muted red |
| `pillTextOff` | `0xff888888` | Inactive pill text color |

Pill resting backgrounds use `bgControl`, hover uses `bgControlHover`. Pill text when active uses `textOnColor`. Pill hover only applies to resting (off) pills — active colored pills do not change on hover.

### Channel type accents (2px top stripe in mixer, 3px left-edge stripe in ProducePane headers)

| Token | Value | Use |
|---|---|---|
| `typeInstrument` | `0xffaa8838` | Amber — virtual instrument tracks |
| `typeAudio` | `0xff5080a0` | Teal — audio input tracks |
| `typeBus` | `0xffa86078` | Rose — busses (deliberately warm to remain distinguishable from teal under red-green CVD) |
| `typeOutput` | `0xff161616` | Matches `bgApp` — Output strip paints no stripe; absence of color is the signal |

Slot type tints, chat bubbles, and track/device color palettes are additional category-specific tokens referenced only from their respective GUI files. See `Theme.h` for the complete list.

## Typography

| Token | Value | Use |
|---|---|---|
| `fontSizeTitle` | `16.0f` | Pane headers, section titles |
| `fontSizeKeyHint` | `15.0f` | Keybinding hints (monospaced) |
| `fontSizeLg` | `14.0f` | Track names, primary content (alias: `fontSize`) |
| `fontSizeMd` | `13.0f` | Slot labels, secondary content |
| `fontSizeSm` | `12.0f` | Badges, hints, type indicators |
| `fontSizeXs` | `9.0f` | Very small labels (metronome, region counters) |
| `fontSizePill` | `13.0f` | Pill button labels |
| `fontSizeMeter` | `10.0f` | dB tick labels |
| `fontSizeLcdLg` / `LcdMd` / `LcdLabel` | `29 / 23 / 8` | Transport LCD digits + tiny labels |

Use `Theme::font(Theme::fontSizeXx)` for sans and `Theme::fontMono(Theme::fontSizeXx)` for monospace (LCD).

## Spacing

| Token | Value |
|---|---|
| `spacingXs` | `2` |
| `spacingS` | `4` |
| `spacingM` | `8` |
| `spacingL` | `12` |
| `spacingXl` | `16` |

## Component dimensions

- `headerHeight = 28` — shared height of mixer strip headers and ProducePane track name row. Single source of truth for vertical name-block rhythm.
- `slotHeight = 24`, `slotGap = 4`, `slotPadding = 4`
- `trackPadding = 12`, `trackStripWidth = 160`, `iconSize = 14`
- `pillSize = 20`, `pillRadius = 4.0f`, `pillGap = 5`, `pillGroupGap = 8`, `pillNameGap = 8`
- `cornerRadius = 6.0f`, `cornerRadiusSm = 4.0f`

## Design principles

- **Minimal color, maximum contrast hierarchy.** The surface stack (`bgApp → bgSurface → bgControl/bgSlot/bgSelection → bgSurfaceRaised/bgControlHover`) carries most of the visual hierarchy. Color is reserved for meaning (transport, meter, status, pill states).
- **Subtle type accents.** Channels carry a small type-colored marker — top stripe in the mixer, left-edge stripe in ProducePane — so `typeInstrument` (amber), `typeAudio` (teal), `typeBus` (rose), and Output (no stripe) are scannable at a glance. Color is minimal-saturation and reserved for type identity; track/channel bodies stay neutral.
- **Two-row track headers in ProducePane:** name on top (`headerHeight = 28`), M/S/R/I pills below with a 10px gap, `trackRowHeight = 72` default.
- **Hover is an interaction signal, not decoration.** Only interactive controls get hover states, and only when resting (off/unselected). Active colored pills don't change on hover.
- **Semantic tokens over value reuse.** Duplicating a hex value across multiple semantic tokens is a feature — future themes can split them.

## Adding a new theme

In principle: add a new JSON file to `runtime/themes/` mirroring the structure of `minimal_dark.json`, bake it into the binary via `juce_add_binary_data`, and switch via `Theme::loadThemeById(id)`. In practice, there's no runtime theme switching UI yet — and the non-pane files (several modals, dialogs, overlays) still contain hardcoded colors. Full themeability requires finishing the pane sweep tracked under Active Work in CLAUDE.md.
