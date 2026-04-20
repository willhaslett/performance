#!/usr/bin/env bash
# Download Airwindows Consolidated (MIT) — ~200 Chris Johnson character
# effects in a single plugin. macOS universal AU from baconpaul's
# airwin2rack release.
#
# Upstream bumps:
#   - the airwin2rack project uses a rolling "DAWPlugin" tag and rebuilds
#     the DMG periodically. Update VERSION below to the dated filename of
#     the release asset; check
#     https://github.com/baconpaul/airwin2rack/releases/tag/DAWPlugin
#
# Usage:   scripts/bundled-plugins/download-airwindows.sh
# Result:  Consolidated.component in .cache/staging/components/

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-2026-04-19-7f5a66c}"
ARCHIVE="airwindows-consolidated-macOS-${VERSION}.dmg"
URL="https://github.com/baconpaul/airwin2rack/releases/download/DAWPlugin/${ARCHIVE}"

DOWNLOADS="$REPO_ROOT/.cache/downloads"
STAGING="$REPO_ROOT/.cache/staging/components"
MOUNT_POINT="$REPO_ROOT/.cache/airwindows-mount"

mkdir -p "$DOWNLOADS" "$STAGING"

echo "==> Airwindows Consolidated $VERSION"

if [ ! -f "$DOWNLOADS/$ARCHIVE" ]; then
    echo "    Downloading $ARCHIVE (~48 MB)"
    curl -fsSL -o "$DOWNLOADS/$ARCHIVE.partial" "$URL"
    mv "$DOWNLOADS/$ARCHIVE.partial" "$DOWNLOADS/$ARCHIVE"
else
    echo "    Using cached $ARCHIVE"
fi

echo "    Mounting DMG"
rm -rf "$MOUNT_POINT"
mkdir -p "$MOUNT_POINT"
hdiutil attach -nobrowse -readonly -noautoopen \
    -mountpoint "$MOUNT_POINT" "$DOWNLOADS/$ARCHIVE" >/dev/null

trap 'hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true' EXIT

echo "    Copying Consolidated.component"
src="$(find "$MOUNT_POINT" -type d -name "Consolidated.component" -print -quit)"
if [ -z "$src" ]; then
    echo "!! Could not find Consolidated.component in the mounted DMG"
    echo "   Contents (*.component):"
    find "$MOUNT_POINT" -maxdepth 4 -type d -name "*.component" | sed 's/^/     /'
    exit 1
fi
dest="$STAGING/Consolidated.component"
rm -rf "$dest"
cp -R "$src" "$dest"
echo "    staged Consolidated.component"

echo "==> Done."
