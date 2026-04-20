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
SUPPORT_ROOT="$REPO_ROOT/.cache/staging/support"
ARCHIVES="$REPO_ROOT/.cache/staging/archives"
MANIFEST="$ARCHIVES/manifest-draft.json"

if [ ! -d "$STAGING" ]; then
    echo "!! $STAGING not found — stage bundles before packaging" >&2
    exit 1
fi

mkdir -p "$ARCHIVES"
rm -f "$ARCHIVES"/*.zip "$MANIFEST"

MANIFEST_ENTRIES=()

# package <slug> <version> <components...> [-- <supportPaths...>]
#
# Emits an archive named <slug>-<version>-macos.zip containing the
# listed stapled components at the top level. If `--` appears, the
# remaining args name support-tree directories that live at
# .cache/staging/support/<slug>/<name>/ — they get dropped into the
# archive alongside the components. The app installer extracts them
# to ~/Library/Application Support/ (one level up from what's packaged).
#
# Appends one manifest entry to MANIFEST_ENTRIES (JSON object string).
# Verifies the staple on each component before sealing — a missing
# staple would mean the archive can't pass first-launch Gatekeeper.
package() {
    local slug="$1" version="$2"; shift 2
    local components=()
    local supports=()
    local parsing_support=0
    local arg
    for arg in "$@"; do
        if [ "$arg" = "--" ]; then parsing_support=1; continue; fi
        if [ "$parsing_support" -eq 1 ]; then
            supports+=("$arg")
        else
            components+=("$arg")
        fi
    done

    local archive_name="${slug}-${version}-macos.zip"
    local archive_path="$ARCHIVES/$archive_name"

    echo "==> $archive_name (${#components[@]} bundle(s), ${#supports[@]} support dir(s))"

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

    if [ "${#supports[@]}" -gt 0 ]; then
        local s
        for s in "${supports[@]}"; do
            local src="$SUPPORT_ROOT/$slug/$s"
            if [ ! -d "$src" ]; then
                echo "!! missing support dir: $src" >&2
                exit 1
            fi
        done
    fi

    # Stage components + support into one scratch dir so we can zip
    # everything at the top level with a single `zip -r` call.
    local stage="$ARCHIVES/.build-$slug"
    rm -rf "$stage"; mkdir -p "$stage"
    for c in "${components[@]}"; do
        cp -R "$STAGING/$c" "$stage/"
    done
    if [ "${#supports[@]}" -gt 0 ]; then
        local s
        for s in "${supports[@]}"; do
            cp -R "$SUPPORT_ROOT/$slug/$s" "$stage/"
        done
    fi

    # `zip -r -y` preserves symlinks + the staple-as-file in each
    # bundle's Contents/. -X strips file metadata timestamps that
    # would churn the SHA-256 across re-runs.
    (cd "$stage" && /usr/bin/zip -r -q -y -X "$archive_path" .)
    rm -rf "$stage"

    local size sha
    size=$(/usr/bin/stat -f%z "$archive_path")
    sha=$(/usr/bin/shasum -a 256 "$archive_path" | awk '{print $1}')
    echo "    $(/usr/bin/du -h "$archive_path" | awk '{print $1}')  sha256=${sha:0:16}…"

    local components_json=""
    for c in "${components[@]}"; do
        [ -n "$components_json" ] && components_json+=", "
        components_json+="\"$c\""
    done
    local supports_json=""
    if [ "${#supports[@]}" -gt 0 ]; then
        for s in "${supports[@]}"; do
            [ -n "$supports_json" ] && supports_json+=", "
            supports_json+="\"$s\""
        done
    fi

    MANIFEST_ENTRIES+=("$(cat <<EOF
    {
      "slug": "$slug",
      "version": "$version",
      "archiveName": "$archive_name",
      "archiveUrl": null,
      "archiveSize": $size,
      "archiveSha256": "$sha",
      "components": [$components_json],
      "supportPaths": [$supports_json]
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
    "Surge XT Effects.component" \
    -- \
    "Surge XT"

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
