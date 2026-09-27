"""Draw the flower-offering art as SVG, render it with headless Edge/Chrome and pack it for the firmware.

The altar background is built in three layers: the church (window, light, bokeh), the portrait cropped from
art/logo_src.png pasted into the arched shrine, and the front layer (shrine frame, altar, vase, candles, counter
panel). Sprites are drawn in screen coordinates; flowers / petals / flames around a design center (FX, FY).

Outputs:
    main/art/logo.rgb565   start screen: art/logo_src.png scaled to 240x280 (big-endian RGB565)
    main/art/altar.rgb565  the altar scene background
    main/art/sprites.bin   sprites: w*h RGB565 (big-endian) followed by w*h alpha bytes, each
    main/sprites.h         sprite table, bouquet slots, layout constants

    python tools/make_art.py [--preview art/preview.png]
"""
import argparse
import math
import os
import pathlib
import random
import subprocess
import tempfile

from PIL import Image, ImageDraw

W, H = 240, 280
ROOT = pathlib.Path(__file__).resolve().parent.parent

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
]

# ------------------------------------------------------------------------------------------------
# Layout shared with the C code
# ------------------------------------------------------------------------------------------------
PILL_X, PILL_Y, PILL_W, PILL_H = 16, 8, 208, 46
ARCH_X, ARCH_Y, ARCH_W, ARCH_H = 76, 62, 88, 100          # portrait inside the shrine
FX, FY = 120, 140                                         # design center of flowers, petals, flames
VASE_CX, VASE_TOP = 120, 212
CANDLES = (34, 206)                                       # candle x; flames sit at y = FLAME_Y
FLAME_Y = 163
PLUS_CX, PLUS_CY = 200, 84
BANNER_CX, BANNER_CY, BANNER_W, BANNER_H = 120, 118, 226, 52
BUTTON_CX, BUTTON_CY = 120, 254
PORTRAIT_CROP = (170, 20, 990, 960)                        # in art/logo_src.png

# bouquet: (x, y, kind) in fill order; kinds: 0..2 lily tilted left / straight / right, 3..4 rose, 5 baby's breath
SLOTS = [
    (120, 208, 1), (104, 207, 3), (136, 207, 4), (94, 196, 0), (146, 196, 2), (113, 195, 4), (128, 194, 3),
    (101, 184, 3), (138, 184, 1), (120, 181, 0), (88, 186, 5), (153, 186, 5), (110, 172, 1), (131, 171, 2),
    (96, 173, 4), (145, 173, 3), (120, 162, 1), (84, 199, 3), (157, 199, 4), (120, 196, 5),
]
FLOWER_KINDS = 6

BLUE, BLUE_DARK, GOLD, CREAM = "#9cc4ea", "#3e6aa8", "#e2b45a", "#fff6e6"
KAI, HEI = "STKaiti", "Microsoft YaHei"


def star4(cx, cy, r, fill="#fff3c4", opacity=1.0):
    k = r * 0.3
    return (f'<path d="M{cx} {cy - r} Q{cx + k} {cy - k} {cx + r} {cy} Q{cx + k} {cy + k} {cx} {cy + r} '
            f'Q{cx - k} {cy + k} {cx - r} {cy} Q{cx - k} {cy - k} {cx} {cy - r} Z" fill="{fill}" opacity="{opacity}"/>')


# ------------------------------------------------------------------------------------------------
# Background layers
# ------------------------------------------------------------------------------------------------
def church():
    rnd = random.Random(5)
    bokeh = "".join(f'<circle cx="{x}" cy="{rnd.uniform(90, 250):.0f}" r="{rnd.uniform(6, 14):.1f}" fill="#ffe2a0" '
                    f'opacity="{rnd.uniform(0.25, 0.55):.2f}" filter="url(#blur)"/>'
                    for x in [rnd.uniform(0, 50) for _ in range(7)] + [rnd.uniform(190, 240) for _ in range(7)])
    panes = ""
    for i, x in enumerate(range(52, 188, 17)):
        for j, y in enumerate(range(40, 200, 20)):
            col = ("#7fa8dc", "#b8d4f0", "#5a86c4", "#e8d8a8")[(i + j * 3) % 4]
            panes += f'<rect x="{x}" y="{y}" width="16" height="19" fill="{col}"/>'
    rays = "".join(f'<path d="M120 40 L{120 + dx - 14} 280 L{120 + dx + 14} 280 Z" fill="#fff6d8" opacity="0.16"/>'
                   for dx in (-80, -30, 30, 80))
    return f"""
<defs>
  <linearGradient id="wall" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#f6e6cc"/><stop offset="1" stop-color="#e8cda4"/></linearGradient>
  <clipPath id="win"><path d="M50 200 V100 A70 62 0 0 1 190 100 V200 Z"/></clipPath>
  <radialGradient id="light" cx="120" cy="110" r="120" gradientUnits="userSpaceOnUse">
    <stop offset="0" stop-color="#fffdf2" stop-opacity="0.95"/><stop offset="1" stop-color="#fffdf2" stop-opacity="0"/></radialGradient>
  <filter id="blur" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="3"/></filter>
  <filter id="soft" x="-10%" y="-10%" width="120%" height="120%"><feGaussianBlur stdDeviation="1.6"/></filter>
</defs>
<rect width="{W}" height="{H}" fill="url(#wall)"/>
<g clip-path="url(#win)" opacity="0.55" filter="url(#soft)">{panes}</g>
<path d="M50 200 V100 A70 62 0 0 1 190 100 V200" fill="none" stroke="#c9a060" stroke-width="3" opacity="0.6"/>
<rect x="30" y="50" width="12" height="160" fill="#d8b890" opacity="0.5"/><rect x="198" y="50" width="12" height="160" fill="#d8b890" opacity="0.5"/>
{rays}
<circle cx="120" cy="110" r="120" fill="url(#light)"/>
{bokeh}
"""


def candle(x):
    return f"""
<rect x="{x - 12}" y="212" width="24" height="5" rx="2" fill="{GOLD}"/>
<rect x="{x - 8}" y="206" width="16" height="7" rx="2" fill="#c89440"/>
<rect x="{x - 7}" y="168" width="14" height="40" rx="2" fill="url(#wax)"/>
<path d="M{x - 7} 172 q3 6 0 10 M{x + 5} 170 q2 5 0 8" stroke="#fff" stroke-width="1.6" opacity="0.8" fill="none"/>
<line x1="{x}" y1="168" x2="{x}" y2="164" stroke="#3a2a1a" stroke-width="1"/>
"""


def front():
    leaves = "".join(f'<ellipse cx="{VASE_CX + dx}" cy="{VASE_TOP - 2 + dy}" rx="9" ry="3.6" transform="rotate({r} {VASE_CX + dx} {VASE_TOP - 2 + dy})" '
                     f'fill="{c}"/>' for dx, dy, r, c in ((-24, -4, -30, "#6a9a4a"), (24, -4, 30, "#5a8a3e"), (-30, 6, -10, "#7aaa56"),
                                                          (30, 6, 10, "#6a9a4a"), (-14, -12, -60, "#5a8a3e"), (14, -12, 60, "#7aaa56")))
    return f"""
<defs>
  <linearGradient id="gold" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fbe3a0"/><stop offset="1" stop-color="#c8903a"/></linearGradient>
  <linearGradient id="cloth" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#fffdf6"/><stop offset="1" stop-color="#eee0c8"/></linearGradient>
  <linearGradient id="wax" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#f0e2c8"/><stop offset="0.5" stop-color="#fffaf0"/><stop offset="1" stop-color="#e6d4b4"/></linearGradient>
  <linearGradient id="vase" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#d8e2ee"/><stop offset="0.45" stop-color="#ffffff"/><stop offset="1" stop-color="#c0cee0"/></linearGradient>
  <radialGradient id="halo" cx="120" cy="104" r="64" gradientUnits="userSpaceOnUse">
    <stop offset="0.6" stop-color="#fff2c0" stop-opacity="0.9"/><stop offset="1" stop-color="#fff2c0" stop-opacity="0"/></radialGradient>
</defs>
<!-- shrine frame around the portrait -->
<path d="M{ARCH_X - 7} {ARCH_Y + ARCH_H + 4} V{ARCH_Y + 44} A{ARCH_W / 2 + 7} {ARCH_W / 2 + 7} 0 0 1 {ARCH_X + ARCH_W + 7} {ARCH_Y + 44} V{ARCH_Y + ARCH_H + 4} Z
         M{ARCH_X} {ARCH_Y + ARCH_H} V{ARCH_Y + 44} A{ARCH_W / 2} {ARCH_W / 2} 0 0 1 {ARCH_X + ARCH_W} {ARCH_Y + 44} V{ARCH_Y + ARCH_H} Z"
      fill="url(#gold)" fill-rule="evenodd" stroke="#a8742a" stroke-width="0.8"/>
<path d="M{ARCH_X - 3} {ARCH_Y + 44} A{ARCH_W / 2 + 3} {ARCH_W / 2 + 3} 0 0 1 {ARCH_X + ARCH_W + 3} {ARCH_Y + 44}" fill="none" stroke="#fff4c8" stroke-width="0.8"/>
{star4(120, ARCH_Y - 10, 6, GOLD)}<circle cx="120" cy="{ARCH_Y - 10}" r="2" fill="#fff8d8"/>
<rect x="{ARCH_X - 12}" y="{ARCH_Y + ARCH_H + 2}" width="{ARCH_W + 24}" height="7" rx="2" fill="url(#gold)" stroke="#a8742a" stroke-width="0.6"/>
<!-- altar -->
<rect x="0" y="206" width="{W}" height="74" fill="url(#cloth)"/>
<path d="M0 206 H240" stroke="{GOLD}" stroke-width="2"/>
<rect x="0" y="222" width="{W}" height="6" fill="{BLUE}" opacity="0.85"/>
<path d="M0 221 H240 M0 229 H240" stroke="{GOLD}" stroke-width="1"/>
<path d="M112 240 h16 M120 234 v22" stroke="{GOLD}" stroke-width="3" stroke-linecap="round" opacity="0.0"/>
{"".join(f'<path d="M{x} 236 q6 8 12 0" fill="none" stroke="{GOLD}" stroke-width="1" opacity="0.6"/>' for x in range(4, 240, 16))}
{candle(CANDLES[0])}{candle(CANDLES[1])}
<!-- vase -->
<path d="M{VASE_CX - 18} {VASE_TOP} Q{VASE_CX - 24} {VASE_TOP + 26} {VASE_CX - 14} {VASE_TOP + 48} H{VASE_CX + 14} Q{VASE_CX + 24} {VASE_TOP + 26} {VASE_CX + 18} {VASE_TOP} Z"
      fill="url(#vase)" stroke="#9fb2cc" stroke-width="0.8"/>
<ellipse cx="{VASE_CX}" cy="{VASE_TOP}" rx="18" ry="3.5" fill="#e8eef6" stroke="#9fb2cc" stroke-width="0.8"/>
<path d="M{VASE_CX - 19} {VASE_TOP + 18} Q{VASE_CX} {VASE_TOP + 22} {VASE_CX + 19} {VASE_TOP + 18}" fill="none" stroke="{BLUE_DARK}" stroke-width="1.2" opacity="0.6"/>
{star4(VASE_CX, VASE_TOP + 31, 5, BLUE_DARK, 0.55)}
{leaves}
<!-- counter panel -->
<rect x="{PILL_X + 2}" y="{PILL_Y + 3}" width="{PILL_W}" height="{PILL_H}" rx="{PILL_H / 2}" fill="#c8a070" opacity="0.3"/>
<rect x="{PILL_X}" y="{PILL_Y}" width="{PILL_W}" height="{PILL_H}" rx="{PILL_H / 2}" fill="#fffcf4" stroke="{GOLD}" stroke-width="1.8"/>
<text x="{PILL_X + 16}" y="{PILL_Y + PILL_H / 2 + 5}" font-family="{HEI}" font-weight="bold" font-size="14" fill="{BLUE_DARK}">献花</text>
<text x="{PILL_X + PILL_W - 18}" y="{PILL_Y + PILL_H / 2 + 5}" text-anchor="end" font-family="{HEI}" font-weight="bold" font-size="14" fill="{BLUE_DARK}">朵</text>
"""


# ------------------------------------------------------------------------------------------------
# Sprites
# ------------------------------------------------------------------------------------------------
def leaves(k, spread=14):
    return "".join(f'<path d="M0 0 C{spread * 0.4:.1f} -4 {spread * 0.8:.1f} -3 {spread} 0 C{spread * 0.8:.1f} 3 {spread * 0.4:.1f} 4 0 0 Z" '
                   f'transform="rotate({a})" fill="{c}" stroke="#3e6a2a" stroke-width="0.4"/>'
                   for a, c in ((150 + 10 * k, "#5a8a3e"), (30 - 10 * k, "#6e9e4a"), (95, "#4e7e36")))


def lily(tilt, k):
    petals = ""
    for i in range(6):
        a = i * 60 + (30 if i % 2 else 0) * 0
        length = 15 if i % 2 == 0 else 13
        petals += (f'<path d="M0 0 C4.5 -5 5.5 -{length - 4} 0 -{length} C-5.5 -{length - 4} -4.5 -5 0 0 Z" '
                   f'transform="rotate({a})" fill="url(#lp{k})" stroke="#d6cfb4" stroke-width="0.5"/>'
                   f'<path d="M0 -3 L0 -{length - 3}" transform="rotate({a})" stroke="#e6ecc8" stroke-width="0.8"/>')
    stamens = "".join(f'<line x1="0" y1="0" x2="{7 * math.cos(math.radians(a)):.1f}" y2="{7 * math.sin(math.radians(a)):.1f}" stroke="#b8c878" stroke-width="0.7"/>'
                      f'<ellipse cx="{7.5 * math.cos(math.radians(a)):.1f}" cy="{7.5 * math.sin(math.radians(a)):.1f}" rx="1.4" ry="0.9" fill="#c8702a"/>'
                      for a in (-110, -80, -50, -20, 200, 230))
    return f"""
<defs><radialGradient id="lp{k}" cx="0" cy="0" r="15" gradientUnits="userSpaceOnUse">
  <stop offset="0" stop-color="#e8f0c0"/><stop offset="0.4" stop-color="#ffffff"/><stop offset="1" stop-color="#f6f2ea"/></radialGradient></defs>
<g transform="translate({FX} {FY + 3})">{leaves(k, 17)}</g>
<g transform="translate({FX} {FY}) rotate({tilt}) scale(1 0.86)">{petals}<circle r="2.6" fill="#d8e4a0"/>{stamens}</g>"""


def rose(k):
    spin = 20 if k else -15
    outer = "".join(f'<circle cx="{6.2 * math.cos(math.radians(a)):.1f}" cy="{6.2 * math.sin(math.radians(a)):.1f}" r="5.6" '
                    f'fill="url(#rp{k})" stroke="#dccab0" stroke-width="0.6"/>' for a in range(0, 360, 72))
    mid = "".join(f'<circle cx="{3 * math.cos(math.radians(a)):.1f}" cy="{3 * math.sin(math.radians(a)):.1f}" r="4" '
                  f'fill="#fffcf6" stroke="#dccab0" stroke-width="0.6"/>' for a in range(36, 396, 120))
    return f"""
<defs><radialGradient id="rp{k}" cx="0" cy="0" r="11" gradientUnits="userSpaceOnUse">
  <stop offset="0.3" stop-color="#efe2cc"/><stop offset="1" stop-color="#ffffff"/></radialGradient></defs>
<g transform="translate({FX} {FY + 3})">{leaves(k + 3, 15)}</g>
<g transform="translate({FX} {FY}) rotate({spin})">
  {outer}{mid}
  <path d="M-2 -1 A2.4 2.4 0 1 1 1.5 1.8 A1.4 1.4 0 0 1 -0.5 0.2" fill="none" stroke="#cbb594" stroke-width="0.9"/>
</g>"""

def babys_breath():
    rnd = random.Random(9)
    dots = ""
    for _ in range(9):
        x, y = rnd.uniform(-9, 9), rnd.uniform(-8, 6)
        dots += (f'<line x1="0" y1="10" x2="{x:.1f}" y2="{y:.1f}" stroke="#8aaa6a" stroke-width="0.5"/>'
                 f'<circle cx="{x:.1f}" cy="{y:.1f}" r="1.8" fill="#ffffff" stroke="#e0e0d0" stroke-width="0.4"/>')
    return f'<g transform="translate({FX} {FY})">{dots}</g>'


def petal(k):
    return f"""
<defs><linearGradient id="pt{k}" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="#f4e6dc"/></linearGradient></defs>
<g transform="translate({FX} {FY}) rotate({k * 45})"><path d="M0 -7 C4 -5 4.5 3 0 7 C-4.5 3 -4 -5 0 -7 Z" fill="url(#pt{k})" stroke="#e6d6c8" stroke-width="0.5"/></g>"""


def flame():
    return f"""
<defs><radialGradient id="fg" cx="{FX}" cy="{FY}" r="12" gradientUnits="userSpaceOnUse">
  <stop offset="0" stop-color="#ffe6a0" stop-opacity="0.8"/><stop offset="1" stop-color="#ffe6a0" stop-opacity="0"/></radialGradient></defs>
<circle cx="{FX}" cy="{FY}" r="12" fill="url(#fg)"/>
<path d="M{FX} {FY - 8} C{FX + 4} {FY - 2} {FX + 4} {FY + 4} {FX} {FY + 5} C{FX - 4} {FY + 4} {FX - 4} {FY - 2} {FX} {FY - 8} Z" fill="#ffb84a"/>
<path d="M{FX} {FY - 3} C{FX + 2} {FY} {FX + 2} {FY + 3} {FX} {FY + 4} C{FX - 2} {FY + 3} {FX - 2} {FY} {FX} {FY - 3} Z" fill="#fff8e0"/>
"""


def glow():
    return f"""
<defs><radialGradient id="sg" cx="120" cy="112" r="74" gradientUnits="userSpaceOnUse">
  <stop offset="0.55" stop-color="#fff2b8" stop-opacity="0"/><stop offset="0.72" stop-color="#fff2b8" stop-opacity="0.95"/>
  <stop offset="1" stop-color="#ffe08a" stop-opacity="0"/></radialGradient></defs>
<circle cx="120" cy="112" r="74" fill="url(#sg)"/>
"""


def plus_one():
    return (f'<text x="{PLUS_CX}" y="{PLUS_CY + 11}" text-anchor="middle" font-family="Arial Black, Segoe UI Black" font-weight="900" '
            f'font-size="28" fill="#ffc84a" stroke="#8a5a1a" stroke-width="5" stroke-linejoin="round" paint-order="stroke">+1</text>')


def banner():
    x, y, w, h = BANNER_CX - BANNER_W / 2, BANNER_CY - BANNER_H / 2, BANNER_W, BANNER_H
    tail = 10
    return f"""
<defs><linearGradient id="rb" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#c6def6"/><stop offset="1" stop-color="#86b0e0"/></linearGradient></defs>
<path d="M{x} {y + 6} L{x + tail} {y + h / 2 + 3} L{x} {y + h} H{x + w} L{x + w - tail} {y + h / 2 + 3} L{x + w} {y + 6} Z" fill="#5a86c4" opacity="0.5"/>
<rect x="{x + tail}" y="{y}" width="{w - 2 * tail}" height="{h}" rx="6" fill="url(#rb)" stroke="{GOLD}" stroke-width="2"/>
<rect x="{x + tail + 4}" y="{y + 4}" width="{w - 2 * tail - 8}" height="{h - 8}" rx="4" fill="none" stroke="#fffaf0" stroke-width="1" opacity="0.8"/>
{star4(x + tail + 12, BANNER_CY, 4, "#fffaf0")}{star4(x + w - tail - 12, BANNER_CY, 4, "#fffaf0")}
"""


def button():
    w, h = 140, 34
    x, y = BUTTON_CX - w / 2, BUTTON_CY - h / 2
    return f"""
<defs><linearGradient id="btn" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#cfe4f8"/><stop offset="1" stop-color="#7fa8dc"/></linearGradient></defs>
<rect x="{x - 3}" y="{y - 3}" width="{w + 6}" height="{h + 6}" rx="{h / 2 + 3}" fill="#fff6d8" opacity="0.5"/>
<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{h / 2}" fill="url(#btn)" stroke="{GOLD}" stroke-width="1.8"/>
<text x="{BUTTON_CX}" y="{BUTTON_CY + 6}" text-anchor="middle" font-family="{HEI}" font-weight="bold" font-size="16" letter-spacing="2"
      fill="#ffffff" stroke="{BLUE_DARK}" stroke-width="3" paint-order="stroke">点击献花</text>
{star4(x + 18, BUTTON_CY, 5, "#fffaf0")}{star4(x + w - 18, BUTTON_CY, 5, "#fffaf0")}
"""


def toast():
    return (f'<rect x="{120 - 80}" y="{128 - 20}" width="160" height="40" rx="12" fill="{BLUE_DARK}" opacity="0.88"/>'
            f'<text x="120" y="135" text-anchor="middle" font-family="{HEI}" font-weight="bold" font-size="17" fill="#fffaf0">献花数已清零</text>')


def sprites():
    out = [("button", button()), ("toast_reset", toast()), ("plus1", plus_one()), ("banner", banner()), ("glow", glow()),
           ("flame", flame()), ("sparkle_big", star4(FX, FY, 7, "#fff4c0")), ("sparkle_small", star4(FX, FY, 4, "#ffffff"))]
    out += [(f"petal_{k}", petal(k)) for k in range(4)]
    out += [("flower_0", lily(-28, 0)), ("flower_1", lily(0, 1)), ("flower_2", lily(28, 2)),
            ("flower_3", rose(0)), ("flower_4", rose(1)), ("flower_5", babys_breath())]
    return out


# ------------------------------------------------------------------------------------------------
# Rendering + packing
# ------------------------------------------------------------------------------------------------
def render(cells, browser, batch=30):
    out = []
    with tempfile.TemporaryDirectory() as tmp:
        for b in range(0, len(cells), batch):
            chunk = cells[b:b + batch]
            cols = 6
            rows = (len(chunk) + cols - 1) // cols
            divs = "".join(f'<svg style="position:absolute;left:{(i % cols) * W}px;top:{(i // cols) * H}px" width="{W}" height="{H}" '
                           f'viewBox="0 0 {W} {H}" xmlns="http://www.w3.org/2000/svg">{body}</svg>\n' for i, body in enumerate(chunk))
            page = os.path.join(tmp, f"art{b}.html")
            png = os.path.join(tmp, f"sheet{b}.png")
            with open(page, "w", encoding="utf-8") as f:
                f.write(f'<!doctype html><html><head><meta charset="utf-8"><style>html,body{{margin:0;background:transparent;'
                        f'overflow:hidden}}</style></head><body>{divs}</body></html>')
            subprocess.run([browser, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                            f"--window-size={cols * W},{rows * H}", "--default-background-color=00000000",
                            f"--user-data-dir={os.path.join(tmp, 'profile')}", f"--screenshot={png}",
                            "--virtual-time-budget=500", pathlib.Path(page).as_uri()], check=True, capture_output=True, timeout=180)
            sheet = Image.open(png).convert("RGBA")
            out += [sheet.crop(((i % cols) * W, (i // cols) * H, (i % cols + 1) * W, (i // cols + 1) * H)) for i in range(len(chunk))]
    return out


def rgb565_be(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return bytes((v >> 8, v & 0xFF))


def write_rgb565(img, path):
    raw = img.convert("RGB").tobytes()
    path.write_bytes(b"".join(rgb565_be(*raw[i:i + 3]) for i in range(0, len(raw), 3)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="save a composed preview PNG here")
    args = ap.parse_args()
    browser = next((b for b in BROWSERS if os.path.exists(b)), None)
    if not browser:
        raise SystemExit("Edge/Chrome not found")

    src = Image.open(ROOT / "art" / "logo_src.png").convert("RGB")
    logo = src.resize((W, H), Image.LANCZOS)

    spr = sprites()
    imgs = render([church(), front()] + [body for _, body in spr], browser)

    altar = Image.new("RGBA", (W, H), (246, 230, 204, 255))
    altar.alpha_composite(imgs[0])
    portrait = src.crop(PORTRAIT_CROP).resize((ARCH_W, ARCH_H), Image.LANCZOS)
    mask = Image.new("L", (ARCH_W, ARCH_H), 0)
    md = ImageDraw.Draw(mask)
    md.ellipse((0, 0, ARCH_W - 1, ARCH_W - 1), fill=255)
    md.rectangle((0, ARCH_W // 2, ARCH_W - 1, ARCH_H - 1), fill=255)
    altar.paste(portrait, (ARCH_X, ARCH_Y), mask)
    altar.alpha_composite(imgs[1])
    altar = altar.convert("RGB")

    art = ROOT / "main" / "art"
    art.mkdir(exist_ok=True)
    write_rgb565(logo, art / "logo.rgb565")
    write_rgb565(altar, art / "altar.rgb565")

    blob, table = bytearray(), []
    for (name, _), im in zip(spr, imgs[2:]):
        box = im.split()[3].getbbox()
        crop = im.crop(box)
        w, h = crop.size
        raw = crop.tobytes()
        table.append((name, len(blob), w, h, box[0], box[1]))
        blob += b"".join(rgb565_be(*raw[i:i + 3]) for i in range(0, len(raw), 4))
        blob += raw[3::4]
    (art / "sprites.bin").write_bytes(blob)

    enum = "\n".join(f"    SPR_{n.upper()}," for n, *_ in table)
    rows = "\n".join(f"    {{{off:7d}, {w:3d}, {h:3d}, {x:3d}, {y:3d}}},  // {n}" for n, off, w, h, x, y in table)
    slots = ", ".join(f"{{{x}, {y}, {k}}}" for x, y, k in SLOTS)
    hdr = f"""/* Generated by tools/make_art.py - do not edit. */
#pragma once

#include <stdint.h>

typedef struct {{
    uint32_t off;   // into sprites.bin: w*h RGB565 (big-endian), then w*h alpha bytes
    uint16_t w, h;
    int16_t  x, y;  // top-left on screen at the design position
}} sprite_t;

enum {{
{enum}
    SPR_COUNT,
}};

#define FX        {FX}   // design center of flowers, petals, flames and sparkles
#define FY        {FY}
#define PILL_X    {PILL_X}
#define PILL_Y    {PILL_Y}
#define PILL_W    {PILL_W}
#define PILL_H    {PILL_H}
#define FLAME_Y   {FLAME_Y}
#define BANNER_CX {BANNER_CX}
#define BANNER_CY {BANNER_CY}
#define FLOWER_KINDS {FLOWER_KINDS}
static const int16_t s_candle_x[2] = {{{CANDLES[0]}, {CANDLES[1]}}};

typedef struct {{
    int16_t x, y;
    uint8_t kind;  // SPR_FLOWER_0 + kind
}} slot_t;

#define SLOT_COUNT {len(SLOTS)}
static const slot_t s_slots[SLOT_COUNT] = {{{slots}}};  // bouquet, in fill order

static const sprite_t s_sprites[SPR_COUNT] = {{
{rows}
}};
"""
    (ROOT / "main" / "sprites.h").write_text(hdr, encoding="utf-8")
    print(f"logo/altar 2x{W * H * 2} bytes, sprites.bin {len(blob)} bytes, {len(table)} sprites")

    if args.preview:
        by = {n: im for (n, _), im in zip(spr, imgs[2:])}
        start = logo.convert("RGBA")
        start.alpha_composite(by["button"])
        for k, (x, y) in enumerate(((40, 60), (200, 150), (70, 200))):
            start.alpha_composite(by[f"petal_{k}"], (x - FX, y - FY))

        def scene(n_flowers, extra=()):
            im = altar.convert("RGBA")
            for x in (CANDLES):
                im.alpha_composite(by["flame"], (x - FX, FLAME_Y - FY))
            for x, y, k in SLOTS[:n_flowers]:
                im.alpha_composite(by[f"flower_{k}"], (x - FX, y - FY))
            for name, dx, dy in extra:
                im.alpha_composite(by[name], (dx, dy))
            return im

        a = scene(5, [("flower_1", 150 - FX, 250 - FY), ("sparkle_big", 104 - FX, 200 - FY), ("plus1", 0, -10)])
        b = scene(20, [("glow", 0, 0), ("banner", 0, 0)] + [(f"petal_{k % 4}", x - FX, y - FY) for k, (x, y) in
                                                           enumerate(((30, 70), (80, 150), (170, 60), (210, 190), (60, 240), (140, 30), (190, 250)))])
        sheet = Image.new("RGB", (W * 3 + 20, H), "white")
        for k, im in enumerate((start, a, b)):
            sheet.paste(im, (k * (W + 10), 0))
        sheet.save(args.preview)


if __name__ == "__main__":
    main()
