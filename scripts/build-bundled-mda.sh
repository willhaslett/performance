#!/usr/bin/env bash
# Build the mda plugin suite from hollance/mda-plugins-juce (MIT) as AU
# bundles, sign each with our Developer ID, and copy the 11 we ship into
# runtime/bundled-plugins/components/.
#
# Why this script:
#   Step 2 of docs/BUNDLED_PLUGINS.md. The mda AU binaries on SourceForge
#   are pre-notarization (2020). Rebuilding against the modernized
#   hollance fork gives us signed, arm64-native, notarizable bundles.
#
# Usage:   scripts/build-bundled-mda.sh
# Result:  11 mda*.component bundles in runtime/bundled-plugins/components/
# Requires: Xcode command-line tools, cmake, a Developer ID signing
#           identity in the login keychain (we use Apple Team ID H25TK2U8FA).

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST_DIR="$REPO_ROOT/runtime/bundled-plugins/components"
SCRATCH_DIR="${SCRATCH_DIR:-$REPO_ROOT/.cache/mda-build}"
MDA_REPO="https://github.com/hollance/mda-plugins-juce.git"
MDA_REF="${MDA_REF:-main}"
DEVELOPER_ID="Developer ID Application: William Haslett (H25TK2U8FA)"

# The 11 mda plugins we actually ship. Others are built but discarded.
WANTED=(
    "mda ePiano"
    "mda JX10"
    "mda DX10"
    "mda Piano"
    "mda DubDelay"
    "mda Leslie"
    "mda RingMod"
    "mda Talkbox"
    "mda Stereo"
    "mda Combo"
    "mda Bandisto"
)

mkdir -p "$DEST_DIR" "$SCRATCH_DIR"

echo "==> Cloning mda-plugins-juce into $SCRATCH_DIR"
if [ -d "$SCRATCH_DIR/mda-plugins-juce/.git" ]; then
    git -C "$SCRATCH_DIR/mda-plugins-juce" fetch --quiet
    git -C "$SCRATCH_DIR/mda-plugins-juce" checkout --quiet "$MDA_REF"
    git -C "$SCRATCH_DIR/mda-plugins-juce" pull --quiet --ff-only
else
    git clone --quiet --depth 1 --branch "$MDA_REF" "$MDA_REPO" \
        "$SCRATCH_DIR/mda-plugins-juce"
fi
cd "$SCRATCH_DIR/mda-plugins-juce"

# Capture the exact commit for the LICENSES manifest (added by later steps).
MDA_COMMIT="$(git rev-parse HEAD)"
echo "==> Built from commit $MDA_COMMIT"

echo "==> Configuring AU build"
# The hollance fork is a JUCE CMake project. AU format is on by default on
# macOS; other formats get disabled here to keep the build small.
cmake -S . -B build-au \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DBUILD_VST3=OFF -DBUILD_VST=OFF -DBUILD_LV2=OFF -DBUILD_CLAP=OFF \
    > /dev/null

echo "==> Building (this takes several minutes)"
cmake --build build-au --parallel

echo "==> Locating built .component bundles"
# JUCE's default output layout: <project>/<target>_artefacts/Release/AU/<name>.component
mapfile -t BUILT < <(find build-au -type d -name "*.component" -path "*/AU/*" | sort)
if [ "${#BUILT[@]}" -eq 0 ]; then
    echo "!! No .component bundles found under build-au. Check the JUCE build output."
    exit 1
fi
echo "   Found ${#BUILT[@]} .component bundles."

echo "==> Copying, signing, and placing the 11 we ship"
for name in "${WANTED[@]}"; do
    found=""
    for b in "${BUILT[@]}"; do
        base="$(basename "$b")"  # e.g. "mda ePiano.component"
        if [ "$base" = "${name}.component" ]; then
            found="$b"
            break
        fi
    done
    if [ -z "$found" ]; then
        echo "!! Missing expected plugin: $name"
        exit 1
    fi

    dest="$DEST_DIR/${name}.component"
    rm -rf "$dest"
    cp -R "$found" "$dest"

    echo "   signing $(basename "$dest")"
    codesign --force --timestamp --options runtime \
        --sign "$DEVELOPER_ID" "$dest" 2>&1 | sed 's/^/     /'
done

echo ""
echo "==> Done. Placed ${#WANTED[@]} signed mda .component bundles in:"
echo "    $DEST_DIR"
echo ""
echo "Built from hollance/mda-plugins-juce commit $MDA_COMMIT"
echo "Record that commit in runtime/bundled-plugins/manifest.json or a"
echo "build-provenance file before committing the binaries."
