# VOX Normalizer

![VOX Normalizer](docs/og-image.png)

**ボーカルのフレーズごとの音量を、ドラッグ1つでそろえる。**

macOS 向けの無料・オープンソースのスタンドアロンアプリです。DAW は不要です。

[**最新版のダウンロードはこちら →**](../../releases/latest) &nbsp;·&nbsp;
[**紹介ページ（ブラウザで動くデモつき）→**](https://tokyomeltdown.github.io/VOX-Normalizer/)

[English README](README.md)

## 何をするもの？

ボーカルを録ると、必ず大きすぎるフレーズと小さすぎるフレーズが出ます。ふつうは
DAW でフレーズを1つずつ選んでクリップゲインを手で上下させますが、これが地味に
時間を食いますし、精度もばらつきます。

VOX Normalizer はその作業を代わりにやります。テイクを読み込むと、各フレーズを
同じ RMS レベルにそろえた**1本のオーディオファイル**を書き出します。DAW の作業
手順は何も変わりません。コンプの前段の「下ごしらえ」が済んだ状態から始められる
だけです。

かけるのは**フレーズ単位の静的なゲイン**で、レベルライダーのような動的処理では
ありません。フレーズ**内側**のダイナミクスは歌ったままの形で残ります。

## 主な機能

- 無音を区切りとした**フレーズ自動検出**（RMS 窓方式・ヒステリシス付き）
- 波形上で**境界を手動ドラッグ**して微調整（サンプル精度）
- **Peak Ceiling** — 破裂音で出力がクリップすることを防ぎます
- **A/B プレビュー** — 元音とノーマライズ後を切り替えて聴き比べ
- **ズーム可能な波形**（1〜16倍）。フレーズごとの RMS・ゲイン・ステータスを表示
- 切れ目のない素材向けの **Whole file モード**（カラオケなど）
- **余計な変換なし** — WAV と AIFF は、サンプルレート・ビット深度・チャンネル数を
  そのまま書き出します。MP3 を読み込んだときだけは、再エンコードで劣化させないために
  WAV で書き出します。WAV の BWF タイムスタンプも残るので、元の位置に戻せます

## 動作環境

macOS 11 以降（Apple Silicon・Intel 両対応のユニバーサルバイナリ）。DAW は不要です。

## インストール

1. [Releases](../../releases/latest) から **`VOX_Normalizer.pkg`** をダウンロード
2. ダブルクリックしてインストーラーの指示に従うだけです（`/Applications` に入ります）

インストーラーとアプリの両方に Developer ID 署名と Apple の公証を済ませてあるので、
Gatekeeper の警告は出ません。

## ライセンス

**GNU Affero General Public License v3.0** です。全文は [LICENSE](LICENSE) を
参照してください。

本プロジェクトは [JUCE](https://github.com/juce-framework/JUCE) を **AGPLv3** の
条項で利用しています（JUCE はデュアルライセンスで、AGPLv3 はその片方です）。
派生物を作る場合の注意として、AGPLv3 の条項は AAX / VST2 / AUv3 / iOS の
ターゲットには使えません。本プロジェクトが Standalone のみを配布しているのは
このためです。

---

Made by [tokyomeltdown](https://tokyomeltdown.github.io)
