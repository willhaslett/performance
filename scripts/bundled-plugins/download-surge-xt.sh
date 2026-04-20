#!/usr/bin/env bash
# Download Surge XT + Surge XT Effects macOS universal AU bundles
# (GPL-3.0) from the upstream release. Extracts to .cache/staging/
# alongside other bundled-plugin sources.
#
# Upstream bumps:
#   - update VERSION below (check https://github.com/surge-synthesizer/releases-xt/releases)
#   - the "pluginsonly" zip pattern has been stable since at least 1.0
#
# Usage:   scripts/bundled-plugins/download-surge-xt.sh
# Result:  Surge XT.component, Surge XT Effects.component in
#          .cache/staging/components/

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="${VERSION:-1.3.4}"
ARCHIVE="surge-xt-macos-${VERSION}-pluginsonly.zip"
URL="https://github.com/surge-synthesizer/releases-xt/releases/download/${VERSION}/${ARCHIVE}"

DOWNLOADS="$REPO_ROOT/.cache/downloads"
STAGING="$REPO_ROOT/.cache/staging/components"
EXTRACT="$REPO_ROOT/.cache/surge-xt-extract"

mkdir -p "$DOWNLOADS" "$STAGING" "$EXTRACT"

echo "==> Surge XT $VERSION"

if [ ! -f "$DOWNLOADS/$ARCHIVE" ]; then
    echo "    Downloading $ARCHIVE (~187 MB)"
    curl -fsSL -o "$DOWNLOADS/$ARCHIVE.partial" "$URL"
    mv "$DOWNLOADS/$ARCHIVE.partial" "$DOWNLOADS/$ARCHIVE"
else
    echo "    Using cached $ARCHIVE"
fi

echo "    Extracting"
rm -rf "$EXTRACT"
mkdir -p "$EXTRACT"
unzip -q "$DOWNLOADS/$ARCHIVE" -d "$EXTRACT"

for want in "Surge XT.component" "Surge XT Effects.component"; do
    src="$(find "$EXTRACT" -type d -name "$want" -print -quit)"
    if [ -z "$src" ]; then
        echo "!! Could not find $want in the extracted archive"
        echo "   Contents:"
        find "$EXTRACT" -maxdepth 4 -type d -name "*.component" | sed 's/^/     /'
        exit 1
    fi
    dest="$STAGING/$want"
    rm -rf "$dest"
    cp -R "$src" "$dest"
    echo "    staged $want"
done

echo "==> Done."
