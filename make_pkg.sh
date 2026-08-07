#!/bin/bash
# ============================================================
#  VOX Normalizer  make_pkg.sh
#
#  notarize.sh が /tmp に残した署名・公証済み .app から
#  .pkg インストーラーを作り、.pkg 自体も公証して staple する。
#
#  ※ 先に bash notarize.sh を実行しておくこと
#
#  v1.2 で .dmg から .pkg に変更した（LUFSBar / caps REC と揃えるため）。
#  ドラッグ&ドロップの説明が要らず、GitHub Releases の直リンクから
#  ダブルクリックだけで入る形になる。
# ============================================================
set -e

# ---- 設定（環境固有の値）----
SIGN_INST="Developer ID Installer: Ryo Yoneya (WDFKYGRKRW)"
NOTARY_PROFILE="VOXNotary"
APP_NAME="VOX Normalizer"
VERSION="1.2"
BUNDLE_ID="com.tokyomeltdown.voxnormalizer"

# ---- パス ----
ROOT="$(cd "$(dirname "$0")" && pwd)"
APP_TMP="/tmp/$APP_NAME.app"          # notarize.sh が staple 済みで残す .app
WORK="/tmp/VOX_Normalizer_pkg_work"
DIST_DIR="$ROOT/dist"
PKG_SIGNED="$DIST_DIR/${APP_NAME// /_}_${VERSION}.pkg"

# バージョン番号なしのコピー。GitHub Releases にこのファイル名でアップロードすると
# releases/latest/download/VOX_Normalizer.pkg が常に最新版を指すようになる。
# LP のダウンロードボタンはこの URL を使うので、更新のたびにリンクを直す必要がない。
PKG_LATEST="$DIST_DIR/${APP_NAME// /_}.pkg"

echo "=========================================="
echo "  VOX Normalizer  make_pkg.sh  (v${VERSION})"
echo "=========================================="

if [ ! -d "$APP_TMP" ]; then
    echo "ERROR: 署名・公証済みアプリが見つかりません: $APP_TMP"
    echo "先に bash notarize.sh を実行してください。"
    exit 1
fi

# staple 済みかを確認（未 staple の .app を配ると初回起動でネット必須になる）
xcrun stapler validate "$APP_TMP" > /dev/null 2>&1 || {
    echo "ERROR: $APP_TMP に公証チケットが staple されていません。"
    echo "bash notarize.sh を最後まで通してください。"
    exit 1
}

rm -rf "$WORK"
mkdir -p "$WORK/root/Applications" "$DIST_DIR"
ditto --norsrc --noextattr --noqtn --noacl "$APP_TMP" "$WORK/root/Applications/$APP_NAME.app"

# ※ postinstall スクリプトは付けない。
#    LUFSBar / caps REC は常駐アプリなのでインストール直後に自動起動させているが、
#    VOX Normalizer は必要なときに開く通常のアプリなので、勝手に起動しない方がよい。

# ---- Step 1: pkgbuild（/Applications へインストール）+ Developer ID Installer 署名 ----
echo "[1/5] pkgbuild + sign ..."
pkgbuild \
    --root "$WORK/root" \
    --install-location "/" \
    --identifier "$BUNDLE_ID" \
    --version "$VERSION" \
    --sign "$SIGN_INST" \
    "$PKG_SIGNED"

# ---- Step 2: pkg を公証 ----
echo "[2/5] notarize pkg (submit & wait) ..."
xcrun notarytool submit "$PKG_SIGNED" \
    --keychain-profile "$NOTARY_PROFILE" --wait

# ---- Step 3: staple ----
echo "[3/5] staple ..."
xcrun stapler staple "$PKG_SIGNED"
xcrun stapler validate "$PKG_SIGNED"

# ---- Step 4: 検証 ----
echo "[4/5] verify ..."
spctl -a -vvv -t install "$PKG_SIGNED" || true
pkgutil --check-signature "$PKG_SIGNED"

# ---- Step 5: バージョン無しコピーを作成（GitHub Releases アップロード用）----
#   署名済み・staple 済みファイルをそのままコピーするだけなので、
#   コピー後も署名 / 公証チケットはそのまま有効。
echo "[5/5] copy version-less release asset ..."
cp "$PKG_SIGNED" "$PKG_LATEST"
pkgutil --check-signature "$PKG_LATEST"

echo ""
echo "=========================================="
echo "  .pkg 完成！"
echo "  アーカイブ用           : $PKG_SIGNED"
echo "  Releases アップロード用 : $PKG_LATEST"
echo "=========================================="
