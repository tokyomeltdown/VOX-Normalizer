#!/usr/bin/env python3
"""
App icon generator for VOX Normalizer (v2 / 2026-08-05)

Design direction:
  Matches LUFSBar (dark purple with a cyan bar) and caps REC (near black with a
  glowing green ring): a dark squircle, one symbol in the accent colour, and a
  restrained glow. The v1 icon (coral background with two lines of Times) stood
  out awkwardly when the three were seen side by side, so it was replaced.
What the symbol means:
  Four vertically symmetric waveform phrases.
  The phrase WIDTHS vary but the HEIGHTS are identical, which is exactly what a
  normalized take looks like. Nothing explanatory is drawn on top of it.

Run:  python3 create_icon.py
Needs: pip3 install Pillow

Output:
  Source/AppIcon_{16,32,64,128,256,512,1024}.png
  AppIcon.icns
  The .jucer refers to bigIcon=Source/AppIcon_512.png and
  smallIcon=Source/AppIcon_16.png, and the Projucer generates the .icns when it
  resaves, so run bash build.sh after replacing the PNGs.
"""
from PIL import Image, ImageDraw, ImageFilter
import math, struct, io, os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

# ---- Design constants ------------------------------------------------------
SS            = 8       # supersampling factor, for antialiasing
ART_RATIO     = 0.809   # how much of the canvas the squircle fills (same as caps REC)
CORNER_RATIO  = 0.2237  # corner radius / squircle width (close to the macOS continuous curve)

BG_TOP   = ( 44,  40,  38)   # background gradient, top (slightly warm dark)
BG_BOT   = ( 20,  18,  18)   # background gradient, bottom
CORAL_HI = (255, 166, 128)   # symbol, top (bright coral)
CORAL_LO = (210, 101,  68)   # symbol, bottom (deep coral)

GLOW_BLUR     = 0.036   # glow blur radius / squircle width
GLOW_STRENGTH = 0.50    # glow opacity

# Shape of the waveform phrases
PHRASE_WIDTHS = [1.7, 0.75, 2.7, 1.15]   # relative widths (uneven, like real phrases)
PHRASE_AMP    = 0.175   # amplitude / squircle width (shared, so the heights match)
PHRASE_SPAN   = 0.78    # total width of the waveform / squircle width
PHRASE_GAP    = 0.34    # gap between phrases, relative to the width unit
PHRASE_JAG_F  = 20.0    # how fine the jagged edge is
PHRASE_JAG_D  = 0.30    # how deep the jagged edge is
PHRASE_FLAT   = 0.13    # flatness of the top edge (smaller = only the ends fall away)


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
    """A dark, vertically graded squircle"""
    g = vertical_gradient(n, BG_TOP, BG_BOT, gamma=0.85)
    g.putalpha(squircle_mask(n))
    return g


def waveform_mask(n):
    """Vertically symmetric phrases: uneven widths, identical heights"""
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
            # Falls away only at the ends; the top stays flat, so the levels read as matched
            env = min(1.0, math.sin(math.pi * u) ** PHRASE_FLAT)
            # A waveform-like fuzz, phase-shifted per phrase
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


# ---- Main ------------------------------------------------------------------
if __name__ == "__main__":
    SRC_DIR   = os.path.join(SCRIPT_DIR, "Source")
    ICNS_PATH = os.path.join(SCRIPT_DIR, "AppIcon.icns")

    SIZES  = [16, 32, 64, 128, 256, 512, 1024]
    OTYPES = [b"icp4", b"icp5", b"icp6", b"ic07", b"ic08", b"ic09", b"ic10"]
    png_list = []

    print("=== VOX Normalizer icon generator v2 ===")
    print("Dark squircle with coral waveform phrases of matched height")

    for s in SIZES:
        img = draw_icon(s)
        img.save(os.path.join(SRC_DIR, f"AppIcon_{s}.png"))
        buf = io.BytesIO(); img.save(buf, format="PNG")
        png_list.append(buf.getvalue())
        print(f"  {s}x{s} done")

    chunks = b""
    for ot, data in zip(OTYPES, png_list):
        chunks += ot + struct.pack(">I", 8 + len(data)) + data
    total = 8 + len(chunks)
    with open(ICNS_PATH, "wb") as f:
        f.write(b"icns" + struct.pack(">I", total) + chunks)

    print(f"\nDone ({total/1024:.1f} KB)")
    print("Check:  open Source/AppIcon_256.png")
    print("Apply:  bash build.sh (the Projucer resave regenerates the .icns)")
