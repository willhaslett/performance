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
| `build-mda.sh` | Clones `hollance/mda-plugins-juce` (MIT), builds AU format universal, places 11 mda plugins in staging. |
| `download-surge-xt.sh` | Downloads Surge XT pluginsonly archive; extracts Surge XT + Surge XT Effects `.component` bundles. |
| `download-dexed.sh` | Downloads Dexed macOS zip; extracts Dexed `.component`. |
| `download-airwindows.sh` | Downloads Airwindows Consolidated DMG from baconpaul/airwin2rack; mounts, expands the outer pkg, extracts the `_AU.pkg` sub-package's payload to get `Airwindows Consolidated.component`. |
| `sign-all.sh` | Codesigns every `.component` in staging with our Developer ID. |
| `notarize-all.sh` | *(planned)* Submits each signed `.component` to Apple notarytool, waits, staples the approval ticket in. |
| `package-all.sh` | *(planned)* Zips each plugin's `.component` bundle(s) into versioned archives and records SHA-256 checksums. |
| `publish.sh` | *(planned)* Uploads the staging archives to S3 `performance-plugins` and regenerates `manifest.json` with URLs + checksums + versions. |
| `fetch-all.sh` | *(planned)* One-shot driver: runs every acquisition + sign + notarize + package + publish. |

## Output layout

```
.cache/
  downloads/         # raw upstream files, cached across re-runs
  staging/
    components/      # all .component bundles, ready for signing
    archives/        # per-plugin versioned .zip files for S3 upload
```

Everything under `.cache/` is gitignored. The `.component` binaries
never land in-tree; they go from staging → S3 via the publish
pipeline. The in-tree `runtime/bundled-plugins/` tree holds only the
plugin manifest + license notices that get baked into the app.
