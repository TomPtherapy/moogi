#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the official site, one self-contained page (site_fmini.html: the Minimoog-styled site with the screens, the presets,
# MIDI and the Felucca installer's logic; the firmware package and every picture are embedded).
#   web/make_fmini_installer.py build/felucca.fwsc OUT.html [VERSION]
# Serve it over https or http://localhost (Web MIDI with SysEx needs a secure context), in Chrome / Edge.
import base64
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_site import HERE, strip_module, product_of  # noqa: E402


def main(pkg, out, version="1.4.2"):
    raw = Path(pkg).read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product) or b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: not a Felucca-loader package (identity {product!r})")
    html = (HERE / "site_fmini.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / f).read_text(encoding="utf-8")) for f in ("fm1pkg.js", "fm1ota.js", "fm1backup.js"))
    url = "data:application/octet-stream;base64," + base64.b64encode(raw).decode()
    meta = json.dumps({"version": version, "product": product, "pkg": url})
    hero = "data:image/jpeg;base64," + base64.b64encode((HERE.parent / "assets" / "acid-moogi-hero.jpg").read_bytes()).decode()
    imgs = {p.stem: ("data:image/" + ("webp" if p.suffix == ".webp" else "png") + ";base64,") + base64.b64encode(p.read_bytes()).decode() for p in sorted((HERE.parent / "assets" / "site").glob("*.*"))}
    names = re.findall(r'"([A-Z0-9 .\'/+&-]{2,12})"', (HERE.parent / "firmware" / "src" / "eng_fmini_presets.h").read_text())[:10]
    html = html.replace("/*IMGS*/{}", json.dumps(imgs)).replace("/*PRESETS*/[]", json.dumps(names))
    for mark in ("/*LIB*/", "/*META*/", "/*HERO*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"site_fmini.html must contain {mark} once")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*HERO*/", hero)
    html = html.replace('<span class="ver" id="ver">v1.1.0</span>', f'<span class="ver" id="ver">v{version}</span>', 1)
    Path(out).write_text(html, encoding="utf-8")
    print(f"{out}: {len(html)} B, {product}, package {len(raw)} B embedded")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
