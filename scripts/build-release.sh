#!/bin/bash
set -euo pipefail

# ── Configuration ─────────────────────────────────────────────
# Update these for your environment:
VERSION="${1:-0.2.0-beta1}"
IDENTITY="${CODESIGN_IDENTITY:-Developer ID Application: Will Haslett}"
APPLE_ID="${NOTARIZE_APPLE_ID:-usefulmoose@gmail.com}"
TEAM_ID="${NOTARIZE_TEAM_ID:-XXXXXXXXXX}"    # Your Apple Developer Team ID
KEYCHAIN_PROFILE="${NOTARIZE_PROFILE:-AC_PASSWORD}"  # stored via: xcrun notarytool store-credentials

APP_NAME="Performance"
BUILD_DIR="build"
APP_BUNDLE="$BUILD_DIR/${APP_NAME}_artefacts/Release/$APP_NAME.app"
DMG_NAME="$APP_NAME-$VERSION.dmg"
DIST_DIR="dist"

# ── 1. Clean release build ────────────────────────────────────
echo "==> Building $APP_NAME $VERSION (Release)..."
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release 2>/dev/null
cmake --build "$BUILD_DIR" --config Release --target "$APP_NAME" -- -j$(sysctl -n hw.ncpu)

if [ ! -d "$APP_BUNDLE" ]; then
    echo "ERROR: App bundle not found at $APP_BUNDLE"
    exit 1
fi

echo "==> Built: $APP_BUNDLE"

# ── 2. Code sign ──────────────────────────────────────────────
# --deep signs nested frameworks + AU plugins inside the bundle.
# --options runtime enables the hardened runtime (required for notarization).
echo "==> Code signing..."
codesign --deep --force --options runtime \
    --sign "$IDENTITY" \
    "$APP_BUNDLE"

codesign --verify --verbose "$APP_BUNDLE"
echo "==> Signed OK"

# ── 3. Create DMG ─────────────────────────────────────────────
mkdir -p "$DIST_DIR"
DMG_PATH="$DIST_DIR/$DMG_NAME"

echo "==> Creating DMG..."
hdiutil create -volname "$APP_NAME $VERSION" \
    -srcfolder "$APP_BUNDLE" \
    -ov -format UDBZ \
    "$DMG_PATH"

echo "==> DMG: $DMG_PATH"

# ── 4. Notarize ───────────────────────────────────────────────
# Submits the DMG to Apple's notary service and waits for approval.
# Typically takes 2-5 minutes. The --wait flag blocks until done.
#
# First-time setup: store credentials once with:
#   xcrun notarytool store-credentials AC_PASSWORD \
#       --apple-id you@email.com --team-id TEAMID --password <app-specific-password>
#
# Generate the app-specific password at https://appleid.apple.com/account/manage
echo "==> Notarizing (this may take a few minutes)..."
xcrun notarytool submit "$DMG_PATH" \
    --keychain-profile "$KEYCHAIN_PROFILE" \
    --wait

# ── 5. Staple ─────────────────────────────────────────────────
# Attaches the notarization ticket to the DMG so it works offline.
echo "==> Stapling notarization ticket..."
xcrun stapler staple "$DMG_PATH"

echo ""
echo "============================================="
echo "  Ready to distribute: $DMG_PATH"
echo "  Version: $VERSION"
echo "============================================="
