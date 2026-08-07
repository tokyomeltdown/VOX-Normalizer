#!/bin/bash
# ============================================================
# VOX Normalizer — 開発ビルド (Debug)
#
# フォーマット: Standalone のみ
#   v1.2 で AAX を廃止した。理由:
#     - 配布は Standalone だけで、AAX は Ryo の Pro Tools 確認用でしかなかった
#     - ソースを公開するため、Avid の NDA 物件である AAX SDK への
#       ローカル絶対パスをリポジトリに残せない（他人がビルドできない）
#     - AGPLv3 の条項は AAX / VST2 / AUv3 / iOS には使えない
#   ※ Source 内の AAX 分岐コード（wrapperType 判定）は無害な死にコードとして
#     残してある。消す作業のリスクの方が大きいため
#
# 環境: JUCE 8.0.12 / macOS 15 / Xcode 16〜26 で動作確認済み
# xattr / CodeSign の罠を対処済み（VUMeter と同パターン）
#
# 配布ビルドは build.sh ではなく notarize.sh → make_dmg.sh を使うこと
# ============================================================

set -e  # エラーがあったら即停止

PROJUCER=~/Documents/JUCE/Projucer.app/Contents/MacOS/Projucer
JUCER_FILE="$(dirname "$0")/VUClipGainNormalizer.jucer"
PBXPROJ="$(dirname "$0")/Builds/MacOSX/VUClipGainNormalizer.xcodeproj/project.pbxproj"

echo "=========================================="
echo "  VOX Normalizer Build Script (Debug)"
echo "=========================================="

# ---- Step 1: Projucer で Xcode プロジェクト再生成 ----
echo "[1/3] Projucer --resave ..."
"$PROJUCER" --resave "$JUCER_FILE" --fix-missing-dependencies

# ---- Step 2: pbxproj パッチ ----
# 罠1対策: ditto に xattr 無効フラグを追加
#   これが無いと拡張属性が原因で CodeSign が失敗する
echo "[2/3] pbxproj patch ..."
sed -i '' 's| ditto | ditto --norsrc --noextattr --noqtn --noacl |g' "$PBXPROJ"

# ---- Step 3: 罠2対策 - コード署名を無効化してビルド ----
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

# ---- Step 3.5: Standalone .app の CFBundleName を正式名称に修正 ----
# Projucer の project name は Xcode プロジェクト名なので旧名のまま。
# アプリのメニューバー表示名だけをここで上書きする
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
