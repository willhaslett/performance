#!/usr/bin/env bash
# Re-sign every .component bundle in runtime/bundled-plugins/components/
# with our Developer ID. Used after dropping pre-built third-party
# plugins (Surge XT, Dexed, Odin 2, Dragonfly, Airwindows) into that
# directory.
#
# The upstream signatures are valid for THEIR distribution but macOS
# Gatekeeper checks bundle-level signatures at load time; re-signing
# with our cert keeps us in a single-signing-chain for notarization.
#
# Usage:   scripts/bundled-plugins/sign-all.sh
# Requires: a Developer ID signing identity in the login keychain.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DIR="$REPO_ROOT/.cache/staging/components"
DEVELOPER_ID="Developer ID Application: William Haslett (H25TK2U8FA)"

if [ ! -d "$DIR" ]; then
    echo "!! Components directory not found: $DIR"
    exit 1
fi

shopt -s nullglob
count=0
for bundle in "$DIR"/*.component; do
    name="$(basename "$bundle")"
    echo "==> signing $name"
    codesign --force --timestamp --options runtime --deep \
        --sign "$DEVELOPER_ID" "$bundle" 2>&1 | sed 's/^/    /'
    count=$((count + 1))
done

if [ "$count" -eq 0 ]; then
    echo "!! No .component bundles found in $DIR"
    exit 1
fi

echo ""
echo "==> Done. Signed $count .component bundles."
