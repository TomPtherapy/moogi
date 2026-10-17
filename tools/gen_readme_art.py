#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the README's pictures (assets/readme/) in the splash's look: the banner (the logo, "by Ruppert" 20 % smaller,
# the instrument) and the eight screens drawn by the firmware's own code (tools/readme_shots.c).
#   python3 -c "import sys; sys.path.insert(0, 'tools'); import build; build.generate()"      # build/gen
#   cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o /tmp/shots tools/readme_shots.c -lm && /tmp/shots /tmp/fa
#   python3 tools/gen_readme_art.py /tmp/fa
# Needs Pillow and the Poppins font.
import pathlib
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = pathlib.Path(__file__).resolve().parent.parent
OUT = ROOT / "assets" / "readme"
SPLASH = ROOT / "assets" / "fmini-splash.png"
FONTS = [pathlib.Path("/usr/share/fonts/truetype/google-fonts"), pathlib.Path.home() / "Library/Fonts",
         pathlib.Path("/Library/Fonts"), pathlib.Path("C:/Windows/Fonts")]
BLACK = (0, 0, 0)               # the splash's background
CREAM = (0xEC, 0xE4, 0xD0)      # fmug_app.c FA_CR
FRAME = (0x2B, 0x28, 0x25)
SHOTS = [("splash", "Splash"), ("osc", "OSC"), ("mix", "MIX"), ("filter", "Filter"),
         ("fx", "FX"), ("preset", "Preset 00"), ("arp", "ARP"), ("seq", "SEQ")]


def font(name, size):
    for d in FONTS:
        if (d / name).exists():
            return ImageFont.truetype(str(d / name), size)
    raise SystemExit(f"font {name} not found")


def spaced(draw, cx, y, text, f, fill, track):
    """text with letter spacing, centred on cx, baseline y"""
    widths = [draw.textlength(ch, font=f) for ch in text]
    x = cx - (sum(widths) + track * (len(text) - 1)) / 2
    for ch, w in zip(text, widths):
        draw.text((x, y), ch, font=f, fill=fill, anchor="ls")
        x += w + track


def banner(splash):
    """the logo on the left, the instrument on the right, on black"""
    W, H, k = 1800, 600, 0.74
    im = Image.new("RGB", (W, H), BLACK)
    logo = splash.crop((70, 115, 1185, 325))                     # "FMini-1"
    logo = logo.resize((round(logo.width * k), round(logo.height * k)), Image.LANCZOS)
    by = splash.crop((130, 352, 1122, 456))                      # "— by Ruppert —": 20 % smaller than on the splash
    by = by.resize((round(by.width * k * 0.8), round(by.height * k * 0.8)), Image.LANCZOS)
    gap = round(28 * k)
    top = (H - (logo.height + gap + by.height)) // 2 - 10
    im.paste(logo, (40, top))
    im.paste(by, (40 + logo.width // 2 - by.width // 2, top + logo.height + gap))   # both centred as on the splash
    synth = splash.crop((0, 490, 1254, 1180))
    synth = synth.resize((round(synth.width * 0.8), round(synth.height * 0.8)), Image.LANCZOS)
    mask = Image.new("L", synth.size, 255)                       # its left edge fades into the black
    md = ImageDraw.Draw(mask)
    for x in range(120):
        md.line([(x, 0), (x, synth.height)], fill=255 * x // 120)
    im.paste(synth, (W - synth.width + 10, (H - synth.height) // 2 + 10), mask)
    return im


def screens(src, cols=4, scale=2):
    """the eight screens in a grid on black, each in a dark frame over its name"""
    gap, pad, cap = 36, 30, 64
    sw = 240 * scale
    rows = (len(SHOTS) + cols - 1) // cols
    im = Image.new("RGB", (pad * 2 + cols * sw + (cols - 1) * gap, pad + rows * (sw + cap + gap) - gap + 10), BLACK)
    d = ImageDraw.Draw(im)
    f = font("Poppins-Medium.ttf", 30)
    for i, (name, label) in enumerate(SHOTS):
        s = Image.open(src / f"shot_{name}.ppm").convert("RGB").resize((sw, sw), Image.NEAREST)
        x, y = pad + (i % cols) * (sw + gap), pad + (i // cols) * (sw + cap + gap)
        d.rounded_rectangle((x - 8, y - 8, x + sw + 7, y + sw + 7), 10, fill=FRAME)
        im.paste(s, (x, y))
        spaced(d, x + sw / 2, y + sw + 50, label.upper(), f, CREAM, 4)
    return im


def main(src):
    OUT.mkdir(parents=True, exist_ok=True)
    splash = Image.open(SPLASH).convert("RGB")
    banner(splash).save(OUT / "banner.png", optimize=True)
    screens(pathlib.Path(src)).save(OUT / "screens.png", optimize=True)
    print("wrote", ", ".join(sorted(p.name for p in OUT.iterdir())))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
