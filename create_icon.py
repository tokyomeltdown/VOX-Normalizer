#!/usr/bin/env python3
"""
VOX Normalizer アプリアイコン生成スクリプト（v2 / 2026-08-05）

デザイン方針:
  LUFSBar（ダーク紫 × 水色バー）・caps REC（ほぼ黒 × 緑リング）と同じ
  「ダークな squircle ＋ アクセント色の単一シンボル ＋ 控えめなグロー」に統一する。
  v1 の「コーラル背景 × Times のテキスト2行」は3作並べたとき1つだけ浮くため廃止。

シンボルの意味:
  上下対称の波形フレーズを4つ描く。
  **フレーズの幅は不揃い・高さだけ完全に揃っている** ＝ ノーマライズ後の波形そのもの。
  ガイドラインの類は描かない（説明的になりすぎるため）。

実行: python3 create_icon.py
依存: pip3 install Pillow

出力:
  Source/AppIcon_{16,32,64,128,256,512,1024}.png
  AppIcon.icns
  ※ .jucer は bigIcon=Source/AppIcon_512.png / smallIcon=Source/AppIcon_16.png を
    参照しており、Projucer が resave 時に .icns を生成する。
    そのため PNG を差し替えたあとは必ず Projucer で resave（= bash build.sh）すること。
"""
from PIL import Image, ImageDraw, ImageFilter
import math, struct, io, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ---- デザイン定数 -----------------------------------------------------------
SS            = 8       # スーパーサンプリング倍率（アンチエイリアス用）
ART_RATIO     = 0.809   # squircle がキャンバスに占める割合（caps REC と同じ）
CORNER_RATIO  = 0.2237  # 角丸半径 / squircle 幅（macOS の連続角丸に近い値）

BG_TOP   = ( 44,  40,  38)   # 背景グラデ 上（わずかに暖色寄りのダーク）
BG_BOT   = ( 20,  18,  18)   # 背景グラデ 下
CORAL_HI = (255, 166, 128)   # シンボル 上（明るいコーラル）
CORAL_LO = (210, 101,  68)   # シンボル 下（深いコーラル）

GLOW_BLUR     = 0.036   # グローのぼかし半径 / squircle 幅
GLOW_STRENGTH = 0.50    # グローの不透明度

# 波形フレーズの形（F1 案）
PHRASE_WIDTHS = [1.7, 0.75, 2.7, 1.15]   # 相対幅（不揃い＝実際の歌のフレーズ長）
PHRASE_AMP    = 0.175   # 振幅 / squircle 幅（全フレーズ共通＝高さが揃っている）
PHRASE_SPAN   = 0.78    # 波形全体の横幅 / squircle 幅
PHRASE_GAP    = 0.34    # フレーズ間の隙間（幅の単位に対する比）
PHRASE_JAG_F  = 20.0    # ギザギザの細かさ
PHRASE_JAG_D  = 0.30    # ギザギザの深さ
PHRASE_FLAT   = 0.13    # 上辺の平坦さ（小さいほど台形に近く＝端だけ落ちる）


def squircle_mask(n):
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).rounded_rectangle(
        [0, 0, n - 1, n - 1], radius=int(n * CORNER_RATIO), fill=255)
    return m


def vertical_gradient(n, top, bot, gamma=1.0):
    g = Image.new("RGBA", (n, n))
    d = ImageDraw.Draw(g)
    for y in range(n):
        t = (y / max(1, n - 1)) ** gamma
        c = tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3))
        d.line([(0, y), (n, y)], fill=c + (255,))
    return g


def base_plate(n):
    """ダークな縦グラデーションの squircle"""
    g = vertical_gradient(n, BG_TOP, BG_BOT, gamma=0.85)
    g.putalpha(squircle_mask(n))
    return g


def waveform_mask(n):
    """上下対称の波形フレーズ。幅は不揃い・高さは全フレーズ共通"""
    m = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(m)

    cy   = n * 0.5
    amp  = n * PHRASE_AMP
    span = n * PHRASE_SPAN

    total = sum(PHRASE_WIDTHS) + PHRASE_GAP * (len(PHRASE_WIDTHS) - 1)
    unit  = span / total
    x     = (n - span) / 2

    for p, wr in enumerate(PHRASE_WIDTHS):
        bw = wr * unit
        pts_top, pts_bot = [], []
        steps = 700
        for i in range(steps + 1):
            u  = i / steps
            xx = x + u * bw
            # 端だけ素早く落ち、上辺はほぼ平ら＝レベルが揃っている見え方
            env = min(1.0, math.sin(math.pi * u) ** PHRASE_FLAT)
            # 波形らしい毛羽立ち（フレーズごとに位相をずらす）
            jag = (1.0 - PHRASE_JAG_D) \
                  + PHRASE_JAG_D * abs(math.sin(u * PHRASE_JAG_F * wr + p * 2.3))
            h = amp * env * jag
            pts_top.append((xx, cy - h))
            pts_bot.append((xx, cy + h))
        d.polygon(pts_top + pts_bot[::-1], fill=255)
        x += bw + PHRASE_GAP * unit

    return m


def draw_icon(size):
    n      = size * SS
    art_n  = int(n * ART_RATIO)

    symbol = vertical_gradient(art_n, CORAL_HI, CORAL_LO)
    symbol.putalpha(waveform_mask(art_n))

    glow = symbol.filter(ImageFilter.GaussianBlur(art_n * GLOW_BLUR))
    glow.putalpha(glow.split()[3].point(lambda v: int(v * GLOW_STRENGTH)))

    plate = base_plate(art_n)
    plate = Image.alpha_composite(plate, glow)
    plate = Image.alpha_composite(plate, symbol)

    canvas = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    off    = (n - art_n) // 2
    canvas.paste(plate, (off, off), plate)
    return canvas.resize((size, size), Image.LANCZOS)


# ---- メイン -----------------------------------------------------------------
if __name__ == "__main__":
    SRC_DIR   = os.path.join(SCRIPT_DIR, "Source")
    ICNS_PATH = os.path.join(SCRIPT_DIR, "AppIcon.icns")

    SIZES  = [16, 32, 64, 128, 256, 512, 1024]
    OTYPES = [b"icp4", b"icp5", b"icp6", b"ic07", b"ic08", b"ic09", b"ic10"]
    png_list = []

    print("=== VOX Normalizer アイコン生成 v2 ===")
    print("ダーク squircle × コーラルの波形フレーズ（高さ揃い）")

    for s in SIZES:
        img = draw_icon(s)
        img.save(os.path.join(SRC_DIR, f"AppIcon_{s}.png"))
        buf = io.BytesIO(); img.save(buf, format="PNG")
        png_list.append(buf.getvalue())
        print(f"  {s}x{s} 完了")

    chunks = b""
    for ot, data in zip(OTYPES, png_list):
        chunks += ot + struct.pack(">I", 8 + len(data)) + data
    total = 8 + len(chunks)
    with open(ICNS_PATH, "wb") as f:
        f.write(b"icns" + struct.pack(">I", total) + chunks)

    print(f"\n✅ 完了 ({total/1024:.1f} KB)")
    print("確認: open Source/AppIcon_256.png")
    print("反映: bash build.sh（Projucer の resave で .icns が再生成される）")
