#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the 100 factory patches (all monophonic, like the instrument they are after) -> firmware/src/eng_fmug_presets.h
#   python3 tools/gen_fmug_presets.py            (writes the header; the header is checked in, the build does not run this)
import pathlib, sys

R = {"LO": 0, "32": 1, "16": 2, "8": 3, "4": 4, "2": 5}
W = {"TRI": 0, "SHK": 1, "SAW": 2, "SQR": 3, "PW": 4, "PN": 5}
FS = {"O1": 1, "O2": 2, "O3": 4, "NZ": 8, "PINK": 16, "MOSC": 32, "MFLT": 64}
P = []
NOMOD = {"KICK", "TOM", "SNARE", "HI HAT", "CLAP", "NOISE FX", "WIND", "JET", "ZAP", "LASER", "THUNDER", "SIREN", "ALARM"}


def p(name, o1, o2, o3, lv, cut, emph, cntr, fe, amp, mod=0, flags="", gl=0, kbd=64, fx=(0, 0, 0, 0), tune=64, mm=0, pat=2):
    """o: (range, wave[, detune]) detune 64 = 0; lv: levels of osc 1..3 and noise (0 = off); fe: filter contour A D S;
    amp: loudness contour A D S R; flags: MOSC MFLT PINK GL (glide) SQR (the LFO as a square)"""
    assert len(name) <= 12, name
    od = [(o + (64,))[:3] if len(o) == 2 else o for o in (o1, o2, o3)]
    sw1 = sum(FS[f] for f in flags.split() if f in FS)
    if name not in NOMOD:                  # the mod wheel (CC1) reaches the pitch (vibrato) of every melodic patch
        sw1 |= FS["MOSC"]
    for i, l in enumerate(lv[:3]):
        if l:
            sw1 |= (1, 2, 4)[i]
    if lv[3]:
        sw1 |= FS["NZ"]
    sw2 = (1 if "GL" in flags.split() else 0) | (2 if "SQR" in flags.split() else 0)
    P.append(dict(name=name, g=[R[od[0][0]], R[od[1][0]], R[od[2][0]], W[od[0][1]], W[od[1][1]], W[od[2][1]], od[1][2], od[2][2],
                  lv[0], lv[1], lv[2], lv[3], sw1, sw2, tune, mm, kbd, gl],
                  e=[cut, emph, cntr, fe[0], fe[1], fe[2], mod, len(P)], amp=amp, fx=fx, pat=pat))


# ---- bass (25)
p("MOOG BASS", ("16", "SAW"), ("16", "SAW", 66), ("32", "SQR"), (100, 100, 90, 0), 50, 55, 90, (0, 56, 20), (0, 70, 110, 30), pat=2)
p("SUB BASS", ("32", "TRI"), ("16", "TRI", 64), ("32", "SQR"), (110, 60, 40, 0), 38, 20, 40, (0, 50, 30), (0, 60, 115, 25))
p("FAT BASS", ("16", "SAW"), ("16", "SAW", 62), ("32", "SAW", 66), (100, 100, 100, 0), 46, 40, 85, (0, 60, 25), (0, 70, 112, 30), fx=(8, 0, 0, 6))
p("FUNK BASS", ("16", "SQR"), ("16", "SAW", 65), ("32", "SQR"), (100, 90, 80, 0), 42, 60, 105, (0, 38, 5), (0, 45, 40, 15), pat=3)
p("RESO BASS", ("16", "SAW"), ("16", "SAW", 65), ("32", "SQR"), (100, 90, 90, 0), 36, 95, 90, (0, 52, 15), (0, 70, 105, 25))
p("ACID BASS", ("16", "SAW"), ("16", "SQR", 64), ("32", "SQR"), (110, 0, 80, 0), 30, 100, 110, (0, 42, 0), (0, 50, 100, 12), fx=(25, 0, 0, 0), pat=3)
p("PULSE BASS", ("16", "PW"), ("16", "PN", 65), ("32", "SQR"), (100, 80, 70, 0), 48, 45, 80, (0, 48, 20), (0, 60, 105, 20))
p("SQUARE BASS", ("16", "SQR"), ("16", "SQR", 66), ("32", "SQR"), (100, 90, 70, 0), 44, 35, 70, (0, 55, 30), (0, 65, 110, 25))
p("GROWL BASS", ("16", "SAW"), ("16", "SHK", 66), ("32", "SAW", 62), (100, 100, 100, 0), 40, 85, 100, (0, 70, 30), (0, 80, 110, 30), mod=40, flags="MFLT", fx=(30, 0, 0, 0))
p("DEEP BASS", ("32", "SAW"), ("32", "SQR", 65), ("16", "TRI"), (100, 90, 60, 0), 34, 30, 60, (0, 60, 40), (0, 80, 115, 35))
p("THUMP BASS", ("32", "TRI"), ("32", "TRI", 64), ("16", "SQR"), (120, 80, 30, 0), 30, 25, 95, (0, 28, 0), (0, 40, 70, 10))
p("SLAP BASS", ("16", "SQR"), ("16", "SAW", 66), ("8", "PN"), (100, 90, 60, 0), 40, 55, 115, (0, 30, 0), (0, 40, 30, 10), kbd=80, pat=3)
p("STAB BASS", ("16", "SAW"), ("16", "SAW", 63), ("16", "SQR"), (100, 100, 90, 0), 45, 50, 110, (0, 32, 0), (0, 36, 20, 8), pat=4)
p("SEQ BASS", ("16", "SAW"), ("16", "PW", 65), ("32", "SQR"), (100, 90, 80, 0), 40, 70, 100, (0, 40, 10), (0, 50, 100, 15), fx=(0, 0, 25, 8), pat=4)
p("DRIVE BASS", ("16", "SAW"), ("16", "SAW", 66), ("32", "SAW"), (127, 127, 127, 0), 55, 50, 70, (0, 58, 30), (0, 70, 115, 30), fx=(60, 0, 0, 0))
p("HOLLOW BASS", ("16", "PN"), ("16", "PN", 66), ("32", "SQR"), (100, 90, 80, 0), 52, 70, 50, (0, 60, 40), (0, 75, 110, 30))
p("RUBBER BASS", ("16", "SAW"), ("16", "TRI", 66), ("32", "SQR"), (100, 90, 90, 0), 33, 80, 120, (0, 46, 0), (0, 55, 95, 20))
p("SOFT BASS", ("16", "TRI"), ("16", "SAW", 64), ("32", "SQR"), (100, 60, 70, 0), 42, 20, 55, (10, 60, 40), (8, 70, 110, 40))
p("BUZZ BASS", ("16", "SHK"), ("16", "SAW", 67), ("32", "SAW"), (100, 100, 90, 0), 58, 60, 70, (0, 65, 40), (0, 75, 112, 30), fx=(20, 0, 0, 0))
p("OCTAVE BASS", ("16", "SAW"), ("8", "SAW", 64), ("32", "SQR"), (100, 70, 90, 0), 46, 45, 80, (0, 56, 25), (0, 66, 108, 25), pat=4)
p("PEDAL BASS", ("32", "SAW"), ("32", "SAW", 63), ("16", "SQR"), (100, 100, 70, 0), 40, 35, 55, (6, 70, 60), (4, 90, 118, 50))
p("WOBBLE BASS", ("16", "SAW"), ("16", "SAW", 66), ("32", "SQR"), (100, 100, 80, 0), 50, 70, 20, (0, 60, 60), (0, 80, 110, 35), mod=80, flags="MFLT", pat=2)
p("PHAT BASS", ("16", "SAW"), ("16", "SAW", 60), ("16", "SAW", 68), (100, 100, 100, 0), 48, 50, 80, (0, 62, 30), (0, 75, 112, 30), fx=(10, 25, 0, 6))
p("PLUCK BASS", ("16", "PN"), ("16", "SQR", 65), ("32", "TRI"), (100, 80, 70, 0), 42, 60, 110, (0, 36, 0), (0, 44, 25, 12), pat=3)
# ---- lead (25)
p("MOOG LEAD", ("8", "SQR"), ("8", "SAW", 67), ("4", "TRI"), (100, 90, 40, 0), 78, 40, 50, (0, 70, 70), (0, 80, 110, 35), mod=20, flags="GL", gl=20, fx=(0, 10, 35, 30), pat=4)
p("FAT LEAD", ("8", "SAW"), ("8", "SAW", 62), ("16", "SAW", 66), (100, 100, 90, 0), 70, 45, 60, (0, 70, 60), (0, 80, 112, 40), flags="GL", gl=18, fx=(10, 20, 30, 25), pat=4)
p("SOLO LEAD", ("8", "SAW"), ("8", "SQR", 66), ("16", "SAW"), (100, 90, 60, 0), 74, 50, 55, (0, 66, 60), (0, 75, 112, 40), flags="GL", gl=25, fx=(0, 10, 40, 30), pat=4)
p("SINGING", ("8", "TRI"), ("8", "SAW", 65), ("4", "TRI", 64), (100, 80, 60, 0), 66, 55, 50, (10, 80, 80), (6, 85, 115, 45), mod=30, flags="MOSC GL", gl=22, fx=(0, 15, 30, 35), pat=5)
p("PULSE LEAD", ("8", "PW"), ("8", "PN", 66), ("16", "SQR"), (100, 90, 60, 0), 72, 45, 55, (0, 68, 60), (0, 78, 110, 38), flags="GL", gl=15, fx=(0, 10, 30, 25), pat=4)
p("SQUARE LEAD", ("8", "SQR"), ("8", "SQR", 66), ("4", "SQR"), (100, 90, 40, 0), 76, 35, 50, (0, 70, 70), (0, 80, 110, 35), flags="GL", gl=18, fx=(0, 5, 30, 25))
p("SAW LEAD", ("8", "SAW"), ("8", "SAW", 65), ("4", "SAW"), (100, 90, 50, 0), 80, 40, 45, (0, 72, 70), (0, 82, 112, 35), flags="GL", gl=20, fx=(15, 10, 30, 25), pat=4)
p("SINE LEAD", ("8", "TRI"), ("8", "TRI", 66), ("16", "TRI", 63), (127, 127, 110, 0), 100, 12, 30, (0, 70, 90), (0, 85, 118, 45), mod=18, flags="MOSC GL", gl=38, kbd=100, fx=(22, 36, 34, 14), pat=4)
p("GLIDE LEAD", ("8", "SAW"), ("8", "SQR", 66), ("16", "SQR"), (100, 90, 70, 0), 68, 55, 55, (0, 66, 60), (0, 78, 112, 40), flags="GL", gl=60, fx=(0, 10, 35, 30), pat=4)
p("RESO LEAD", ("8", "SAW"), ("8", "SAW", 66), ("16", "SQR"), (100, 90, 60, 0), 60, 100, 70, (0, 64, 40), (0, 76, 110, 38), flags="GL", gl=18, fx=(10, 10, 30, 25), pat=4)
p("WARM LEAD", ("8", "TRI"), ("8", "SAW", 65), ("16", "TRI"), (100, 80, 70, 0), 62, 40, 40, (8, 78, 80), (6, 85, 115, 45), mod=15, flags="MOSC GL", gl=22, fx=(0, 15, 35, 35), pat=5)
p("BITE LEAD", ("8", "SAW"), ("8", "SHK", 66), ("4", "SAW"), (100, 100, 70, 0), 56, 70, 95, (0, 40, 30), (0, 55, 105, 25), flags="GL", gl=12, fx=(25, 5, 25, 20), pat=4)
p("FLUTE LEAD", ("8", "TRI"), ("4", "TRI", 64), ("8", "SQR"), (110, 50, 20, 20), 55, 20, 35, (20, 70, 70), (18, 70, 105, 35), mod=20, flags="MOSC", fx=(0, 10, 30, 40), pat=5, mm=40)
p("REED LEAD", ("8", "PW"), ("8", "SQR", 65), ("4", "PN"), (100, 90, 50, 0), 58, 40, 50, (6, 72, 60), (4, 80, 108, 35), mod=18, flags="MOSC", fx=(0, 10, 25, 30), pat=5)
p("VOX LEAD", ("8", "PN"), ("8", "SAW", 65), ("4", "PW"), (100, 90, 50, 0), 52, 75, 55, (8, 75, 60), (6, 82, 110, 40), mod=14, flags="MOSC GL", gl=20, fx=(0, 15, 30, 35), pat=5)
p("ROCK LEAD", ("8", "SAW"), ("8", "SAW", 63), ("16", "SAW", 65), (110, 110, 80, 0), 74, 55, 50, (0, 70, 70), (0, 80, 115, 40), flags="GL", gl=20, fx=(55, 5, 30, 20), pat=4)
p("PROG LEAD", ("8", "SQR"), ("8", "SAW", 66), ("16", "SQR", 66), (100, 100, 70, 0), 70, 50, 60, (0, 70, 70), (0, 80, 112, 40), mod=10, flags="GL", gl=30, fx=(20, 10, 40, 30), pat=4)
p("SCREAM LEAD", ("8", "SAW"), ("8", "SAW", 65), ("4", "SQR"), (100, 100, 80, 0), 66, 90, 80, (0, 70, 50), (0, 80, 112, 40), mod=25, flags="GL", gl=25, fx=(50, 5, 25, 20), pat=4)
p("OCTAVE LEAD", ("8", "SAW"), ("4", "SAW", 64), ("8", "SQR", 65), (100, 70, 60, 0), 72, 40, 50, (0, 70, 70), (0, 80, 110, 35), flags="GL", gl=18, fx=(0, 10, 30, 25), pat=4)
p("DUAL LEAD", ("8", "SQR"), ("8", "SQR", 71), ("16", "SAW"), (100, 100, 50, 0), 68, 45, 50, (0, 70, 70), (0, 80, 110, 35), flags="GL", gl=18, fx=(0, 15, 35, 25), pat=4)
p("SOFT LEAD", ("8", "TRI"), ("8", "SAW", 65), ("4", "TRI"), (100, 60, 40, 0), 56, 30, 30, (14, 80, 80), (10, 85, 110, 45), flags="GL", gl=25, fx=(0, 10, 35, 40), pat=5)
p("WHISTLE", ("4", "TRI"), ("4", "TRI", 64), ("2", "TRI"), (100, 50, 20, 10), 50, 15, 20, (12, 80, 90), (10, 80, 105, 40), mod=25, flags="MOSC", fx=(0, 5, 30, 45), pat=5)
p("CRY LEAD", ("8", "SHK"), ("8", "SAW", 66), ("4", "PN"), (100, 90, 40, 0), 60, 80, 60, (6, 75, 60), (4, 82, 110, 40), mod=22, flags="MOSC GL", gl=35, fx=(15, 10, 30, 30), pat=5)
p("SWEEP LEAD", ("8", "SAW"), ("8", "SAW", 65), ("16", "SQR"), (100, 90, 70, 0), 40, 80, 100, (30, 90, 40), (6, 85, 112, 45), flags="GL", gl=18, fx=(10, 15, 35, 35), pat=5)
p("THIN LEAD", ("8", "PN"), ("8", "PN", 66), ("4", "PN"), (100, 90, 50, 0), 82, 30, 45, (0, 70, 70), (0, 80, 108, 35), flags="GL", gl=18, fx=(0, 10, 30, 25), pat=4)
p("BIG LEAD", ("8", "SAW"), ("8", "SAW", 61), ("16", "SAW", 67), (110, 110, 110, 0), 66, 50, 55, (0, 72, 70), (0, 84, 115, 45), flags="GL", gl=22, fx=(25, 25, 40, 40), pat=4)
# ---- brass, reeds, strings, pads (20)
p("BRASS", ("8", "SAW"), ("8", "SAW", 65), ("4", "SAW", 63), (90, 90, 60, 0), 50, 25, 60, (60, 80, 70), (40, 90, 110, 40), kbd=40, fx=(0, 20, 20, 35), pat=5)
p("SOFT BRASS", ("8", "SAW"), ("8", "TRI", 65), ("4", "SAW", 63), (90, 80, 50, 0), 46, 20, 50, (70, 85, 70), (50, 90, 108, 45), kbd=40, fx=(0, 20, 25, 40), pat=5)
p("BRASS STAB", ("8", "SAW"), ("8", "SAW", 66), ("16", "SAW", 63), (100, 100, 70, 0), 44, 30, 85, (14, 55, 20), (10, 60, 90, 25), kbd=45, fx=(5, 15, 25, 30), pat=4)
p("HORN", ("8", "SAW"), ("8", "SQR", 65), ("16", "SAW", 63), (100, 80, 60, 0), 40, 20, 55, (50, 80, 70), (35, 85, 110, 40), kbd=40, fx=(0, 15, 25, 40), pat=5)
p("TRUMPET", ("8", "PW"), ("8", "SAW", 65), ("4", "PN", 64), (100, 90, 40, 0), 44, 35, 70, (26, 70, 60), (20, 80, 108, 35), mod=12, flags="MOSC", fx=(0, 10, 25, 30), pat=5)
p("SYN BRASS", ("8", "SAW"), ("8", "SAW", 61), ("4", "SAW", 67), (100, 100, 80, 0), 54, 30, 65, (40, 80, 70), (30, 88, 112, 45), kbd=40, fx=(0, 30, 30, 40), pat=5)
p("OBOE", ("8", "PN"), ("8", "PW", 65), ("4", "PN"), (100, 80, 40, 0), 46, 60, 50, (16, 75, 70), (12, 82, 105, 35), mod=16, flags="MOSC", fx=(0, 8, 25, 35), pat=5)
p("CLARINET", ("8", "SQR"), ("8", "SQR", 64), ("4", "TRI"), (100, 70, 30, 0), 42, 20, 40, (14, 75, 70), (10, 82, 105, 35), mod=14, flags="MOSC", fx=(0, 8, 25, 35), pat=5)
p("SAX", ("8", "SHK"), ("8", "PW", 65), ("4", "PN"), (100, 80, 40, 0), 48, 55, 55, (10, 75, 70), (6, 82, 108, 35), mod=14, flags="MOSC", fx=(5, 8, 25, 30), pat=5)
p("STRINGS", ("8", "SAW"), ("8", "SAW", 61), ("4", "SAW", 67), (80, 80, 50, 0), 56, 15, 30, (70, 90, 100), (70, 95, 115, 80), mod=10, flags="MOSC", kbd=30, fx=(0, 50, 25, 50), pat=5)
p("SLOW STRING", ("8", "SAW"), ("8", "SAW", 60), ("16", "SAW", 68), (80, 80, 60, 0), 50, 15, 25, (90, 100, 100), (95, 100, 118, 95), mod=12, flags="MOSC", kbd=30, fx=(0, 60, 30, 60), pat=5)
p("SOFT PAD", ("8", "SAW"), ("8", "PW", 66), ("4", "TRI"), (80, 80, 60, 0), 60, 25, 40, (90, 100, 90), (90, 100, 120, 90), mod=15, flags="MOSC", kbd=30, fx=(0, 50, 30, 60), pat=5)
p("MOOG PAD", ("8", "SAW"), ("8", "SQR", 65), ("16", "SAW", 63), (90, 80, 70, 0), 48, 30, 45, (80, 100, 80), (80, 100, 118, 90), mod=10, flags="MFLT", kbd=30, fx=(0, 40, 35, 55), pat=5)
p("WARM PAD", ("8", "TRI"), ("8", "SAW", 63), ("16", "TRI", 66), (90, 70, 70, 0), 44, 20, 30, (70, 100, 90), (75, 100, 118, 90), kbd=30, fx=(0, 45, 30, 60), pat=5)
p("DARK PAD", ("16", "SAW"), ("16", "SAW", 61), ("8", "SQR", 67), (90, 90, 60, 0), 36, 25, 35, (90, 100, 80), (90, 100, 118, 95), mod=8, flags="MFLT", kbd=25, fx=(0, 40, 35, 65), pat=5)
p("SWEEP PAD", ("8", "SAW"), ("8", "SAW", 62), ("4", "SAW", 66), (90, 90, 60, 0), 30, 55, 90, (100, 100, 60), (95, 100, 118, 95), kbd=30, fx=(0, 40, 40, 65), pat=5)
p("CHOIR", ("8", "PN"), ("8", "PW", 65), ("4", "PN", 63), (90, 80, 50, 0), 44, 70, 30, (60, 90, 90), (65, 95, 112, 80), mod=12, flags="MOSC", kbd=30, fx=(0, 40, 30, 60), pat=5)
p("DRONE", ("16", "SAW"), ("16", "SAW", 64), ("32", "SAW"), (100, 100, 100, 0), 52, 40, 20, (0, 100, 110), (20, 100, 127, 70), mod=12, flags="MFLT", fx=(10, 20, 25, 45), pat=5)
p("ORGAN", ("16", "SQR"), ("8", "SQR", 64), ("4", "SQR", 64), (100, 80, 60, 0), 96, 5, 0, (0, 60, 127), (0, 40, 127, 5), kbd=100, fx=(0, 20, 0, 20), pat=5)
p("5TH LEAD", ("8", "SAW"), ("8", "SAW", 127), ("16", "SQR"), (100, 100, 60, 0), 66, 40, 60, (0, 70, 70), (0, 80, 112, 40), flags="GL", gl=18, fx=(0, 15, 35, 30), pat=4)
# ---- keys, plucks, percussion-like tones (15)
p("CLAVI", ("8", "PN"), ("8", "PN", 65), ("4", "SQR"), (100, 80, 40, 0), 60, 40, 80, (0, 24, 0), (0, 34, 0, 10), kbd=90, fx=(0, 0, 10, 10), pat=3)
p("PLUCK", ("8", "PN"), ("8", "SQR", 70), ("4", "SAW"), (100, 80, 0, 0), 55, 45, 85, (0, 50, 0), (0, 60, 0, 40), kbd=90, fx=(0, 15, 35, 30), pat=3)
p("HARPSI", ("8", "SAW"), ("4", "PN", 64), ("8", "PN", 66), (100, 70, 60, 0), 72, 20, 50, (0, 30, 0), (0, 40, 0, 15), kbd=100, fx=(0, 10, 15, 25), pat=3)
p("MARIMBA", ("8", "TRI"), ("2", "TRI", 64), ("8", "SQR"), (110, 30, 20, 0), 46, 20, 60, (0, 28, 0), (0, 36, 0, 12), kbd=100, fx=(0, 0, 20, 30), pat=3)
p("XYLO", ("4", "TRI"), ("2", "SQR", 64), ("4", "SQR"), (110, 40, 20, 0), 60, 20, 60, (0, 24, 0), (0, 28, 0, 8), kbd=100, fx=(0, 0, 20, 30), pat=3)
p("KALIMBA", ("4", "TRI"), ("2", "TRI", 64), ("4", "PN", 64), (110, 40, 0, 0), 52, 25, 60, (0, 32, 0), (0, 42, 0, 20), kbd=100, fx=(0, 0, 30, 35), pat=3)
p("E PIANO", ("8", "TRI"), ("4", "TRI", 64), ("8", "SQR", 64), (100, 40, 30, 0), 56, 20, 50, (0, 50, 10), (0, 70, 20, 30), kbd=90, fx=(0, 25, 20, 30), pat=3)
p("BELL", ("4", "TRI"), ("2", "TRI", 78), ("4", "TRI", 90), (100, 50, 40, 0), 76, 15, 30, (0, 60, 20), (0, 90, 10, 60), kbd=110, fx=(0, 10, 40, 55), pat=3)
p("GLASS", ("4", "TRI"), ("2", "TRI", 64), ("4", "SQR", 73), (100, 60, 20, 0), 80, 40, 30, (6, 70, 40), (4, 85, 40, 60), kbd=110, fx=(0, 15, 40, 55), pat=3)
p("HARP", ("8", "TRI"), ("4", "TRI", 64), ("8", "PN", 64), (100, 50, 30, 0), 54, 25, 60, (0, 40, 0), (0, 55, 0, 40), kbd=100, fx=(0, 10, 30, 40), pat=3)
p("STEEL DRUM", ("8", "TRI"), ("4", "SQR", 70), ("8", "TRI", 66), (100, 50, 40, 0), 64, 50, 70, (0, 30, 0), (0, 42, 0, 18), kbd=100, fx=(0, 10, 25, 40), pat=3)
p("BLIP", ("4", "SQR"), ("4", "PN", 64), ("8", "SQR"), (100, 60, 30, 0), 66, 30, 80, (0, 14, 0), (0, 20, 0, 6), kbd=100, fx=(0, 0, 20, 15), pat=3)
p("ARP SAW", ("8", "SAW"), ("8", "SAW", 65), ("4", "SQR"), (100, 90, 40, 0), 44, 60, 100, (0, 30, 0), (0, 40, 40, 14), kbd=80, fx=(0, 5, 30, 25), pat=4)
p("ARP PULSE", ("8", "PW"), ("8", "PN", 66), ("4", "SQR"), (100, 90, 40, 0), 46, 55, 95, (0, 32, 0), (0, 44, 50, 16), kbd=80, fx=(0, 5, 35, 25), pat=4)
p("SYNC-ISH", ("8", "SAW"), ("8", "SHK", 90), ("8", "SAW", 40), (100, 100, 70, 0), 66, 50, 90, (0, 60, 30), (0, 72, 100, 30), mod=30, flags="MOSC", fx=(20, 0, 25, 20), pat=4)
# ---- effects and noise (15)
p("NOISE FX", ("16", "SAW"), ("16", "SAW", 64), ("16", "SQR"), (60, 0, 0, 110), 40, 90, 100, (60, 100, 20), (20, 100, 90, 90), kbd=30, fx=(0, 20, 35, 50), mm=127, pat=7)
p("WIND", ("16", "SAW"), ("16", "SAW", 64), ("16", "SQR"), (0, 0, 0, 110), 40, 70, 60, (100, 100, 100), (100, 100, 110, 100), mod=40, flags="MFLT PINK", kbd=0, fx=(0, 30, 30, 70), mm=127, pat=7)
p("JET", ("16", "SAW"), ("16", "SAW", 64), ("16", "SQR"), (0, 0, 0, 120), 30, 80, 110, (100, 110, 0), (90, 100, 110, 100), kbd=0, fx=(0, 20, 30, 60), pat=7)
p("SIREN", ("8", "SQR"), ("8", "SQR", 64), ("4", "TRI"), (100, 80, 40, 0), 90, 10, 0, (0, 60, 60), (0, 60, 127, 5), mod=127, flags="MOSC SQR", fx=(0, 0, 30, 25), pat=7)
p("LASER", ("4", "SAW"), ("2", "SQR", 64), ("4", "SQR"), (100, 60, 30, 0), 80, 60, 110, (0, 20, 0), (0, 24, 0, 6), mod=100, flags="MOSC", fx=(15, 0, 30, 25), pat=7)
p("ZAP", ("4", "SAW"), ("4", "SQR", 70), ("8", "SAW", 90), (100, 80, 50, 0), 40, 100, 120, (0, 18, 0), (0, 26, 0, 8), fx=(25, 0, 20, 20), pat=7)
p("THUNDER", ("32", "SAW"), ("32", "SAW", 64), ("LO", "SAW"), (0, 0, 0, 120), 24, 40, 50, (30, 110, 40), (20, 110, 70, 100), kbd=0, fx=(0, 0, 25, 70), pat=7)
p("KICK", ("32", "TRI"), ("LO", "TRI", 64), ("32", "SQR"), (120, 90, 30, 0), 28, 30, 110, (0, 22, 0), (0, 36, 0, 8), kbd=0, fx=(0, 0, 0, 8), pat=6)
p("TOM", ("16", "TRI"), ("32", "TRI", 64), ("16", "SQR"), (120, 80, 20, 20), 36, 30, 100, (0, 28, 0), (0, 44, 0, 14), kbd=60, fx=(0, 0, 15, 20), pat=6)
p("SNARE", ("8", "SQR"), ("16", "TRI", 64), ("8", "SQR"), (60, 50, 0, 100), 62, 30, 80, (0, 24, 0), (0, 34, 0, 10), kbd=0, fx=(5, 0, 10, 25), pat=6)
p("HI HAT", ("4", "SQR"), ("2", "SQR", 80), ("4", "SQR", 100), (0, 0, 0, 120), 100, 20, 30, (0, 10, 0), (0, 14, 0, 4), kbd=0, fx=(0, 0, 0, 10), pat=6)
p("CLAP", ("8", "SQR"), ("8", "SQR", 64), ("8", "SQR"), (0, 0, 0, 120), 60, 50, 80, (0, 20, 0), (0, 28, 0, 10), kbd=0, fx=(0, 0, 10, 40), pat=6)
p("ALARM", ("8", "SQR"), ("4", "SQR", 80), ("8", "SQR", 64), (100, 70, 50, 0), 80, 20, 20, (0, 80, 80), (0, 80, 127, 5), mod=100, flags="MOSC SQR", fx=(10, 0, 30, 20), pat=7)
p("SCI-FI", ("8", "SAW"), ("4", "TRI", 70), ("8", "SQR", 60), (100, 60, 50, 0), 60, 70, 60, (30, 90, 40), (20, 100, 100, 70), mod=70, flags="MOSC MFLT", fx=(10, 25, 40, 50), mm=60, pat=7)
p("HOWL", ("16", "SHK"), ("16", "SAW", 64), ("8", "PN"), (100, 90, 40, 0), 50, 85, 80, (60, 100, 30), (50, 100, 100, 80), mod=60, flags="MOSC MFLT GL", gl=70, fx=(20, 20, 35, 50), mm=30, pat=7)

# ---- effects: every patch its own (DIST, CHORUS, DELAY, REVERB); the reverb is for pads, bells and atmospheres only
FXMAP = {
 "MOOG BASS": (6, 0, 0, 4), "SUB BASS": (0, 0, 0, 0), "FAT BASS": (8, 28, 0, 5), "FUNK BASS": (12, 0, 22, 0), "RESO BASS": (18, 0, 0, 0),
 "ACID BASS": (35, 0, 32, 0), "PULSE BASS": (0, 18, 0, 6), "SQUARE BASS": (10, 0, 0, 0), "GROWL BASS": (55, 0, 0, 0), "DEEP BASS": (0, 0, 0, 10),
 "THUMP BASS": (22, 0, 0, 0), "SLAP BASS": (0, 0, 16, 4), "STAB BASS": (14, 0, 26, 8), "SEQ BASS": (0, 0, 40, 6), "DRIVE BASS": (80, 0, 0, 0),
 "HOLLOW BASS": (0, 40, 0, 8), "RUBBER BASS": (28, 0, 18, 0), "SOFT BASS": (0, 22, 0, 12), "BUZZ BASS": (45, 12, 0, 0), "OCTAVE BASS": (16, 0, 24, 0),
 "PEDAL BASS": (0, 0, 0, 18), "WOBBLE BASS": (30, 0, 28, 0), "PHAT BASS": (20, 45, 0, 4), "PLUCK BASS": (0, 0, 34, 8),
 "MOOG LEAD": (0, 0, 28, 8), "FAT LEAD": (18, 35, 30, 10), "SOLO LEAD": (10, 0, 45, 14), "SINGING": (0, 26, 22, 24), "PULSE LEAD": (0, 32, 18, 6),
 "SQUARE LEAD": (15, 0, 38, 0), "SAW LEAD": (20, 12, 34, 10), "SINE LEAD": (22, 36, 34, 14), "GLIDE LEAD": (0, 0, 52, 16), "RESO LEAD": (25, 0, 26, 6),
 "WARM LEAD": (0, 38, 0, 22), "BITE LEAD": (50, 0, 18, 0), "FLUTE LEAD": (0, 0, 14, 30), "REED LEAD": (6, 22, 0, 10), "VOX LEAD": (0, 48, 0, 26),
 "ROCK LEAD": (85, 0, 22, 6), "PROG LEAD": (30, 16, 56, 12), "SCREAM LEAD": (75, 0, 30, 0), "OCTAVE LEAD": (0, 0, 36, 6), "DUAL LEAD": (12, 60, 0, 6),
 "SOFT LEAD": (0, 24, 0, 28), "WHISTLE": (0, 0, 30, 36), "CRY LEAD": (22, 0, 40, 18), "SWEEP LEAD": (8, 30, 0, 24), "THIN LEAD": (0, 0, 26, 6),
 "BIG LEAD": (35, 50, 38, 20), "5TH LEAD": (28, 10, 44, 8),
 "BRASS": (4, 0, 0, 12), "SOFT BRASS": (0, 16, 0, 18), "BRASS STAB": (12, 0, 20, 8), "HORN": (0, 0, 0, 26), "TRUMPET": (0, 0, 18, 10),
 "SYN BRASS": (0, 52, 0, 14), "OBOE": (0, 0, 0, 16), "CLARINET": (0, 0, 0, 22), "SAX": (14, 0, 12, 14), "STRINGS": (0, 70, 0, 22),
 "SLOW STRING": (0, 55, 0, 40), "SOFT PAD": (0, 40, 0, 34), "MOOG PAD": (6, 24, 26, 28), "WARM PAD": (0, 45, 0, 30), "DARK PAD": (14, 0, 0, 50),
 "SWEEP PAD": (0, 64, 22, 36), "CHOIR": (0, 30, 0, 44), "DRONE": (30, 12, 16, 26), "ORGAN": (8, 52, 0, 10),
 "CLAVI": (18, 0, 12, 0), "PLUCK": (0, 0, 40, 8), "HARPSI": (0, 0, 14, 10), "MARIMBA": (0, 0, 30, 16), "XYLO": (0, 0, 24, 10), "KALIMBA": (0, 10, 38, 20),
 "E PIANO": (0, 55, 0, 14), "BELL": (0, 0, 36, 40), "GLASS": (0, 14, 44, 34), "HARP": (0, 0, 28, 32), "STEEL DRUM": (0, 22, 22, 18), "BLIP": (0, 0, 30, 0),
 "ARP SAW": (0, 10, 48, 6), "ARP PULSE": (6, 0, 54, 4), "SYNC-ISH": (24, 0, 22, 4),
 "NOISE FX": (40, 20, 35, 30), "WIND": (0, 60, 20, 45), "JET": (0, 70, 38, 30), "SIREN": (0, 0, 48, 22), "LASER": (20, 0, 56, 10), "ZAP": (30, 0, 44, 8),
 "THUNDER": (0, 0, 0, 60), "KICK": (18, 0, 0, 2), "TOM": (0, 0, 0, 10), "SNARE": (10, 0, 0, 18), "HI HAT": (0, 0, 0, 6), "CLAP": (0, 0, 0, 34),
 "ALARM": (22, 0, 26, 8), "SCI-FI": (12, 36, 40, 30), "HOWL": (28, 24, 34, 40),
}
for x in P:
    x["fx"] = FXMAP[x["name"]]
assert set(FXMAP) == {x["name"] for x in P}, set(FXMAP) ^ {x["name"] for x in P}

def _cat(i, n):                        # the groups, by a patch's place before the showcase moves it
    if n in ("FIFTHS", "5TH LEAD", "ARP SAW", "ARP PULSE", "SYNC-ISH", "SINE LEAD"): return "L"
    if i < 24: return "B"
    if i < 50: return "L"
    if n in ("ORGAN", "CLAVI", "E PIANO"): return "K"
    if n in ("KICK", "TOM", "SNARE", "HI HAT", "CLAP"): return "D"
    if i < 70: return "P"
    if i < 85: return "U"
    return "F"
for i, x in enumerate(P):
    x["cat"] = _cat(i, x["name"])

# ---- the first ten: a showcase of very different sounds; the groups follow
SHOW = ["SAW LEAD", "SINE LEAD", "MOOG BASS", "ACID BASS", "5TH LEAD", "PLUCK", "HARP", "JET", "SIREN", "WARM PAD"]
SHOWCAT = "LLBBLUUFFP"
byname = {x["name"]: x for x in P}
P[:] = [byname[n] for n in SHOW] + [x for x in P if x["name"] not in SHOW]
for i, x in enumerate(P):
    if i < 10:
        x["cat"] = SHOWCAT[i]
    x["e"][7] = i                      # SLOT = its place in the list

assert len(P) == 100, len(P)
names = [x["name"] for x in P]
assert len(set(names)) == 100, [n for n in names if names.count(n) > 1]

out = ["/* SPDX-License-Identifier: GPL-3.0-only",
       " * Generated by tools/gen_fmug_presets.py: the 100 monophonic factory patches of FMini-1 (do not edit by hand). */", ""]
out.append("static const uint8_t FMUG_FACTORY[FMUG_NFACTORY][FG_SIZE] = {")
for x in P:
    g = x["g"] + [0] * 14
    out.append("    FGP(" + ", ".join(str(v) for v in x["g"]) + "),   /* " + str(P.index(x) + 1) + " " + x["name"] + " */")
out.append("};\n")
out.append("/* {CUT, EMPH, CNTR, F.ATK, F.DEC, F.SUS, MOD, SLOT}: the factory patch; every one is MONO (legato) */")
def cat(i, n):
    return P[i]["cat"]
out.append('#define FMUG_CATS "' + "".join(cat(i, x["name"]) for i, x in enumerate(P)) + '"   /* category.c: B bass, L lead, P pad, U pluck, K keys, D drum, F fx */')
out.append("static const preset_t FMUG_PRESETS[] = {")
for x in P:
    e = ", ".join(str(v) for v in x["e"])
    a = ", ".join(str(v) for v in x["amp"])
    f = ", ".join(str(v) for v in x["fx"])
    out.append('    {"%s", {%s}, {%s}, 0, 1, FX(%s), PAT(%d)},' % (x["name"], e, a, f, x["pat"]))
out.append("};")
path = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "src" / "eng_fmug_presets.h"
path.write_text("\n".join(out) + "\n")
print("wrote", path, len(P), "presets")
