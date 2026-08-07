#!/bin/bash
# ============================================================
# VOX Normalizer — development build (Debug)
#
# Formats: Standalone only.
#   AAX was dropped in v1.2 because:
#     - only the Standalone build is distributed; AAX existed purely so the
#       author could check the plugin in Pro Tools
#     - the source is public, and the AAX SDK is under NDA from Avid, so a local
#       absolute path to it cannot live in the repository (nobody else could build)
#     - the AGPLv3 route cannot be used for AAX / VST2 / AUv3 / iOS targets
#   The AAX branches inside Source (the wrapperType checks) are left in place as
#   harmless dead code: removing them is riskier than keeping them.
#
# Verified with JUCE 8.0.12 on macOS 15 with Xcode 16 through 26.
#
# For a distributable build use notarize.sh then make_pkg.sh, not this script.
# ============================================================

set -e  # stop at the first error

PROJUCER=~/Documents/JUCE/Projucer.app/Contents/MacOS/Projucer
JUCER_FILE="$(dirname "$0")/VUClipGainNormalizer.jucer"
PBXPROJ="$(dirname "$0")/Builds/MacOSX/VUClipGainNormalizer.xcodeproj/project.pbxproj"

echo "=========================================="
echo "  VOX Normalizer Build Script (Debug)"
echo "=========================================="

# ---- Step 1: regenerate the Xcode project with the Projucer ----
echo "[1/3] Projucer --resave ..."
"$PROJUCER" --resave "$JUCER_FILE" --fix-missing-dependencies

# ---- Step 2: patch the pbxproj ----
# Adds the flags that stop ditto copying extended attributes.
# Without this, CodeSign fails because of the attributes it would otherwise copy.
echo "[2/3] pbxproj patch ..."
sed -i '' 's| ditto | ditto --norsrc --noextattr --noqtn --noacl |g' "$PBXPROJ"

# ---- Step 3: build with code signing disabled ----
echo "[3/3] xcodebuild ..."
xcodebuild \
    -project "$(dirname "$0")/Builds/MacOSX/VUClipGainNormalizer.xcodeproj" \
    -configuration Debug \
    -arch arm64 \
    -target "VUClipGainNormalizer - Standalone Plugin" \
    CODE_SIGN_IDENTITY="" \
    CODE_SIGNING_REQUIRED=NO \
    CODE_SIGNING_ALLOWED=NO \
    | grep -E "^(Build|error:|warning:|note:|\*\*)" || true

# ---- Step 3.5: set CFBundleName on the Standalone .app ----
# The Projucer project name is the Xcode project name, which is still the old one,
# so only the name shown in the menu bar is overwritten here.
APP_PLIST="$(dirname "$0")/build/Debug/VOX Normalizer.app/Contents/Info.plist"
if [ -f "$APP_PLIST" ]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleName 'VOX Normalizer'" "$APP_PLIST"
    echo "  CFBundleName → VOX Normalizer"
fi

echo ""
echo "=========================================="
echo "  Build complete!"
echo "  App: build/Debug/VOX Normalizer.app"
echo "=========================================="
