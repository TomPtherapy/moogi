#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the README's pictures (assets/readme/), in the splash's look: black, the orange F, cream lettering, the walnut
# cheeks and the panel's labels.
#   cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o /tmp/fa/t tests/fmug_app_test.c -lm && /tmp/fa/t /tmp/fa
#   python3 tools/gen_readme_art.py /tmp/fa
# (the screens are the real drawing code's, from tests/fmug_app_test.c; needs Pillow and the Poppins font)
import pathlib
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "readme"
SPLASH = ROOT / "assets" / "fmini-splash.png"
FONTS = [pathlib.Path("/usr/share/fonts/truetype/google-fonts"), pathlib.Path.home() / "Library/Fonts",
         pathlib.Path("/Library/Fonts"), pathlib.Path("C:/Windows/Fonts")]

BLACK = (0, 0, 0)
PANEL_TOP, PANEL_BOT = (0x24, 0x21, 0x1E), (0x14, 0x12, 0x11)  # the screen's panel gradient (fmug_app.c fa_bg)
ORANGE = (0xE0, 0x75, 0x2B)  # FA_OR
CREAM = (0xEC, 0xE4, 0xD0)  # FA_CR
MUTED = (0x9A, 0x8F, 0x7E)  # FA_MU
TEAL = (0x2A, 0x9D, 0xA3)  # the splash's rocker switches
RED = (0xD6, 0x38, 0x2B)


def font(name, size):
    for d in FONTS:
        p = d / name
        if p.exists():
            return ImageFont.truetype(str(p), size)
    raise SystemExit(f"font {name} not found")


def vgrad(w, h, a, b):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))
        for x in range(w):
            px[x, y] = c
    return im


def spaced(draw, xy, text, f, fill, track, anchor_center=True):
    """text with letter spacing; xy is its centre (or left) at the cap line"""
    widths = [draw.textlength(ch, font=f) for ch in text]
    total = sum(widths) + track * (len(text) - 1)
    x = xy[0] - total / 2 if anchor_center else xy[0]
    for ch, w in zip(text, widths):
        draw.text((x, xy[1]), ch, font=f, fill=fill, anchor="ls")
        x += w + track
    return total


def banner(splash):
    """the logo on the left, the instrument on the right, on black"""
    W, H = 1800, 600
    im = Image.new("RGB", (W, H), BLACK)
    logo = splash.crop((70, 110, 1185, 450))                     # "FMini-1" and "— by Ruppert —"
    logo = logo.resize((int(logo.width * 0.74), int(logo.height * 0.74)), Image.LANCZOS)
    im.paste(logo, (40, (H - logo.height) // 2 - 10))
    synth = splash.crop((0, 490, 1254, 1180))
    synth = synth.resize((int(synth.width * 0.80), int(synth.height * 0.80)), Image.LANCZOS)
    mask = Image.new("L", synth.size, 255)                       # fade its left edge into the black
    md = ImageDraw.Draw(mask)
    for x in range(120):
        md.line([(x, 0), (x, synth.height)], fill=int(255 * x / 120))
    im.paste(synth, (W - synth.width + 10, (H - synth.height) // 2 + 10), mask)
    return im


def header(label, sub=None):
    """a strip of the panel with its label, like OSCILLATOR BANK on the splash: a rocker switch, the label, a rule"""
    W, H = 1800, 132
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    panel = vgrad(W - 8, H - 8, PANEL_TOP, PANEL_BOT)
    m = Image.new("L", panel.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, panel.width - 1, panel.height - 1), 18, fill=255)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((6, 8, W - 2, H - 1), 20, fill=(0, 0, 0, 110))
    im.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(4)))
    im.paste(panel, (4, 2), m)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((4, 2, W - 5, H - 7), 18, outline=(0x3A, 0x35, 0x30), width=2)
    # the rocker switch: orange body, a lighter top half
    x0, y0 = 46, (H - 6) // 2 - 30
    d.rounded_rectangle((x0, y0, x0 + 34, y0 + 60), 6, fill=(0x8E, 0x3F, 0x14))
    d.rounded_rectangle((x0, y0, x0 + 34, y0 + 34), 6, fill=ORANGE)
    d.rectangle((x0 + 4, y0 + 30, x0 + 30, y0 + 33), fill=(0xF3, 0x9A, 0x55))
    f = font("Poppins-Medium.ttf", 54)
    base = (H - 6) // 2 + 19
    w = spaced(d, (118, base), label.upper(), f, CREAM, 6, anchor_center=False)
    x = 118 + w + 36
    if sub:
        fs = font("Poppins-Regular.ttf", 30)
        d.text((x, base), sub.upper(), font=fs, fill=MUTED, anchor="ls")
        x += d.textlength(sub.upper(), font=fs) + 36
    d.line((x, base - 19, W - 60, base - 19), fill=(0x5A, 0x52, 0x48), width=3)
    d.ellipse((W - 52, base - 30, W - 30, base - 8), fill=RED, outline=(0x5A, 0x16, 0x10), width=2)
    d.ellipse((W - 47, base - 26, W - 40, base - 19), fill=(0xFF, 0xA0, 0x90))
    return im


def screens(src, items, scale=2):
    """screens of the real firmware side by side on black, each under its tab name"""
    gap, pad, cap = 36, 30, 64
    sw = 240 * scale
    W = pad * 2 + len(items) * sw + (len(items) - 1) * gap
    H = pad + sw + cap + 10
    im = Image.new("RGB", (W, H), BLACK)
    d = ImageDraw.Draw(im)
    f = font("Poppins-Medium.ttf", 30)
    for i, (n, name) in enumerate(items):
        s = Image.open(src / f"fa_{n}.ppm").convert("RGB").resize((sw, sw), Image.NEAREST)
        x = pad + i * (sw + gap)
        d.rounded_rectangle((x - 8, pad - 8, x + sw + 7, pad + sw + 7), 10, fill=(0x2B, 0x28, 0x25))
        im.paste(s, (x, pad))
        spaced(d, (x + sw / 2, pad + sw + 50), name.upper(), f, CREAM, 4)
    return im


def wood_rule(splash):
    """a thin walnut strip, the instrument's front rail"""
    rail = splash.crop((120, 790, 960, 830)).resize((1800, 26), Image.LANCZOS)
    im = Image.new("RGBA", (1800, 26), (0, 0, 0, 0))
    m = Image.new("L", rail.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, rail.width - 1, rail.height - 1), 8, fill=255)
    im.paste(rail, (0, 0), m)
    return im


def main(src):
    src = pathlib.Path(src)
    OUT.mkdir(parents=True, exist_ok=True)
    splash = Image.open(SPLASH).convert("RGB")
    banner(splash).save(OUT / "banner.png", optimize=True)
    wood_rule(splash).save(OUT / "rail.png", optimize=True)
    screens(src, [(0, "Controllers"), (1, "Oscillators"), (3, "Filter"), (6, "Effects")]).save(OUT / "screens-pages.png", optimize=True)
    screens(src, [(16, "Arpeggiator"), (18, "Sequencer"), (13, "Global"), (17, "Presets")]).save(OUT / "screens-popups.png", optimize=True)
    for slug, label, sub in [("sobre", "O instrumento", None), ("som", "Motor de som", "Oscillator bank · Mixer · Modifiers"),
                             ("controles", "Controles", "Painel do FM-1"), ("patches", "Patches", "100 de fábrica + 32 seus"),
                             ("instalar", "Instalação", None), ("compilar", "Compilar", None),
                             ("creditos", "Créditos e licença", None)]:
        header(label, sub).save(OUT / f"h-{slug}.png", optimize=True)
    print("wrote", ", ".join(sorted(p.name for p in OUT.iterdir())))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
