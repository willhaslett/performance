#!/usr/bin/env bash
# Notarize every signed .component bundle in .cache/staging/components/
# with Apple, then staple the approval ticket so the bundle passes
# Gatekeeper offline.
#
# Per-plugin (not per-release) because each bundle ships independently
# from S3 and is notarized once. Re-running is safe: already-stapled
# bundles are skipped via `stapler validate`, so a partial run can be
# resumed after a failure or network hiccup without re-submitting.
#
# Usage:   scripts/bundled-plugins/notarize-all.sh
# Requires: the AC_PASSWORD keychain profile (set up once with
#           `xcrun notarytool store-credentials AC_PASSWORD ...`).
#
# Notes:
#   - Each submission takes ~2-5 minutes. 15 plugins serial = ~30-75 min.
#     Serial rather than parallel because Apple throttles concurrent
#     submissions and parallel failure logs are noisy.
#   - The staple writes a ticket file *inside* the .component bundle
#     ("CodeResources"-adjacent). That bundle is what you distribute.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
STAGING="$REPO_ROOT/.cache/staging/components"
SCRATCH="$REPO_ROOT/.cache/notarize-scratch"
LOG_FILE="$REPO_ROOT/.cache/notarize-log.txt"
KEYCHAIN_PROFILE="${KEYCHAIN_PROFILE:-AC_PASSWORD}"

if [ ! -d "$STAGING" ]; then
    echo "!! $STAGING not found — run build/download scripts + sign-all.sh first" >&2
    exit 1
fi

# Sanity: keychain profile reachable before we submit anything.
if ! xcrun notarytool history --keychain-profile "$KEYCHAIN_PROFILE" >/dev/null 2>&1; then
    echo "!! notarytool can't read keychain profile '$KEYCHAIN_PROFILE'" >&2
    echo "   Create it with: xcrun notarytool store-credentials $KEYCHAIN_PROFILE ..." >&2
    exit 1
fi

mkdir -p "$SCRATCH"
date -u +"[%FT%TZ] notarize-all.sh start" >>"$LOG_FILE"

shopt -s nullglob
accepted=0; skipped=0; failed=0
for bundle in "$STAGING"/*.component; do
    name="$(basename "$bundle")"
    echo "==> $name"

    if xcrun stapler validate "$bundle" >/dev/null 2>&1; then
        echo "    already stapled, skipping"
        skipped=$((skipped + 1))
        continue
    fi

    zip="$SCRATCH/${name}.zip"
    rm -f "$zip"
    # ditto --keepParent preserves the .component directory structure
    # inside the zip; notarytool expects that for bundle submissions.
    ditto -c -k --keepParent "$bundle" "$zip"

    echo "    submitting ($(du -h "$zip" | awk '{print $1}'))"
    submit_out="$(xcrun notarytool submit "$zip" \
        --keychain-profile "$KEYCHAIN_PROFILE" \
        --wait 2>&1)" || true

    sub_id="$(echo "$submit_out" | awk -F': ' '/^  id:/ {print $2; exit}')"
    status="$(echo "$submit_out" | awk -F': ' '/^  status:/ {print $2; exit}')"

    echo "[$name] id=$sub_id status=$status" >>"$LOG_FILE"

    if [ "$status" != "Accepted" ]; then
        echo "!! submission $sub_id: status=$status"
        if [ -n "$sub_id" ]; then
            echo "   developer log:"
            xcrun notarytool log "$sub_id" \
                --keychain-profile "$KEYCHAIN_PROFILE" 2>&1 | sed 's/^/     /' | head -60
        fi
        # Fail fast: a signing / entitlement problem on one plugin is
        # almost certainly the same on all of them. The script is
        # restartable, so fix + rerun picks up where we left off.
        echo "[$name] bailing after first failure" >>"$LOG_FILE"
        exit 1
    fi

    echo "    stapling"
    xcrun stapler staple "$bundle" 2>&1 | sed 's/^/     /'
    xcrun stapler validate "$bundle" >/dev/null
    rm -f "$zip"
    accepted=$((accepted + 1))
done

rmdir "$SCRATCH" 2>/dev/null || true

echo ""
echo "==> Done. accepted=$accepted skipped=$skipped failed=$failed"
date -u +"[%FT%TZ] notarize-all.sh end (accepted=$accepted skipped=$skipped failed=$failed)" >>"$LOG_FILE"

if [ "$failed" -gt 0 ]; then
    exit 1
fi
