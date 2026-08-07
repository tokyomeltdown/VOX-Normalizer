#!/bin/bash
# ============================================================
#  VOX Normalizer  make_pkg.sh
#
#  Builds a .pkg installer from the signed and notarized .app that notarize.sh
#  leaves in /tmp, then notarizes and staples the .pkg itself.
#
#  Run bash notarize.sh first.
#
#  v1.2 switched from .dmg to .pkg, to match LUFSBar and caps REC. There is no
#  drag-and-drop step to explain, and it installs with a double click straight
#  from the GitHub Releases link.
# ============================================================
set -e

# ---- Settings (specific to this machine and account) ----
SIGN_INST="Developer ID Installer: Ryo Yoneya (WDFKYGRKRW)"
NOTARY_PROFILE="VOXNotary"
APP_NAME="VOX Normalizer"
VERSION="1.2"
BUNDLE_ID="com.tokyomeltdown.voxnormalizer"

# ---- Paths ----
ROOT="$(cd "$(dirname "$0")" && pwd)"
APP_TMP="/tmp/$APP_NAME.app"          # the stapled .app left behind by notarize.sh
WORK="/tmp/VOX_Normalizer_pkg_work"
DIST_DIR="$ROOT/dist"
PKG_SIGNED="$DIST_DIR/${APP_NAME// /_}_${VERSION}.pkg"

# A copy without the version number. Uploading it to GitHub Releases under this
# name makes releases/latest/download/VOX_Normalizer.pkg always point at the newest
# build, so the download button on the site never needs updating.
PKG_LATEST="$DIST_DIR/${APP_NAME// /_}.pkg"

echo "=========================================="
echo "  VOX Normalizer  make_pkg.sh  (v${VERSION})"
echo "=========================================="

if [ ! -d "$APP_TMP" ]; then
    echo "ERROR: signed and notarized app not found: $APP_TMP"
    echo "Run bash notarize.sh first."
    exit 1
fi

# Check it is stapled: shipping an unstapled .app requires a network connection on first launch
xcrun stapler validate "$APP_TMP" > /dev/null 2>&1 || {
    echo "ERROR: no notarization ticket stapled to $APP_TMP"
    echo "Run bash notarize.sh through to the end."
    exit 1
}

rm -rf "$WORK"
mkdir -p "$WORK/root/Applications" "$DIST_DIR"
ditto --norsrc --noextattr --noqtn --noacl "$APP_TMP" "$WORK/root/Applications/$APP_NAME.app"

# No postinstall script on purpose.
#   LUFSBar and caps REC sit in the menu bar, so they launch themselves right
#   after installing. VOX Normalizer is a normal app you open when you need it,
#   so it should not start on its own.
# ---- Step 1: pkgbuild (installs into /Applications) + Developer ID Installer signature ----
echo "[1/5] pkgbuild + sign ..."
pkgbuild \
    --root "$WORK/root" \
    --install-location "/" \
    --identifier "$BUNDLE_ID" \
    --version "$VERSION" \
    --sign "$SIGN_INST" \
    "$PKG_SIGNED"

# ---- Step 2: notarize the pkg ----
echo "[2/5] notarize pkg (submit & wait) ..."
xcrun notarytool submit "$PKG_SIGNED" \
    --keychain-profile "$NOTARY_PROFILE" --wait

# ---- Step 3: staple ----
echo "[3/5] staple ..."
xcrun stapler staple "$PKG_SIGNED"
xcrun stapler validate "$PKG_SIGNED"

# ---- Step 4: verify ----
echo "[4/5] verify ..."
spctl -a -vvv -t install "$PKG_SIGNED" || true
pkgutil --check-signature "$PKG_SIGNED"

# ---- Step 5: version-less copy for the GitHub Releases upload ----
#   This is a plain copy of an already signed and stapled file, so the signature
#   and the notarization ticket stay valid.
echo "[5/5] copy version-less release asset ..."
cp "$PKG_SIGNED" "$PKG_LATEST"
pkgutil --check-signature "$PKG_LATEST"

echo ""
echo "=========================================="
echo "  .pkg is ready."
echo "  For the archive : $PKG_SIGNED"
echo "  For Releases    : $PKG_LATEST"
echo "=========================================="
