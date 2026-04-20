#!/usr/bin/env bash
# Download Dragonfly Reverb family (GPL-3.0): Hall, Room, Plate, and
# Early Reflections. Universal macOS DMG from upstream.
#
# Upstream bumps:
#   - update VERSION below (check https://github.com/michaelwillis/dragonfly-reverb/releases)
#
# Usage:   scripts/bundled-plugins/download-dragonfly.sh
# Result:  DragonflyHallReverb.component, DragonflyRoomReverb.component,
#          DragonflyPlateReverb.component, DragonflyEarlyReflections.component
#          in .cache/staging/components/

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-3.2.10}"
ARCHIVE="dragonfly-reverb-${VERSION}-macos-universal.dmg"
URL="https://github.com/michaelwillis/dragonfly-reverb/releases/download/${VERSION}/${ARCHIVE}"

DOWNLOADS="$REPO_ROOT/.cache/downloads"
STAGING="$REPO_ROOT/.cache/staging/components"
MOUNT_POINT="$REPO_ROOT/.cache/dragonfly-mount"

mkdir -p "$DOWNLOADS" "$STAGING"

echo "==> Dragonfly Reverb $VERSION"

if [ ! -f "$DOWNLOADS/$ARCHIVE" ]; then
    echo "    Downloading $ARCHIVE (~25 MB)"
    curl -fsSL -o "$DOWNLOADS/$ARCHIVE.partial" "$URL"
    mv "$DOWNLOADS/$ARCHIVE.partial" "$DOWNLOADS/$ARCHIVE"
else
    echo "    Using cached $ARCHIVE"
fi

# Mount the DMG read-only to a fixed mount point.
echo "    Mounting DMG"
rm -rf "$MOUNT_POINT"
mkdir -p "$MOUNT_POINT"
hdiutil attach -nobrowse -readonly -noautoopen \
    -mountpoint "$MOUNT_POINT" "$DOWNLOADS/$ARCHIVE" >/dev/null

# Always unmount on exit, including errors.
trap 'hdiutil detach "$MOUNT_POINT" -quiet 2>/dev/null || true' EXIT

echo "    Copying .component bundles"
wanted=(
    "DragonflyHallReverb.component"
    "DragonflyRoomReverb.component"
    "DragonflyPlateReverb.component"
    "DragonflyEarlyReflections.component"
)
for w in "${wanted[@]}"; do
    src="$(find "$MOUNT_POINT" -type d -name "$w" -print -quit)"
    if [ -z "$src" ]; then
        echo "!! Could not find $w in the mounted DMG"
        echo "   Contents:"
        find "$MOUNT_POINT" -maxdepth 4 -type d -name "*.component" | sed 's/^/     /'
        exit 1
    fi
    dest="$STAGING/$w"
    rm -rf "$dest"
    cp -R "$src" "$dest"
    echo "    staged $w"
done

echo "==> Done."
