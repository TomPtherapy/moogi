#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1 1.7.0: the factory bank, moved from the old filter (a plain four-pole ladder with a one-sample feedback delay, CUTOFF
# 30 Hz .. 16 kHz in the pole's frequency, EMPHASIS a feedback gain) to the Minimoog-style one (tools/fmug_filter.py).
# For each patch the old filter's response (CUTOFF, EMPHASIS, keyboard tracking as the patch had them, played at middle C) is
# matched by the new filter's: CUTOFF and EMPHASIS are searched over the whole knob grid for the least weighted distance between
# the two log-magnitude responses (weights: where the old patch had its energy, taking a sawtooth's 1/f spectrum through the
# old filter), a free overall gain aside (the LEVEL is calibrated afterwards, tools/preset_lab.py --calibrate).
#   git show <1.6.1>:firmware/src/eng_fmug_presets.h > old_presets.h
#   python3 tools/fmug_retune.py old_presets.h [--write]     prints the table; --write rewrites cutoff= and emph= in tools/fmug_bank.py
# The bank then states its cutoffs as the filter's -3 dB corner in Hz at middle C (keyboard tracking included) and the EMPHASIS
# on the Minimoog's 0 .. 10 dial; gen_fmug_presets.py turns them into the knob bytes (the keyboard's 0 V is F, so a patch
# with tracking has its knob that much lower).
import math, pathlib, re, sys
import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fmug_filter as F  # noqa: E402

FS = 44100.0
KMAX_OLD = 18400 / 4096.0


def old_cutoff_hz(v):
    return 30.0 * (16000.0 / 30.0) ** (v / 127.0)


def old_response(f, e0, e1):
    """the old filter at middle C: |T| (linear) on the frequencies f"""
    fc = min(old_cutoff_hz(e0), 0.45 * FS)
    g = math.tan(math.pi * fc / FS)
    G = g / (1 + g)
    k = e1 / 127.0 * KMAX_OLD * (1 - G)
    z1 = np.exp(-1j * 2 * np.pi * f / FS)
    H = G * (1 + z1) / (1 - (1 - 2 * G) * z1)
    H4 = H ** 4
    return np.abs(H4 / (1 + k * z1 * H4)) * (1 + k / 8.0)


def new_response(f, e0, e1, kbd):
    """the new filter at middle C with the patch's tracking"""
    fosc = F.fosc_hz(e0 / 127.0) * 2 ** (kbd / 127.0 * (60 - F.NOTE0) / 12.0)
    fosc = min(fosc, F.FOSC_MAX)
    g = F.g_of_fosc(fosc)
    w = np.tan(np.pi * f / FS) / g
    k = F.emph_k(e1)
    H4 = 1 / (1 + 1j * w) ** 4
    Hx = 1 / (1 + 1j * w / F.WX)
    return np.abs(H4 / (1 + k * Hx * H4)) * 10 ** (F.emph_trim_db(e1) / 20)


FGP_RE = re.compile(r"FGP\(" + r",\s*".join([r"(\d+)"] * 18) + r"\),\s*/\* (\d+) (.+?) \*/")


def parse(path):
    text = pathlib.Path(path).read_text()
    pres = re.findall(r'\{"([^"]+)", \{(\d+), (\d+), (\d+), ', text)
    out = []
    for m, p in zip(FGP_RE.finditer(text), pres):
        g = list(map(int, m.groups()[:18]))
        out.append(dict(name=p[0], e0=int(p[1]), e1=int(p[2]), e2=int(p[3]), kbd=g[16], sw1=g[12]))
    return out


def retune(x, f, wnote=60):
    ref = old_response(f, x["e0"], x["e1"])
    wgt = (ref / f) ** 2                         # a sawtooth through the old filter: the energy per log-frequency step
    wgt = wgt / wgt.sum() + 2e-4 / len(f)          # (and a little weight for the skirts)
    Lo = 20 * np.log10(ref + 1e-9)
    best = None
    kb = x["kbd"]
    # candidate knobs: the corner within +-30 steps of the old pole's 0.435 (shifted for the tracking)
    corner_old = 0.435 * old_cutoff_hz(x["e0"])
    c_target = corner_old / 2 ** (kb / 127.0 * (60 - F.NOTE0) / 12.0)
    e0c = min(range(128), key=lambda v: abs(math.log(F.corner_hz(v / 127.0)) - math.log(c_target)))
    for e0 in range(max(0, e0c - 40), min(127, e0c + 25) + 1):
        for e1 in range(0, 128):
            if F.emph_r(e1) > 0.97:                 # past the edge of singing the linear response means nothing
                break
            Ln = 20 * np.log10(new_response(f, e0, e1, kb) + 1e-9)
            d = Lo - Ln
            c = (wgt * d).sum() / wgt.sum()
            dist = (wgt * (d - c) ** 2).sum() / wgt.sum()
            if best is None or dist < best[0]:
                best = (dist, e0, e1, c)
    return best, e0c


def main():
    old = parse(sys.argv[1])
    f = np.geomspace(30, 16000, 360)
    rows = []
    for x in old:
        best, e0c = retune(x, f)
        dist, e0, e1, c = best
        corner_c4 = F.corner_hz(e0 / 127.0) * 2 ** (x["kbd"] / 127.0 * (60 - F.NOTE0) / 12.0)
        rows.append((x["name"], x["e0"], x["e1"], x["kbd"], e0, e1, corner_c4, e1 * 10.0 / 127, dist, c))
        print("%-13s old CUT %3d (%6.0f Hz pole) EMPH %3d KBD %3d  ->  CUT %3d (dial %+.1f, corner@C4 %6.0f) EMPH %3d (dial %.1f)  rms %.1f dB, gain %+.1f dB" % (
            x["name"], x["e0"], old_cutoff_hz(x["e0"]), x["e1"], x["kbd"], e0, e0 * 10.0 / 127 - 5, corner_c4, e1, e1 * 10.0 / 127, math.sqrt(dist), c))
    if "--write" in sys.argv:
        src = (HERE / "fmug_bank.py").read_text()
        end = src.index("def build(")
        cuts = [m for m in re.finditer(r"cutoff=([^,]+?),", src) if m.start() < end]
        ems = [m for m in re.finditer(r"emph=(\d+),", src) if m.start() < end]
        assert len(cuts) == len(rows) == len(ems), (len(cuts), len(rows), len(ems))
        out, last = [], 0
        spans = sorted([(m.start(1), m.end(1), "cut", i) for i, m in enumerate(cuts)] + [(m.start(1), m.end(1), "em", i) for i, m in enumerate(ems)])
        for a, b, kind, i in spans:
            r = rows[i]
            out.append(src[last:a])
            if kind == "cut":
                v = r[6]
                out.append("%d" % int(round(v, -1 if v >= 1000 else 0)) if v >= 100 else "%.0f" % v)
            else:
                out.append("%.1f" % r[7])
            last = b
        out.append(src[last:])
        (HERE / "fmug_bank.py").write_text("".join(out))
        print("rewrote tools/fmug_bank.py")


if __name__ == "__main__":
    main()
