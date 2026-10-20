#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the Minimoog-style ladder filter's laws and tables (firmware/src/eng_fmini_filter.h), and the float model
# the firmware's fixed point is checked against (tests/fmini_filter_test.c, tools/fmini_retune.py).
#   python3 tools/fmini_filter.py            (writes firmware/src/eng_fmini_filter.h; the header is checked in)
#
# What is modelled (the Minimoog Model D filter: four transistor-pair stages, a feedback "emphasis" path):
#   * four equal one-pole stages, each a differential pair (tanh in front of it), the resonance fed from the last stage
#     back to the input, plus a lag pole in the feedback path (the emphasis network and the output buffer): it moves the
#     180-degree point down to 0.6864 of the stage pole, so the filter sings at 1.578 times its -3 dB corner, as the
#     hardware does (measured by the CA-72 project's calibration against a hardware reference, GPL-3.0-or-later,
#     https://github.com/idlefoundry/ca-72, docs/calibration/README.md) and breaks into oscillation at a loop gain of
#     2.92 (instead of the ideal ladder's 4).
#   * CUTOFF knob: -5..+5 on the dial, 26 Hz .. 19 kHz corner over the knob's travel, 1.04 octave per volt, with the
#     pot's taper (the CA-72 measured the filter's singing pitch at -3 .. +2: 110.8 206.6 472.2 1073.8 2344 5188 Hz).
#   * EMPHASIS knob 0..10: the passband falls -0.8, -2.25, -4, -8, -9.5, -10.4, -11.9, -12.5 dB at 2, 2.5, 3, 4, 5, 6, 7,
#     7.5, and the peak rises 0, +0.4, +2.6, +9.3, +11.3, +14, +20, +25 dB over it (the CA-72's session E, J and N); the
#     filter starts to sing at about 8.
#   * KEYBOARD: 1 volt per octave (both switches on), 0 V on the lowest key, F (MIDI 41).
import math, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
FS = 44100.0                             # the engines' sample rate (gen_engine_tables.py)

C4 = math.sqrt(2 ** 0.25 - 1)              # corner / stage pole of four equal poles: 0.43498
RATIO = 1.578                              # the singing pitch / the -3 dB corner (the hardware's)
W0 = RATIO * C4                            # the singing pitch in stage-pole units: 0.6864
WX = W0 / math.tan(math.pi - 4 * math.atan(W0))   # the feedback lag pole, in stage-pole units: 0.7587
KC = 1.0 / abs((1 + 1j * W0) ** -4 * (1 / (1 + 1j * W0 / WX)))   # the loop gain at which it sings: 2.9186

FOSC_MAX = 19500.0                         # the singing pitch's ceiling (the stage pole's tan() stays below ~5)
NOTE0 = 41                                 # the keyboard's 0 V: the Minimoog's lowest key, F (MIDI 41)

# the knob's pot taper: knob fraction (dial -4 -2 0 +2 +4) -> track fraction
CUT_TRACK = [(0.0, 0.0), (0.1, 0.0582), (0.3, 0.2669), (0.5, 0.5), (0.7, 0.7304), (0.9, 0.9346), (1.0, 1.0)]
# the singing pitch measured at the dial's -3 .. +2 (CA-72 docs/calibration, session N, takes 23 to 30), Hz, at the track
# fractions the taper gives; the corner is that over 1.578. Between the marks straight in octaves; the ends carry on at the
# slope of the last interval and meet the hardware's 19 kHz corner at the right stop.
CUT_MARKS = [(0.1626, 110.8), (0.2669, 206.6), (0.3835, 472.2), (0.5, 1073.8), (0.6152, 2344.0), (0.7304, 5188.0)]
CUT_TOP_CORNER = 19000.0            # Hz, the corner at the right stop (the CA-72's 19.0 kHz at 0 V)

# emphasis dial (0..10) -> loop gain as a share of the singing gain
EMPH_R = [(0, 0.0), (1, 0.007), (2, 0.033), (2.5, 0.10), (3, 0.20), (4, 0.48), (5, 0.58), (6, 0.68), (7, 0.80),
          (7.5, 0.88), (8, 1.00), (8.5, 1.06), (9, 1.12), (10, 1.25)]
# the reference's passband loss (dB) at the dial marks: what the lag-pole model leaves out is given back as a trim
EMPH_LOSS = [(0, 0.0), (2, -0.8), (2.5, -2.25), (3, -4.0), (4, -8.0), (5, -9.5), (6, -10.43), (7, -11.92), (7.5, -12.5),
             (8.5, -14.67), (10, -15.5)]


def lin(points, x):
    if x <= points[0][0]:
        return points[0][1]
    for (xa, ya), (xb, yb) in zip(points, points[1:]):
        if x <= xb:
            return ya + (yb - ya) * (x - xa) / (xb - xa)
    return points[-1][1]


def track(q):
    """the knob (0..1) -> the pot's track fraction"""
    return lin(CUT_TRACK, q)


def fosc_uncapped(q):
    """the singing pitch (Hz) at knob fraction q: straight in octaves between the measured marks"""
    t = track(q)
    pts = [(tm, math.log2(f)) for tm, f in CUT_MARKS]
    lo = (0.0, pts[0][1] - pts[0][0] * (pts[1][1] - pts[0][1]) / (pts[1][0] - pts[0][0]))
    hi = (1.0, math.log2(CUT_TOP_CORNER * RATIO))
    pts = [lo] + pts + [hi]
    for (ta, ya), (tb, yb) in zip(pts, pts[1:]):
        if t <= tb:
            return 2 ** (ya + (yb - ya) * (t - ta) / (tb - ta))
    return 2 ** pts[-1][1]


def corner_hz(q):
    """the -3 dB corner at knob fraction q (no emphasis, keyboard at its 0 V), Hz"""
    return fosc_uncapped(q) / RATIO


def fosc_hz(q):
    """the pitch the filter sings at, Hz (capped)"""
    return min(fosc_uncapped(q), FOSC_MAX)


def midi16(f):
    """a frequency -> MIDI note x 16 (1/16 semitone)"""
    return 16 * (69 + 12 * math.log2(f / 440.0))


def cut_p16(v):
    """CUTOFF value 0..127 -> the singing pitch in 1/16 semitones of MIDI note"""
    return int(round(midi16(fosc_hz(v / 127.0))))


def emph_dial(v):
    return v * 10.0 / 127.0


def emph_r(v):
    return lin(EMPH_R, emph_dial(v))


def emph_k(v):
    """EMPHASIS value 0..127 -> the loop gain"""
    return KC * emph_r(v)


def response(w, k, wx=WX):
    """the closed-loop response at stage-pole units w (numpy array)"""
    import numpy as np
    h4 = 1 / (1 + 1j * w) ** 4
    hx = 1 / (1 + 1j * w / wx)
    return h4 / (1 + k * hx * h4)


def emph_loss_db(k):
    return -20 * math.log10(1 + k)


def emph_trim_db(v):
    """what the passband still lacks of the hardware's loss at EMPHASIS v (dB, <= 0, at most 1.5 dB)"""
    k = emph_k(v)
    want = lin(EMPH_LOSS, emph_dial(v))
    have = emph_loss_db(k)
    return max(-1.5, min(0.0, want - have))


def g_of_fosc(f):
    """the stage pole's g = tan(pi fp / FS) so that the loop's 180-degree point (the singing pitch f) is exact"""
    return math.tan(math.pi * f / FS) / W0


def tables():
    """(CUT[128] p16, K[128] Q12, TRIM[128] Q12, G[], GX[]) the tables of eng_fmini_filter.h"""
    cut = [cut_p16(v) for v in range(128)]
    kq = [int(round(emph_k(v) * 4096)) for v in range(128)]
    trim = [int(round(4096 * 10 ** (emph_trim_db(v) / 20))) for v in range(128)]
    m0, m1 = 16, 137                      # MIDI notes of the singing pitch: 20.6 Hz .. 22.3 kHz (the cap is below)
    G, GX = [], []
    for m in range(m0, m1 + 1):
        f = 440.0 * 2 ** ((m - 69) / 12.0)
        g = g_of_fosc(min(f, FS * 0.45))
        gx = g * WX
        G.append(int(round(g / (1 + g) * (1 << 24))))
        GX.append(int(round(gx / (1 + gx) * (1 << 24))))
    return cut, kq, trim, m0, G, GX


def graph_rows():
    """the FILT page's response drawing: 17 rows (EMPHASIS 0, 8 .. 120, 127) x 73 columns (the frequency from 6 octaves
    below the singing pitch to 3 above, an eighth of an octave a column): dB x 8, clamped to -50 .. +30 dB"""
    import numpy as np
    rows = []
    for r in range(17):
        e = min(127, r * 8)
        k, trim = emph_k(e), 10 ** (emph_trim_db(e) / 20)
        line = []
        for j in range(73):
            w = W0 * 2 ** (-6 + j / 8.0)
            h4 = 1 / (1 + 1j * w) ** 4
            hx = 1 / (1 + 1j * w / WX)
            t = abs(h4 / (1 + min(k, KC * 0.985) * hx * h4)) * trim     # (just below singing: a line has no infinite peak)
            line.append(int(round(max(-50.0, min(30.0, 20 * math.log10(t + 1e-9))) * 8)))
        rows.append(line)
    return rows


def emit(path):
    cut, kq, trim, m0, G, GX = tables()
    L = ["/* SPDX-License-Identifier: GPL-3.0-only",
         " * Generated by tools/fmini_filter.py (do not edit by hand): FMini-1's Minimoog-style ladder filter. */", "",
         "#define FMINI_KC_Q12 %d          /* the loop gain at which the ladder sings, Q12 */" % int(round(KC * 4096)),
         "#define FMINI_GM0 %d             /* the first MIDI note of FMINI_G / FMINI_GX */" % m0,
         "#define FMINI_GN %d              /* their length */" % len(G),
         "#define FMINI_NOTE0 %d           /* the keyboard's 0 V: the lowest key, F */" % NOTE0,
         "#define FMINI_FOSC_MAX16 %d      /* the singing pitch's ceiling, MIDI x 16 */" % int(round(midi16(FOSC_MAX))),
         ""]

    def arr(name, typ, vals, per=8, comment=""):
        L.append("static const %s %s[%d] = {   %s" % (typ, name, len(vals), comment))
        for i in range(0, len(vals), per):
            L.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
        L.append("};")
        L.append("")

    arr("FMINI_CUT", "int16_t", cut, 8, "/* CUTOFF 0..127 -> the singing pitch, MIDI x 16 (the dial's -5 .. +5) */")
    arr("FMINI_K", "uint16_t", kq, 8, "/* EMPHASIS 0..127 -> the feedback gain, Q12 */")
    arr("FMINI_TRIM", "uint16_t", trim, 8, "/* EMPHASIS 0..127 -> the passband's extra loss, Q12 */")
    gr = graph_rows()
    L.append("static const int16_t FMINI_GRAPH[17][73] = {   /* the FILT page's response: EMPHASIS 0, 8 .. 120, 127 x the frequency re the singing pitch (-6 .. +3 octaves, 1/8 a column), dB x 8 */")
    for line in gr:
        L.append("    {" + ", ".join(str(v) for v in line) + "},")
    L.append("};")
    L.append("")
    arr("FMINI_G", "uint32_t", G, 6, "/* stage pole G = g/(1+g), Q24, per semitone of the singing pitch */")
    arr("FMINI_GX", "uint32_t", GX, 6, "/* the feedback lag pole's G, Q24 */")
    pathlib.Path(path).write_text("\n".join(L))


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else str(HERE.parent / "firmware" / "src" / "eng_fmini_filter.h")
    emit(out)
    print("wrote", out, " KC=%.4f WX=%.4f W0=%.4f" % (KC, WX, W0))
