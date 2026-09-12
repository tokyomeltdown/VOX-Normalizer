#!/bin/bash
# ============================================================
#  VOX Normalizer  notarize.sh
#  Takes the Standalone build (Release / Universal) through
#    Developer ID signing -> notarization -> stapling -> a distributable zip
#  Standalone is the only format. AAX was dropped in v1.2 (see the top of build.sh).
# ============================================================
set -e  # stop at the first error

# ---- Settings (specific to this machine and account) ----
# The signing identity names the Apple account and the team it belongs to.
# Every signed copy carries it, so it is not much of a secret, but AGENTS.md
# section 3 keeps values of that kind out of the source. It is read from the
# environment, or from one line in a file outside the tree.
SIGNER_FILE="$HOME/.config/tokyomeltdown/signer"
if [ -z "${SIGNER:-}" ] && [ -f "$SIGNER_FILE" ]; then
    SIGNER="$(head -1 "$SIGNER_FILE")"
fi
if [ -z "${SIGNER:-}" ]; then
    echo "ERROR: no signing identity."
    echo "Put the account name and team on one line in $SIGNER_FILE"
    exit 1
fi
SIGN_ID="Developer ID Application: $SIGNER"
NOTARY_PROFILE="VOXNotary"                                    # notarytool keychain profile
APP_NAME="VOX Normalizer"                                     # product name (.app name)
VERSION="1.2"                                                 # used in the zip file name

# ---- Paths ----
ROOT="$(cd "$(dirname "$0")" && pwd)"
PROJUCER=~/Documents/JUCE/Projucer.app/Contents/MacOS/Projucer
JUCER_FILE="$ROOT/VUClipGainNormalizer.jucer"
PBXPROJ="$ROOT/Builds/MacOSX/VUClipGainNormalizer.xcodeproj/project.pbxproj"
BUILD_DIR="$ROOT/build"
APP="$BUILD_DIR/Release/$APP_NAME.app"          # build output (inside the synced folder)
APP_TMP="/tmp/$APP_NAME.app"                     # working copy for signing, outside it
DIST_DIR="$ROOT/dist"
ZIP_NOTARIZE="/tmp/${APP_NAME// /_}_notarize.zip"
ZIP_DIST="$DIST_DIR/${APP_NAME// /_}_${VERSION}.zip"

echo "=========================================="
echo "  VOX Normalizer  Notarize Build (Release / Universal)"
echo "=========================================="

# ---- Step 1: regenerate the Xcode project with the Projucer ----
echo "[1/8] Projucer --resave ..."
"$PROJUCER" --resave "$JUCER_FILE" --fix-missing-dependencies

# ---- Step 2: patch the pbxproj (same fix as build.sh) ----
echo "[2/8] pbxproj patch ..."
# Stop ditto copying extended attributes
sed -i '' 's| ditto | ditto --norsrc --noextattr --noqtn --noacl |g' "$PBXPROJ"
# v1.2: the AAX SDK header paths are no longer re-added, since AAX was dropped

# ---- Step 3: Release build (Standalone, universal, signed manually later) ----
echo "[3/8] xcodebuild Release Universal (Standalone) ..."
xcodebuild \
    -project "$ROOT/Builds/MacOSX/VUClipGainNormalizer.xcodeproj" \
    -configuration Release \
    -target "VUClipGainNormalizer - Standalone Plugin" \
    ARCHS="x86_64 arm64" \
    ONLY_ACTIVE_ARCH=NO \
    CODE_SIGN_IDENTITY="" \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGNING_ALLOWED=NO \
    | grep -E "^(Build|error:|warning:|\*\*)" || true

if [ ! -d "$APP" ]; then
    echo "  ERROR: build output not found: $APP"
    exit 1
fi

# ---- Step 4: clean copy to /tmp, fix CFBundleName, sign with the Developer ID ----
#   IMPORTANT: the project lives under a synced/watched folder, which attaches
#   com.apple.FinderInfo just before signing and makes codesign fail every time.
#   Copying to /tmp with ditto --noextattr first avoids it.
#   --options runtime : hardened runtime, required for notarization
#   --timestamp       : secure timestamp, also required
echo "[4/8] clean copy to /tmp + codesign (Developer ID + hardened runtime) ..."
rm -rf "$APP_TMP"
ditto --norsrc --noextattr --noqtn --noacl "$APP" "$APP_TMP"
/usr/libexec/PlistBuddy -c "Set :CFBundleName '$APP_NAME'" "$APP_TMP/Contents/Info.plist" 2>/dev/null || true
codesign --force --options runtime --timestamp --sign "$SIGN_ID" "$APP_TMP"

# ---- Step 5: verify the signature ----
echo "[5/8] verify signature ..."
codesign --verify --strict --verbose=2 "$APP_TMP"

# ---- Step 6: zip for submission, then notarize and wait ----
echo "[6/8] notarize (submit & wait) ..."
rm -f "$ZIP_NOTARIZE"
ditto -c -k --keepParent "$APP_TMP" "$ZIP_NOTARIZE"
xcrun notarytool submit "$ZIP_NOTARIZE" \
    --keychain-profile "$NOTARY_PROFILE" --wait

# ---- Step 7: staple the ticket to the .app and validate ----
echo "[7/8] staple ..."
xcrun stapler staple "$APP_TMP"
xcrun stapler validate "$APP_TMP"
spctl -a -vvv "$APP_TMP" || true   # Gatekeeper assessment, shown for reference

# ---- Step 8: repackage the stapled .app from /tmp into dist/ ----
echo "[8/8] package distribution zip ..."
mkdir -p "$DIST_DIR"
rm -f "$ZIP_DIST"
ditto -c -k --keepParent "$APP_TMP" "$ZIP_DIST"

echo ""
echo "=========================================="
echo "  Notarization complete."
echo "  Distributable zip : $ZIP_DIST"
echo ""
echo "  Next, run  bash make_pkg.sh  to build the installer"
echo "  (from v1.2 the distributable is a .pkg, not a .dmg)"
echo "=========================================="
