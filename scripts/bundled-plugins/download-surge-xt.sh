#!/usr/bin/env bash
# Download Surge XT (GPL-3.0) — full release, including factory
# patches + wavetables + skins — and stage it for packaging.
#
# Layout:
#   DMG → outer pkg → nine sub-pkgs (one per target x format). We pull
#   three of them:
#     Surge_XT_AU.pkg/Payload         → Surge XT.component
#     Surge_XT_FXAU.pkg/Payload       → Surge XT Effects.component
#     Surge_XT_Resources.pkg/Payload  → factory support tree (patches,
#                                        wavetables, skins, etc.)
#
# The support tree is everything that normally lands in
# ~/Library/Application Support/Surge XT/ when a user runs the
# upstream installer. Without it Surge XT has no factory patches or
# wavetables — functional but empty.
#
# Upstream bumps:
#   - update VERSION; check https://github.com/surge-synthesizer/releases-xt/releases
#
# Usage:   scripts/bundled-plugins/download-surge-xt.sh
# Result:  Surge XT.component, Surge XT Effects.component in
#          .cache/staging/components/
#          Surge XT/ support tree in .cache/staging/support/surge-xt/

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-1.3.4}"
ARCHIVE="surge-xt-macos-${VERSION}.dmg"
URL="https://github.com/surge-synthesizer/releases-xt/releases/download/${VERSION}/${ARCHIVE}"

DOWNLOADS="$REPO_ROOT/.cache/downloads"
STAGING="$REPO_ROOT/.cache/staging/components"
SUPPORT="$REPO_ROOT/.cache/staging/support/surge-xt"
SCRATCH="$REPO_ROOT/.cache/surge-xt-scratch"
MOUNT_POINT="$SCRATCH/mount"
PKG_EXPAND="$SCRATCH/pkg-expand"

mkdir -p "$DOWNLOADS" "$STAGING"

echo "==> Surge XT $VERSION (full release with factory content)"

if [ ! -f "$DOWNLOADS/$ARCHIVE" ]; then
    echo "    Downloading $ARCHIVE (~400 MB)"
    curl -fL --progress-bar -o "$DOWNLOADS/$ARCHIVE.partial" "$URL"
    mv "$DOWNLOADS/$ARCHIVE.partial" "$DOWNLOADS/$ARCHIVE"
else
    echo "    Using cached $ARCHIVE"
fi

rm -rf "$SCRATCH" "$SUPPORT"
mkdir -p "$MOUNT_POINT" "$SUPPORT"

echo "    Mounting DMG"
hdiutil attach -nobrowse -readonly -noautoopen \
    -mountpoint "$MOUNT_POINT" "$DOWNLOADS/$ARCHIVE" >/dev/null
trap 'hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true' EXIT

pkg="$(find "$MOUNT_POINT" -maxdepth 2 -name "*.pkg" -print -quit)"
if [ -z "$pkg" ]; then
    echo "!! No outer .pkg in the mounted DMG" >&2
    exit 1
fi

echo "    Expanding outer pkg"
pkgutil --expand "$pkg" "$PKG_EXPAND"

# Helper: extract one sub-pkg's Payload (a tar archive) into $1, then
# pull the top-level directory matching $2 out to $3.
extract_from_subpkg() {
    local subpkg_name="$1"
    local expected_dir="$2"
    local dest_parent="$3"
    local label="$4"

    local subpkg="$PKG_EXPAND/$subpkg_name"
    if [ ! -f "$subpkg/Payload" ]; then
        echo "!! Missing sub-pkg payload: $subpkg_name" >&2
        exit 1
    fi
    local tmp="$SCRATCH/$subpkg_name.extracted"
    rm -rf "$tmp"; mkdir -p "$tmp"
    (cd "$tmp" && tar xf "$subpkg/Payload")

    local src
    src="$(find "$tmp" -maxdepth 3 -type d -name "$expected_dir" -print -quit)"
    if [ -z "$src" ]; then
        echo "!! Couldn't find $expected_dir in $subpkg_name payload" >&2
        find "$tmp" -maxdepth 3 -type d | sed 's/^/     /' >&2
        exit 1
    fi

    local dest="$dest_parent/$expected_dir"
    rm -rf "$dest"
    cp -R "$src" "$dest"
    echo "    staged $label → $dest"
}

extract_from_subpkg "Surge_XT_AU.pkg"   "Surge XT.component"         "$STAGING" "AU"
extract_from_subpkg "Surge_XT_FXAU.pkg" "Surge XT Effects.component" "$STAGING" "FX AU"

# Resources pkg has a flat payload (patches_factory/, wavetables/,
# etc.) that normally installs at ~/Library/Application Support/Surge XT/.
# Wrap it in a Surge XT/ directory so our packager can name the
# support tree explicitly in the manifest.
RES_TMP="$SCRATCH/resources-extract"
rm -rf "$RES_TMP"; mkdir -p "$RES_TMP"
(cd "$RES_TMP" && tar xf "$PKG_EXPAND/Surge_XT_Resources.pkg/Payload")

# Verify we see the expected top-level dirs before wrapping.
if [ ! -d "$RES_TMP/patches_factory" ] || [ ! -d "$RES_TMP/wavetables" ]; then
    echo "!! Resources payload missing patches_factory or wavetables" >&2
    ls "$RES_TMP" | sed 's/^/     /' >&2
    exit 1
fi

mkdir -p "$SUPPORT/Surge XT"
# Move every top-level dir/file in the resources extract under Surge XT/.
(cd "$RES_TMP" && find . -maxdepth 1 -mindepth 1 -exec mv {} "$SUPPORT/Surge XT/" \;)

res_size=$(/usr/bin/du -sh "$SUPPORT/Surge XT" | awk '{print $1}')
echo "    staged resources → $SUPPORT/Surge XT ($res_size)"

echo "==> Done."
