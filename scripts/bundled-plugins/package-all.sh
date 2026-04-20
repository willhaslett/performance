#!/usr/bin/env bash
# Zip the stapled .component bundles in .cache/staging/components/ into
# per-creator archives for S3 upload, and emit a draft manifest with
# each archive's size and SHA-256.
#
# Grouping is by upstream creator, not per-plugin, because the 11 mda
# bundles share a single upstream commit and update atomically, and the
# Surge XT archive naturally contains both the instrument and its FX
# rack. 4 archives keeps the install UI simple.
#
# Publish finalizes this: publish.sh uploads each zip to the
# performance-plugins S3 bucket and rewrites `archiveUrl` from null
# to the versioned S3 URL.
#
# Usage:  scripts/bundled-plugins/package-all.sh
# Output:
#   .cache/staging/archives/mda-suite-<ver>-macos.zip
#   .cache/staging/archives/dexed-<ver>-macos.zip
#   .cache/staging/archives/surge-xt-<ver>-macos.zip
#   .cache/staging/archives/airwindows-consolidated-<ver>-macos.zip
#   .cache/staging/archives/manifest-draft.json

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGING="$REPO_ROOT/.cache/staging/components"
ARCHIVES="$REPO_ROOT/.cache/staging/archives"
MANIFEST="$ARCHIVES/manifest-draft.json"

if [ ! -d "$STAGING" ]; then
    echo "!! $STAGING not found — stage bundles before packaging" >&2
    exit 1
fi

mkdir -p "$ARCHIVES"
rm -f "$ARCHIVES"/*.zip "$MANIFEST"

MANIFEST_ENTRIES=()

# package <slug> <version> <component> [<component> ...]
#
# Emits an archive named <slug>-<version>-macos.zip containing the
# listed stapled components, appends one manifest entry to
# MANIFEST_ENTRIES (JSON object string), and verifies the staple on
# each component before sealing — a missing staple would mean the
# archive can't pass first-launch Gatekeeper on a user's machine.
package() {
    local slug="$1" version="$2"; shift 2
    local components=("$@")
    local archive_name="${slug}-${version}-macos.zip"
    local archive_path="$ARCHIVES/$archive_name"

    echo "==> $archive_name (${#components[@]} bundle(s))"

    local c
    for c in "${components[@]}"; do
        local src="$STAGING/$c"
        if [ ! -d "$src" ]; then
            echo "!! missing component: $src" >&2
            exit 1
        fi
        if ! /usr/bin/xcrun stapler validate "$src" >/dev/null 2>&1; then
            echo "!! not stapled: $src (re-run notarize-all.sh)" >&2
            exit 1
        fi
    done

    # `ditto` only accepts one source, so for multi-bundle archives we
    # use `zip -r -y` instead. The notarization staple is stored as a
    # regular file inside each .component bundle's Contents/, so a
    # standard recursive zip preserves it. `-y` preserves symlinks.
    (cd "$STAGING" && /usr/bin/zip -r -q -y "$archive_path" \
        "${components[@]}")

    local size sha
    size=$(/usr/bin/stat -f%z "$archive_path")
    sha=$(/usr/bin/shasum -a 256 "$archive_path" | awk '{print $1}')
    echo "    $(/usr/bin/du -h "$archive_path" | awk '{print $1}')  sha256=${sha:0:16}…"

    local components_json=""
    for c in "${components[@]}"; do
        [ -n "$components_json" ] && components_json+=", "
        components_json+="\"$c\""
    done

    MANIFEST_ENTRIES+=("$(cat <<EOF
    {
      "slug": "$slug",
      "version": "$version",
      "archiveName": "$archive_name",
      "archiveUrl": null,
      "archiveSize": $size,
      "archiveSha256": "$sha",
      "components": [$components_json]
    }
EOF
)")
}

# ----- groups (bump versions alongside each upstream refresh) -----

# hollance/mda-plugins-juce commit 5527024
package "mda-suite" "2026-04-20" \
    "mda ePiano.component" \
    "mda JX10.component" \
    "mda DX10.component" \
    "mda Piano.component" \
    "mda Delay.component" \
    "mda Overdrive.component" \
    "mda Dynamics.component" \
    "mda Ambience.component" \
    "mda RingMod.component" \
    "mda Stereo.component" \
    "mda Bandisto.component"

package "dexed" "1.0.1" \
    "Dexed.component"

package "surge-xt" "1.3.4" \
    "Surge XT.component" \
    "Surge XT Effects.component"

package "airwindows-consolidated" "2026-04-19-7f5a66c" \
    "Airwindows Consolidated.component"

# ----- write manifest -----

{
    echo "{"
    echo "  \"version\": 1,"
    echo "  \"generatedAt\": \"$(/bin/date -u +%FT%TZ)\","
    echo "  \"archives\": ["
    local_first=1
    for entry in "${MANIFEST_ENTRIES[@]}"; do
        [ "$local_first" -eq 0 ] && echo ","
        local_first=0
        printf '%s' "$entry"
    done
    echo ""
    echo "  ]"
    echo "}"
} >"$MANIFEST"

echo ""
echo "==> Done. ${#MANIFEST_ENTRIES[@]} archives + manifest-draft.json in:"
echo "    $ARCHIVES"
echo ""
echo "Next: scripts/bundled-plugins/publish.sh to upload + finalize manifest."
