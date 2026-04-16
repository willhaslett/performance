#!/bin/bash
set -euo pipefail

# ── Configuration ─────────────────────────────────────────────
# Version: pass as first argument or defaults to 0.2.0-beta1
VERSION="${1:-0.2.0-beta1}"

# Code signing identity (from: security find-identity -v -p codesigning)
IDENTITY="Developer ID Application: William Haslett (H25TK2U8FA)"

# Notarization credentials stored in Keychain via:
#   xcrun notarytool store-credentials AC_PASSWORD \
#       --apple-id will.haslett@gmail.com --team-id H25TK2U8FA \
#       --password <app-specific-password>
KEYCHAIN_PROFILE="AC_PASSWORD"

APP_NAME="Performance"
BUILD_DIR="build"
APP_BUNDLE="$BUILD_DIR/${APP_NAME}_artefacts/Release/$APP_NAME.app"
DMG_NAME="$APP_NAME-$VERSION.dmg"
DIST_DIR="dist"

# ── Preflight checks ─────────────────────────────────────────
if ! security find-identity -v -p codesigning | grep -q "$IDENTITY"; then
    echo "ERROR: Signing identity not found: $IDENTITY"
    echo "Run: security find-identity -v -p codesigning"
    exit 1
fi

echo "==> Building $APP_NAME $VERSION"
echo ""

# ── 1. Release build ─────────────────────────────────────────
echo "==> [1/5] Building (Release)..."
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release 2>/dev/null
cmake --build "$BUILD_DIR" --config Release --target "$APP_NAME" -- -j$(sysctl -n hw.ncpu)

if [ ! -d "$APP_BUNDLE" ]; then
    echo "ERROR: App bundle not found at $APP_BUNDLE"
    exit 1
fi

# ── 2. Code sign ──────────────────────────────────────────────
echo "==> [2/5] Code signing..."
codesign --deep --force --options runtime \
    --sign "$IDENTITY" \
    "$APP_BUNDLE"

codesign --verify --verbose "$APP_BUNDLE"

# ── 3. Create DMG ─────────────────────────────────────────────
echo "==> [3/5] Creating DMG..."
mkdir -p "$DIST_DIR"
DMG_PATH="$DIST_DIR/$DMG_NAME"

hdiutil create -volname "$APP_NAME $VERSION" \
    -srcfolder "$APP_BUNDLE" \
    -ov -format UDBZ \
    "$DMG_PATH"

# ── 4. Notarize ───────────────────────────────────────────────
echo "==> [4/5] Notarizing (typically 2-5 minutes)..."
xcrun notarytool submit "$DMG_PATH" \
    --keychain-profile "$KEYCHAIN_PROFILE" \
    --wait

# ── 5. Staple ─────────────────────────────────────────────────
echo "==> [5/5] Stapling notarization ticket..."
xcrun stapler staple "$DMG_PATH"

echo ""
echo "============================================="
echo "  Ready to distribute: $DMG_PATH"
echo "  Version: $VERSION"
echo "============================================="
