#!/bin/bash
# ============================================================
#  VOX Normalizer  notarize.sh
#  Standalone (Release / Universal) を
#    Developer ID 署名 → 公証(notarize) → staple → 配布zip化
#  ※ 配布は Standalone のみ。AAX は v1.2 で廃止した（詳細は build.sh 冒頭のコメント）
# ============================================================
set -e  # エラーが出たら即停止

# ---- 設定（環境固有の値） ----
SIGN_ID="Developer ID Application: Ryo Yoneya (WDFKYGRKRW)"   # 配布署名ID
NOTARY_PROFILE="VOXNotary"                                    # notarytool キーチェーンプロファイル
APP_NAME="VOX Normalizer"                                     # 製品名（.app 名）
VERSION="1.2"                                                 # 配布バージョン（zip名に使う）

# ---- パス ----
ROOT="$(cd "$(dirname "$0")" && pwd)"
PROJUCER=~/Documents/JUCE/Projucer.app/Contents/MacOS/Projucer
JUCER_FILE="$ROOT/VUClipGainNormalizer.jucer"
PBXPROJ="$ROOT/Builds/MacOSX/VUClipGainNormalizer.xcodeproj/project.pbxproj"
BUILD_DIR="$ROOT/build"
APP="$BUILD_DIR/Release/$APP_NAME.app"          # ビルド成果物（監視フォルダ内）
APP_TMP="/tmp/$APP_NAME.app"                     # 署名・公証の作業用（監視外の /tmp）
DIST_DIR="$ROOT/dist"
ZIP_NOTARIZE="/tmp/${APP_NAME// /_}_notarize.zip"
ZIP_DIST="$DIST_DIR/${APP_NAME// /_}_${VERSION}.zip"

echo "=========================================="
echo "  VOX Normalizer  Notarize Build (Release / Universal)"
echo "=========================================="

# ---- Step 1: Projucer で Xcode プロジェクト再生成 ----
echo "[1/8] Projucer --resave ..."
"$PROJUCER" --resave "$JUCER_FILE" --fix-missing-dependencies

# ---- Step 2: pbxproj パッチ（build.sh と同じ罠対策） ----
echo "[2/8] pbxproj patch ..."
# 罠1: ditto に xattr 無効フラグ
sed -i '' 's| ditto | ditto --norsrc --noextattr --noqtn --noacl |g' "$PBXPROJ"
# v1.2: AAX 廃止に伴い AAX SDK ヘッダーパスの再付与は削除（build.sh も同様）

# ---- Step 3: Release ビルド（Standalone のみ・ユニバーサル・署名は後で手動） ----
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
    echo "  ERROR: ビルド成果物が見つかりません: $APP"
    exit 1
fi

# ---- Step 4: /tmp にクリーンコピー → CFBundleName修正 → Developer ID 署名 ----
#   ※ 重要：プロジェクトは ~/Documents/Claude/... 配下で同期/監視されており、
#      署名直前に com.apple.FinderInfo が付与されて codesign が必ず失敗する。
#      監視外の /tmp にクリーンコピー（ditto --noextattr）してから署名する。
#   --options runtime : 公証の必須条件（強化ランタイム）
#   --timestamp       : セキュアタイムスタンプ（公証必須）
echo "[4/8] clean copy to /tmp + codesign (Developer ID + hardened runtime) ..."
rm -rf "$APP_TMP"
ditto --norsrc --noextattr --noqtn --noacl "$APP" "$APP_TMP"
/usr/libexec/PlistBuddy -c "Set :CFBundleName '$APP_NAME'" "$APP_TMP/Contents/Info.plist" 2>/dev/null || true
codesign --force --options runtime --timestamp --sign "$SIGN_ID" "$APP_TMP"

# ---- Step 5: 署名検証 ----
echo "[5/8] verify signature ..."
codesign --verify --strict --verbose=2 "$APP_TMP"

# ---- Step 6: 公証用zip作成 → notarytool 申請（完了まで待機） ----
echo "[6/8] notarize (submit & wait) ..."
rm -f "$ZIP_NOTARIZE"
ditto -c -k --keepParent "$APP_TMP" "$ZIP_NOTARIZE"
xcrun notarytool submit "$ZIP_NOTARIZE" \
    --keychain-profile "$NOTARY_PROFILE" --wait

# ---- Step 7: staple（公証チケットを.appに添付）＋検証 ----
echo "[7/8] staple ..."
xcrun stapler staple "$APP_TMP"
xcrun stapler validate "$APP_TMP"
spctl -a -vvv "$APP_TMP" || true   # Gatekeeper評価（参考表示）

# ---- Step 8: 配布用zip作成（/tmp の staple済み .app を再パッケージして dist/ に出力） ----
echo "[8/8] package distribution zip ..."
mkdir -p "$DIST_DIR"
rm -f "$ZIP_DIST"
ditto -c -k --keepParent "$APP_TMP" "$ZIP_DIST"

echo ""
echo "=========================================="
echo "  公証完了！"
echo "  配布用zip : $ZIP_DIST"
echo ""
echo "  続けて  bash make_pkg.sh  を実行して .pkg を作ってください"
echo "  （v1.2 から配布物は .dmg ではなく .pkg です）"
echo "=========================================="
