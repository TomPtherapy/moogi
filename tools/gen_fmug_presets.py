#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the 100 factory patches -> firmware/src/eng_fmug_presets.h
#   python3 tools/gen_fmug_presets.py            (writes the header; the header is checked in, the build does not run this)
#
# The bank itself is tools/fmug_bank.py: one call of add() per patch, in the order of the PRESETS list, written in
# physical units (ms, Hz, cents, semitones). This file turns them into the engine's bytes and checks the bank.
import math, os, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fmug_filter as FF  # noqa: E402   (the filter's laws: CUTOFF dial, EMPHASIS dial, keyboard zero; one source for the firmware and for this)

R = {"LO": 0, "32": 1, "16": 2, "8": 3, "4": 4, "2": 5}
W = {"TRI": 0, "SHK": 1, "SAW": 2, "SQR": 3, "PW": 4, "PN": 5}
AM = {"OFF": 0, "UP": 1, "DN": 2, "UPDN": 3, "RND": 4, "ORD": 5, "REPEAT": 6, "DNUP": 7, "UP+8": 8, "CONV": 9, "DIVG": 10,
      "PINKY": 11, "THUMB": 12, "WALK": 13, "CHORD": 14}
AR = {"1/4": 0, "1/8": 1, "1/16": 2, "1/32": 3, "8T": 4, "16T": 5, "1/2": 6, "1/1": 7}
RT = {"ROOM": 0, "HALL": 1, "PLATE": 2, "SPRING": 3, "CLOUD": 4}
DT = {"ANALOG": 0, "DIGITAL": 1, "TAPE": 2, "P-PONG": 3, "MULTI": 4, "SLAP": 5, "REVERSE": 6}
CT = {"STEREO": 0, "JUNO": 1, "MULTI": 2}
DRT = {"SOFT": 0, "TUBE": 1}
LW = {"SIN": 0, "TRI": 1, "SAW": 2, "SQR": 3, "S&H": 4}
LS = {"OFF": 0, "4BAR": 1, "2BAR": 2, "1/1": 3, "1/2": 4, "1/4": 5, "1/8": 6, "8T": 7, "1/16": 8, "16T": 9, "1/32": 10}
CATLET = {"bass": "B", "lead": "L", "pad": "P", "pluck": "U", "keys": "K", "fx": "F", "brass": "L", "atmo": "P"}


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


# ---- units -> the engine's bytes
def ms(t):
    """a contour time in ms -> 0..127 (TIME_MS = 12000 ** (v / 127))"""
    return clamp(int(round(127 * math.log(max(t, 1.0)) / math.log(12000.0))), 0, 127)


def hz(f):
    """an LFO / chorus rate in Hz -> 0..127 (0.05 * 800 ** (v / 127))"""
    return clamp(int(round(127 * math.log(f / 0.05) / math.log(800.0))), 0, 127)


def cut(f, kbd=0):
    """a cutoff -> CUTOFF 0..127. f is the filter's -3 dB corner in Hz when middle C is played (the keyboard's tracking
    included: the keyboard's 0 V is F, 19 semitones lower, so a patch that tracks has its knob that much lower)"""
    knob_hz = max(f, 1.0) / 2 ** (kbd / 127.0 * (60 - FF.NOTE0) / 12.0)
    return min(range(128), key=lambda i: abs(math.log(FF.corner_hz(i / 127.0)) - math.log(knob_hz)))


def emp(d):
    """EMPHASIS on the Minimoog's dial (0 .. 10) -> 0..127"""
    return clamp(int(round(d * 127 / 10.0)), 0, 127)


def dly(t):
    """a delay time in ms -> 0..127 (10 + v * v * 1390 / 16129)"""
    return clamp(int(round(math.sqrt(max(t - 10, 0) * 16129 / 1390))), 0, 127)


def co(octs):
    """the contour's amount in octaves of cutoff (127 = about 5) -> 0..127"""
    return clamp(int(round(octs * 127 / 5.0)), 0, 127)


def semi(n):
    """an oscillator's offset in semitones (OSC 2 / 3 FREQ: 64 = 0, 11 cents a step) -> byte"""
    return clamp(64 + int(round(n * 100 / 11.0)), 0, 127)


def det(c):
    """an offset in cents -> byte"""
    return clamp(64 + int(round(c / 11.0)), 0, 127)


def st_pit(n):
    """LFO -> pitch / contour -> pitch in semitones at full swing -> the common parameter (-64..63, 0.1875 st a step)"""
    return clamp(int(round(n / 0.1875)), -64, 63)


def o(rng, wave, level, cents=0):
    """an oscillator: range, wave, mixer level 0..127 (0 = the switch is off; a squared curve: 100 = 0.61, 127 = 0.98), offset in cents"""
    return (rng, wave, level, cents)


def osc_off():
    return ("8", "SAW", 0, 0)


def A(mode, rate, octs=1, gate=0.5, swg=0, prob=127):
    """the arpeggiator: MODE, RATE (a note value of the tempo), OCTAVES 1..4, GATE (0..1 of the step), SWING 0..100, PROB 0..127"""
    return (mode, rate, octs, gate, swg, prob)


LEVELS = {}
_lv = HERE / "fmug_levels.json"
if _lv.exists():
    import json
    LEVELS = json.loads(_lv.read_text())


class Bank:
    def __init__(self):
        self.p = []

    def add(self, name, cat, osc, noise=0, pink=False,
            cutoff=1000, emph=3.0, cntr=0, kbd=64, fenv=(0, 200, 0), env=(5, 300, 100, 150), level_trim=0,
            vib=0, mosc=False, mflt=False, mmix=0, lfo=(5.0, "SIN", 0, "OFF"), pit=0, trem=0, penv=0,
            glide=0, tune=0, poly=False, sqr=False,
            send=(0, 0, 0, 0), rv=("HALL", 70, 67), dl=("ANALOG", 375, 50), ch=("STEREO", 60, 0.6), dr=("SOFT", 64, 64),
            arp=("UP", "1/16", 1, 0.5, 0, 127), arp_on=False, oct=0, pat=0, note=""):
        assert len(name) <= 12, name
        assert len(osc) == 3, name
        sw1 = 0
        g = [0] * 18
        for i, (rng, wave, lv, cents) in enumerate(osc):
            g[i] = R[rng]
            g[3 + i] = W[wave]
            g[8 + i] = clamp(int(lv), 0, 127)
            if lv:
                sw1 |= (1, 2, 4)[i]
            if i:
                g[5 + i] = det(cents)          # FG_F2 (index 6), FG_F3 (index 7)
        if noise:
            sw1 |= 8
            g[11] = clamp(int(noise), 0, 127)
            if pink:
                sw1 |= 16
        if mosc:
            sw1 |= 32
        if mflt:
            sw1 |= 64
        g[12] = sw1
        g[13] = (1 if glide else 0) | (2 if sqr else 0)
        g[14] = clamp(64 + int(round(tune / 1.5625)), 0, 127)   # TUNE (cents): 64 = A 440, 1.56 cents a step
        g[15] = clamp(int(mmix), 0, 127)
        g[16] = clamp(int(kbd), 0, 127)
        g[17] = clamp(ms(glide) if glide else 0, 0, 127)
        e = [clamp(cut(cutoff, kbd), 0, 127), emp(emph), clamp(int(cntr), 0, 127),
             ms(fenv[0]), ms(fenv[1]), clamp(int(fenv[2]), 0, 127), clamp(int(vib), 0, 127), 0]
        a = [ms(env[0]), ms(env[1]), clamp(int(env[2]), 0, 127), ms(env[3])]
        rvT, rvD, rvTone = rv
        dlT, dlTime, dlFb = dl
        chT, chDepth, chRate = ch
        drT, drTone, drOut = dr
        fxg = [RT[rvT], clamp(int(rvD), 0, 127), clamp(int(rvTone), 0, 127), DT[dlT], dly(dlTime), clamp(int(dlFb), 0, 120),
               CT[chT], clamp(int(chDepth), 0, 127), hz(chRate), DRT[drT], clamp(int(drTone), 0, 127), clamp(int(drOut), 0, 127)]
        am, ar, ao, ag, asw, apr = arp
        arpv = [1 if arp_on else 0, AM[am], AR[ar], clamp(int(ao), 1, 4), clamp(int(round(ag * 127)), 1, 127), clamp(int(asw), 0, 100),
                clamp(int(apr), 0, 127)]
        lr, lw, lf, ls = lfo
        lfov = [hz(lr), LW[lw], ms(lf) if lf else 0, LS[ls]]
        modv = [clamp(int(pit), -64, 63), clamp(int(trem), 0, 127), clamp(int(penv), -64, 63)]
        self.p.append(dict(name=name, cat=cat, g=g, e=e, amp=a, send=[clamp(int(s), 0, 127) for s in send], poly=poly, fxg=fxg,
                           arp=arpv, lfo=lfov, mod=modv, oct=oct, pat=pat, note=note, level=clamp(int(LEVELS.get(name, 104 + level_trim)), 1, 127)))


def emit(bank, path):
    P = bank.p
    out = ["/* SPDX-License-Identifier: GPL-3.0-only",
           " * Generated by tools/gen_fmug_presets.py from tools/fmug_bank.py: the 100 factory patches of FMini-1 (do not edit by hand). */", ""]
    out.append("static const uint8_t FMUG_FACTORY[FMUG_NFACTORY][FG_SIZE] = {")
    for i, x in enumerate(P):
        out.append("    FGP(" + ", ".join(str(v) for v in x["g"]) + "),   /* %02d %s */" % (i, x["name"]))
    out.append("};\n")
    out.append("/* {CUT, EMPH, CNTR, F.ATK, F.DEC, F.SUS, MOD, SLOT}, {ATK DEC SUS REL}, MONO (legato) or POLY, the sends DIST CHORUS DELAY REVERB */")
    out.append('#define FMUG_CATS "' + "".join(CATLET[x["cat"]] for x in P) +
               '"   /* category.c: B bass, L lead, P pad, U pluck, K keys, D drum, F fx */')
    out.append("static const preset_t FMUG_PRESETS[] = {")
    for i, x in enumerate(P):
        e = list(x["e"])
        e[7] = i
        out.append('    {"%s", {%s}, {%s}, 0, %d, FX(%s), PAT(%d)},' % (
            x["name"], ", ".join(str(v) for v in e), ", ".join(str(v) for v in x["amp"]), 0 if x["poly"] else 1,
            ", ".join(str(v) for v in x["send"]), x["pat"]))
    out.append("};\n")
    out.append("/* per patch: the arpeggiator {ON MODE RATE OCT GATE SWG PROB}, the LFO {RATE WAVE FADE SYNC}, the FX {reverb TYPE DECAY TONE, delay TYPE TIME FEEDBK,")
    out.append(" * chorus TYPE DEPTH RATE, drive TYPE TONE OUTPUT}, the LEVEL, the modulation {LFO>PITCH LFO>AMP CONTOUR>PITCH} and the octave (fmug_x_t) */")
    out.append("static const fmug_x_t FMUG_X[FMUG_NFACTORY] = {")
    for i, x in enumerate(P):
        out.append("    {{%s}, {%s}, {%s}, %d, {%s}, %d},   /* %02d */" % (
            ", ".join(str(v) for v in x["arp"]), ", ".join(str(v) for v in x["lfo"]), ", ".join(str(v) for v in x["fxg"]),
            x["level"], ", ".join(str(v) for v in x["mod"]), x["oct"], i))
    out.append("};")
    pathlib.Path(path).write_text("\n".join(out) + "\n")


def check(bank):
    P = bank.p
    assert len(P) == 100, "the bank has %d patches" % len(P)
    names = [x["name"] for x in P]
    dup = sorted({n for n in names if names.count(n) > 1})
    assert not dup, "duplicate names: %s" % dup
    for i, x in enumerate(P):
        assert x["g"][8] or x["g"][9] or x["g"][10] or x["g"][11], "%s: nothing in the mixer" % x["name"]
    n_arp = sum(x["arp"][0] for x in P)
    assert os.environ.get("FMUG_PARTIAL") or 22 <= n_arp <= 28, "ARP on in %d of 100 patches (about a quarter wanted)" % n_arp
    for i in range(50, 60):
        assert P[i]["oct"] == -1, "%02d %s: a brass patch comes in with OCT-1" % (i, P[i]["name"])


if __name__ == "__main__":
    sys.path.insert(0, str(HERE))
    import fmug_bank  # noqa: E402
    bank = fmug_bank.build(Bank())
    check(bank)
    path = HERE.parent / "firmware" / "src" / "eng_fmug_presets.h"
    emit(bank, path)
    print("wrote", path, len(bank.p), "patches;", sum(x["arp"][0] for x in bank.p), "with the arpeggiator on")
