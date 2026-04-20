# Bundled-plugin acquisition scripts

Scripts that produce the signed `.component` bundles we ship in
`runtime/bundled-plugins/components/`. Each plugin source gets its own
acquisition script (`build-*` for things we build from source,
`download-*` for things we pull pre-built), and a single `sign-all.sh`
step re-signs everything with our Developer ID so it notarizes cleanly
as part of the app bundle.

See `docs/BUNDLED_PLUGINS.md` for the full plugin list, licenses, and
bundle design.

## Scripts

| Script | Role |
|---|---|
| `build-mda.sh` | Clones `hollance/mda-plugins-juce` (MIT), builds AU format universal, signs, places 11 mda plugins. |
| `download-surge-xt.sh` | *(planned)* Downloads Surge XT + Surge XT Effects from the upstream release, places the 2 `.component` bundles. |
| `download-dexed.sh` | *(planned)* Downloads Dexed. |
| `download-odin2.sh` | *(planned)* Downloads Odin 2. |
| `download-dragonfly.sh` | *(planned)* Downloads the 4 Dragonfly reverb plugins. |
| `download-airwindows.sh` | *(planned)* Downloads Airwindows Consolidated. |
| `sign-all.sh` | Re-signs every `.component` in the output directory with our Developer ID. |
| `fetch-all.sh` | *(planned)* One-shot driver: runs every acquisition script + `sign-all.sh`. |

## Output

All scripts place their `.component` bundles into
`runtime/bundled-plugins/components/`. That directory is the single
source of truth for what ships in the install pack — `sign-all.sh`
iterates it, and later build steps copy it into the app bundle.

## Scratch dir

Build scripts cache their working copies under `.cache/` at repo root
(gitignored). Re-runs reuse the cache; delete the specific
subdirectory to force a fresh clone.
