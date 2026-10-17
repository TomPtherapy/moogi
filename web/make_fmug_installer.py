#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: one self-contained installer page (the Felucca installer, index_pkg.html, with the firmware package embedded).
#   web/make_fmug_installer.py build/felucca.fwsc OUT.html [VERSION]
# Serve it over https or http://localhost (Web MIDI with SysEx needs a secure context), in Chrome / Edge.
import base64
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from make_site import HERE, strip_module, product_of  # noqa: E402


def main(pkg, out, version="1.0"):
    raw = Path(pkg).read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product) or b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: not a Felucca-loader package (identity {product!r})")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / f).read_text(encoding="utf-8")) for f in ("fm1pkg.js", "fm1ota.js", "fm1backup.js"))
    url = "data:application/octet-stream;base64," + base64.b64encode(raw).decode()
    meta = json.dumps({"version": "FMini-1 " + version, "product": product, "pkg": url})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    html = html.replace("<title>Felucca</title>", "<title>FMini-1 installer</title>", 1)
    html = html.replace("<h1>Felucca</h1>", "<h1>FMini-1</h1>\n  <p class=\"small\">A classic 3-oscillator, ladder-filter analog synth firmware for the M-VAVE FM-1, "
                        "based on Felucca (GPL-3.0).</p>", 1)
    html = html.replace("`Felucca ${meta.version}`", "`${meta.version}`")
    html = html.replace('<p><a href="../editor/" style="color:inherit" data-t="editor"></a></p>', "")
    html = html.replace("../../firmware/LICENSING.md", "https://github.com/hugelton/Felucca/blob/main/LICENSING.md")
    html = html.replace("../../firmware/LICENSES/", "https://github.com/hugelton/Felucca/tree/main/LICENSES/")
    html = html.replace("../../firmware/LICENSE", "https://github.com/hugelton/Felucca/blob/main/LICENSE")
    Path(out).write_text(html, encoding="utf-8")
    print(f"{out}: {len(html)} B, {product}, package {len(raw)} B embedded")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(*sys.argv[1:])
