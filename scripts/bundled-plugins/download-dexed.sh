#!/usr/bin/env bash
# Download Dexed (GPL-3.0, DX7 emulator) macOS AU bundle from upstream.
#
# Upstream bumps:
#   - update VERSION below (check https://github.com/asb2m10/dexed/releases)
#
# Usage:   scripts/bundled-plugins/download-dexed.sh
# Result:  Dexed.component in .cache/staging/components/

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-1.0.1}"
ARCHIVE="Dexed-${VERSION}-macOS.zip"
URL="https://github.com/asb2m10/dexed/releases/download/v${VERSION}/${ARCHIVE}"

DOWNLOADS="$REPO_ROOT/.cache/downloads"
STAGING="$REPO_ROOT/.cache/staging/components"
EXTRACT="$REPO_ROOT/.cache/dexed-extract"

mkdir -p "$DOWNLOADS" "$STAGING" "$EXTRACT"

echo "==> Dexed $VERSION"

if [ ! -f "$DOWNLOADS/$ARCHIVE" ]; then
    echo "    Downloading $ARCHIVE (~17 MB)"
    curl -fsSL -o "$DOWNLOADS/$ARCHIVE.partial" "$URL"
    mv "$DOWNLOADS/$ARCHIVE.partial" "$DOWNLOADS/$ARCHIVE"
else
    echo "    Using cached $ARCHIVE"
fi

echo "    Extracting"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
unzip -q "$DOWNLOADS/$ARCHIVE" -d "$EXTRACT"

src="$(find "$EXTRACT" -type d -name "Dexed.component" -print -quit)"
if [ -z "$src" ]; then
    echo "!! Could not find Dexed.component in the extracted archive"
    find "$EXTRACT" -maxdepth 4 -type d -name "*.component" | sed 's/^/     /'
    exit 1
fi
dest="$STAGING/Dexed.component"
rm -rf "$dest"
cp -R "$src" "$dest"
echo "    staged Dexed.component"

echo "==> Done."
