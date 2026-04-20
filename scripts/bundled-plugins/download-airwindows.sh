#!/usr/bin/env bash
# Download Airwindows Consolidated (MIT) — ~200 Chris Johnson character
# effects in a single plugin. macOS universal AU from baconpaul's
# airwin2rack release.
#
# Layout note: the DMG contains one outer .pkg which in turn contains
# four sub-packages (APP, AU, CLAP, VST3). We extract only the AU
# sub-package's Payload (a tar archive) to get the .component bundle.
#
# Upstream bumps:
#   - the airwin2rack project uses a rolling "DAWPlugin" tag and rebuilds
#     the DMG periodically. Update VERSION below to the dated filename of
#     the release asset; check
#     https://github.com/baconpaul/airwin2rack/releases/tag/DAWPlugin
#
# Usage:   scripts/bundled-plugins/download-airwindows.sh
# Result:  Airwindows Consolidated.component in .cache/staging/components/

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-2026-04-19-7f5a66c}"
ARCHIVE="airwindows-consolidated-macOS-${VERSION}.dmg"
URL="https://github.com/baconpaul/airwin2rack/releases/download/DAWPlugin/${ARCHIVE}"

DOWNLOADS="$REPO_ROOT/.cache/downloads"
STAGING="$REPO_ROOT/.cache/staging/components"
SCRATCH="$REPO_ROOT/.cache/airwindows-scratch"
MOUNT_POINT="$SCRATCH/mount"
PKG_EXPAND="$SCRATCH/pkg-expand"
AU_EXTRACT="$SCRATCH/au-extract"

mkdir -p "$DOWNLOADS" "$STAGING"

echo "==> Airwindows Consolidated $VERSION"

if [ ! -f "$DOWNLOADS/$ARCHIVE" ]; then
    echo "    Downloading $ARCHIVE (~48 MB)"
    curl -fsSL -o "$DOWNLOADS/$ARCHIVE.partial" "$URL"
    mv "$DOWNLOADS/$ARCHIVE.partial" "$DOWNLOADS/$ARCHIVE"
else
    echo "    Using cached $ARCHIVE"
fi

rm -rf "$SCRATCH"
mkdir -p "$MOUNT_POINT"

echo "    Mounting DMG"
hdiutil attach -nobrowse -readonly -noautoopen \
    -mountpoint "$MOUNT_POINT" "$DOWNLOADS/$ARCHIVE" >/dev/null
trap 'hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true' EXIT

pkg="$(find "$MOUNT_POINT" -maxdepth 2 -name "*.pkg" -print -quit)"
if [ -z "$pkg" ]; then
    echo "!! No .pkg found in the mounted DMG"
    exit 1
fi

echo "    Expanding outer pkg"
pkgutil --expand "$pkg" "$PKG_EXPAND"

au_pkg="$(find "$PKG_EXPAND" -maxdepth 2 -type d -name "*_AU.pkg" -print -quit)"
if [ -z "$au_pkg" ]; then
    echo "!! No _AU.pkg sub-package found"
    echo "   Expanded contents:"
    ls -la "$PKG_EXPAND" | sed 's/^/     /'
    exit 1
fi

echo "    Extracting AU payload"
mkdir -p "$AU_EXTRACT"
(cd "$AU_EXTRACT" && tar xf "$au_pkg/Payload")

src="$(find "$AU_EXTRACT" -type d -name "*.component" -print -quit)"
if [ -z "$src" ]; then
    echo "!! No .component found in AU payload"
    find "$AU_EXTRACT" -maxdepth 4 -type d | sed 's/^/     /'
    exit 1
fi

bundle_name="$(basename "$src")"  # "Airwindows Consolidated.component"
dest="$STAGING/$bundle_name"
rm -rf "$dest"
cp -R "$src" "$dest"
echo "    staged $bundle_name"

echo "==> Done."
