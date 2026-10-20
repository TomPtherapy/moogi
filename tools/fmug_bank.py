# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the factory bank. 00-09 a showcase, 10-19 hard leads, 20-29 soft leads, 30-39 basses, 40-49 pads and strings,
# 50-59 synth brass (OCT-1), 60-69 plucks, 70-79 keys, 80-89 atmospheres and drones, 90-99 effects.
# tools/gen_fmug_presets.py turns it into firmware/src/eng_fmug_presets.h; tools/preset_lab.py plays and measures it.
#
# Reading a patch: o(range, wave, level, cents) are the three oscillators; cutoff the filter's -3 dB corner in Hz when middle C is
# played (keyboard tracking included), emph the Minimoog's EMPHASIS dial 0 .. 10 (sings from about 8), cntr the filter
# contour's amount (co(octaves)), fenv (attack ms, decay ms, sustain 0..127) the filter contour, env (attack, decay, sustain,
# release) the loudness contour; send = (drive, chorus, delay, reverb) levels; rv / dl / ch / dr the FX settings.
import os
from gen_fmug_presets import A, o, osc_off, co, st_pit, hz, ms, cut, dly, semi, det


def showcase(b):
    b.add("MINI LEAD", "lead", (o("8", "SAW", 90), o("8", "SAW", 90, 11), o("16", "SQR", 70)),
          cutoff=1010, emph=3.5, cntr=co(2.0), kbd=64, fenv=(0, 350, 70), env=(3, 400, 110, 180),
          vib=30, mosc=False, lfo=(5.5, "SIN", 400, "OFF"), glide=40, oct=1,      # (GLIDE 3.9 (-10 %) on the knob)
          send=(30, 0, 55, 40), rv=("HALL", 60, 80), dl=("ANALOG", 375, 45), dr=("TUBE", 70, 64), pat=4)
    b.add("I FEEL ARP", "bass", (o("16", "SAW", 120), o("16", "SQR", 110, 11), o("32", "TRI", 90)),
          cutoff=309, emph=3.6, cntr=co(2.5), kbd=40, fenv=(0, 120, 20), env=(2, 180, 40, 60),
          send=(15, 0, 20, 10), rv=("PLATE", 40, 90), dl=("DIGITAL", 188, 35),
          arp=A("UP", "1/16", 2, 0.45), arp_on=True, pat=2)
    b.add("LUSH STRINGS", "pad", (o("8", "SAW", 80), o("8", "SAW", 80, 22), o("4", "SAW", 60, -22)),
          cutoff=1150, emph=2.3, cntr=0, kbd=64, fenv=(0, 400, 100), env=(350, 600, 118, 700),
          vib=14, mosc=False, lfo=(5.0, "SIN", 800, "OFF"), poly=True,
          send=(0, 90, 0, 50), rv=("HALL", 80, 85), ch=("MULTI", 70, 0.5), pat=5)
    b.add("ROTARY ORG", "keys", (o("16", "TRI", 90), o("8", "TRI", 100), o("4", "SQR", 50)),
          cutoff=2410, emph=0.0, cntr=0, kbd=100, fenv=(0, 100, 127), env=(6, 100, 127, 60),
          lfo=(6.5, "SIN", 0, "OFF"), trem=20, pit=st_pit(0.1), poly=True,
          send=(0, 80, 0, 64), rv=("HALL", 40, 70), ch=("STEREO", 90, 6.5), pat=6)
    b.add("CRYSTAL BELL", "keys", (o("4", "TRI", 100), o("4", "TRI", 70, 693), o("2", "TRI", 40, 516)),
          cutoff=700, emph=2.3, cntr=co(3.0), kbd=127, fenv=(0, 1500, 0), env=(1, 2500, 0, 1500), poly=True,
          send=(0, 20, 30, 70), rv=("PLATE", 80, 100), dl=("DIGITAL", 375, 40), ch=("STEREO", 60, 0.5), pat=7)
    b.add("DUB STAB", "pluck", (o("8", "SAW", 115), o("8", "SAW", 115, 11), o("8", "SQR", 90, -11)),
          cutoff=406, emph=3.3, cntr=co(3.2), kbd=64, fenv=(0, 180, 0), env=(2, 380, 0, 200), poly=True,
          send=(20, 0, 90, 45), rv=("SPRING", 55, 80), dl=("TAPE", 375, 70), dr=("TUBE", 64, 64), pat=6)
    b.add("CS-80 BRASS", "brass", (o("8", "SAW", 90), o("8", "SAW", 90, 14), o("16", "SAW", 70, -11)),
          cutoff=293, emph=3.1, cntr=co(3.0), kbd=64, fenv=(250, 900, 70), env=(90, 600, 118, 500),
          vib=20, mosc=True, lfo=(5.2, "SIN", 700, "OFF"), poly=True, oct=-1,
          send=(0, 60, 0, 45), rv=("HALL", 70, 80), ch=("STEREO", 60, 0.8), pat=5)
    b.add("SPACE DRONE", "atmo", (o("16", "SAW", 90), o("16", "SAW", 90, 17), o("8", "PW", 60)), noise=25, pink=True,
          cutoff=272, emph=3.8, cntr=co(2.5), kbd=40, fenv=(3000, 4000, 60), env=(2500, 3000, 118, 3200),
          vib=70, mflt=True, lfo=(0.08, "SIN", 0, "OFF"), poly=True,
          send=(0, 80, 60, 90), rv=("CLOUD", 90, 70), dl=("REVERSE", 560, 50), ch=("MULTI", 70, 0.3), pat=5)
    b.add("TANGERINE", "pluck", (o("8", "SAW", 100), o("8", "SQR", 90, 7), o("4", "SAW", 60, -7)),
          cutoff=555, emph=4.3, cntr=co(2.6), kbd=70, fenv=(0, 240, 10), env=(1, 330, 25, 160), poly=True,
          send=(20, 40, 85, 55), rv=("PLATE", 60, 90), dl=("DIGITAL", 375, 62), ch=("STEREO", 60, 0.4),
          arp=A("UPDN", "1/16", 3, 0.5, 0, 127), arp_on=True, pat=13)
    b.add("E.PIANO", "keys", (o("8", "TRI", 100), o("4", "TRI", 60), o("2", "SQR", 30)),
          cutoff=973, emph=2.5, cntr=co(2.0), kbd=90, fenv=(0, 250, 10), env=(2, 1800, 10, 250), poly=True,
          send=(0, 70, 0, 25), rv=("PLATE", 50, 90), ch=("STEREO", 60, 0.7), pat=6)


def hard_leads(b):                       # 10-19: intense, aggressive, big
    b.add("EMERSON", "lead", (o("8", "SAW", 100), o("8", "SAW", 100, -17), o("16", "SQR", 75, 6)),
          cutoff=1110, emph=4.6, cntr=co(1.8), kbd=90, fenv=(0, 600, 55), env=(2, 300, 118, 260),
          vib=28, mosc=False, lfo=(5.3, "SIN", 600, "OFF"), glide=150,
          send=(60, 0, 55, 35), rv=("PLATE", 45, 80), dl=("TAPE", 300, 38), dr=("TUBE", 80, 70), pat=4)
    b.add("NUMAN", "lead", (o("8", "SAW", 100), o("8", "SAW", 100, 9), o("4", "SAW", 60, -9)),
          cutoff=1470, emph=2.8, cntr=co(1.2), kbd=100, fenv=(0, 700, 85), env=(4, 220, 122, 320),
          vib=14, mosc=True, lfo=(5.6, "SIN", 500, "OFF"),
          send=(20, 115, 35, 45), rv=("HALL", 65, 85), dl=("DIGITAL", 250, 30), ch=("JUNO", 115, 0.45), pat=4)
    b.add("HAMMER", "lead", (o("8", "SHK", 100), o("8", "PW", 80, 12), o("16", "SQR", 60)),
          cutoff=784, emph=4.3, cntr=co(2.4), kbd=80, fenv=(40, 450, 25), env=(1, 500, 100, 120),
          vib=18, mosc=True, lfo=(5.8, "SIN", 700, "OFF"), glide=45,
          send=(70, 0, 70, 30), rv=("PLATE", 40, 80), dl=("ANALOG", 428, 52), dr=("TUBE", 90, 64), pat=4)
    b.add("FIFTHS", "lead", (o("8", "SAW", 100), o("4", "SAW", 85, -500), o("8", "SHK", 60, 6)),
          cutoff=1340, emph=3.8, cntr=co(1.6), kbd=90, fenv=(0, 450, 65), env=(3, 300, 120, 300),
          vib=20, mosc=True, lfo=(5.0, "SIN", 500, "OFF"), glide=70,
          send=(75, 70, 35, 50), rv=("PLATE", 75, 85), dl=("SLAP", 130, 28), ch=("JUNO", 70, 0.5), dr=("SOFT", 80, 64), pat=4)
    b.add("RAVE ARP", "lead", (o("8", "SAW", 100, -19), o("8", "SAW", 100, 19), o("4", "SAW", 75)),
          cutoff=1430, emph=3.0, cntr=co(2.0), kbd=90, fenv=(0, 200, 50), env=(1, 260, 70, 110), poly=True,
          send=(25, 70, 75, 45), rv=("PLATE", 55, 90), dl=("DIGITAL", 375, 55), ch=("JUNO", 70, 0.35),
          arp=A("UP", "1/16", 2, 0.6, 0, 127), arp_on=True, pat=13)
    b.add("ACID LEAD", "lead", (o("8", "SAW", 115), o("8", "SQR", 60, 7), osc_off()),
          cutoff=227, emph=7.8, cntr=co(4.2), kbd=70, fenv=(0, 160, 5), env=(1, 200, 110, 70), glide=70,
          send=(70, 0, 55, 25), rv=("ROOM", 30, 80), dl=("ANALOG", 375, 55), dr=("TUBE", 85, 70),
          arp=A("UPDN", "1/16", 2, 0.5, 0, 127), arp_on=True, pat=1)
    b.add("PULSE HERO", "lead", (o("8", "PW", 100), o("8", "PN", 95, 13), o("16", "SQR", 70)),
          cutoff=1770, emph=3.1, cntr=co(1.2), kbd=100, fenv=(0, 350, 55), env=(2, 250, 118, 170),
          vib=24, mosc=True, lfo=(5.0, "SIN", 400, "OFF"),
          send=(35, 70, 50, 35), rv=("ROOM", 55, 85), dl=("SLAP", 120, 30), ch=("STEREO", 80, 0.7), pat=4)
    b.add("TITAN", "lead", (o("16", "SQR", 105), o("8", "SAW", 100), o("8", "SAW", 100, -23)),
          cutoff=428, emph=3.7, cntr=co(3.2), kbd=55, fenv=(70, 800, 45), env=(3, 260, 122, 400),
          vib=16, mosc=True, lfo=(4.8, "SIN", 700, "OFF"), glide=80,
          send=(85, 0, 35, 55), rv=("HALL", 100, 50), dl=("TAPE", 375, 40), dr=("TUBE", 50, 70), pat=4)
    b.add("FUZZ LEAD", "lead", (o("8", "SQR", 120), o("8", "SAW", 118, 6), o("4", "SQR", 70, -6)),
          cutoff=2000, emph=2.8, cntr=co(0.9), kbd=100, fenv=(0, 250, 70), env=(1, 200, 120, 140),
          vib=20, mosc=True, lfo=(5.5, "SIN", 500, "OFF"), glide=30,
          send=(127, 0, 50, 25), rv=("ROOM", 40, 80), dl=("DIGITAL", 188, 35), dr=("SOFT", 110, 50), pat=4)
    b.add("GROWL", "lead", (o("8", "SHK", 100), o("8", "SAW", 100, -19), o("8", "PN", 70)),
          cutoff=392, emph=5.0, cntr=co(1.2), kbd=60, fenv=(0, 300, 50), env=(3, 250, 120, 250),
          vib=85, mflt=True, lfo=(2.0, "TRI", 0, "1/8"),
          send=(60, 40, 35, 40), rv=("PLATE", 45, 70), dl=("DIGITAL", 250, 30), dr=("TUBE", 70, 64), pat=4)


def soft_leads(b):                       # 20-29: gentler, singing, mellow
    b.add("MOOG FLUTE", "lead", (o("8", "TRI", 105), o("4", "SQR", 35, 3), osc_off()), noise=14, pink=True,
          cutoff=932, emph=2.5, cntr=co(0.8), kbd=100, fenv=(0, 200, 55), env=(55, 400, 118, 260),
          vib=26, mosc=True, lfo=(5.0, "SIN", 700, "OFF"),
          send=(0, 40, 25, 55), rv=("ROOM", 55, 85), ch=("STEREO", 40, 0.4), pat=4)
    b.add("G-FUNK", "lead", (o("8", "TRI", 100), o("4", "SAW", 50), osc_off()),
          cutoff=994, emph=3.3, cntr=0, kbd=100, fenv=(0, 200, 100), env=(30, 500, 120, 350), glide=240,
          vib=18, mosc=True, lfo=(6.0, "SIN", 800, "OFF"),
          send=(0, 60, 50, 40), rv=("PLATE", 40, 80), dl=("ANALOG", 375, 35), ch=("STEREO", 40, 0.5), pat=4)
    b.add("SINE SOLO", "lead", (o("8", "TRI", 120), o("8", "SAW", 25), osc_off()),
          cutoff=487, emph=0.0, cntr=0, kbd=100, fenv=(0, 200, 100), env=(8, 600, 120, 400), glide=110,
          vib=24, mosc=True, lfo=(5.5, "SIN", 900, "OFF"),
          send=(0, 0, 55, 60), rv=("HALL", 85, 70), dl=("ANALOG", 429, 45), pat=4)
    b.add("BANKS", "lead", (o("8", "SAW", 95), o("8", "SAW", 95, -12), o("16", "TRI", 70)),
          cutoff=719, emph=2.8, cntr=co(1.4), kbd=70, fenv=(120, 1200, 60), env=(40, 500, 118, 450),
          vib=22, mosc=True, lfo=(5.0, "SIN", 800, "OFF"),
          send=(0, 70, 40, 60), rv=("HALL", 90, 70), dl=("ANALOG", 375, 30), ch=("STEREO", 70, 0.35), pat=4)
    b.add("OBOE", "lead", (o("8", "PW", 100), o("8", "SQR", 60, 8), o("4", "PN", 30)),
          cutoff=994, emph=3.6, cntr=co(0.8), kbd=100, fenv=(30, 300, 70), env=(35, 300, 118, 150),
          vib=22, mosc=True, lfo=(5.5, "SIN", 600, "OFF"),
          send=(0, 25, 20, 35), rv=("ROOM", 50, 75), pat=4)
    b.add("SOLO VIOLIN", "lead", (o("8", "SAW", 100), o("8", "SAW", 90, 10), o("4", "TRI", 30)),
          cutoff=1210, emph=2.7, cntr=co(0.8), kbd=100, fenv=(60, 300, 80), env=(110, 300, 120, 220),
          vib=40, mosc=True, lfo=(5.8, "SIN", 900, "OFF"),
          send=(0, 35, 20, 55), rv=("HALL", 85, 75), ch=("STEREO", 40, 0.5), pat=4)
    b.add("DREAMER", "lead", (o("8", "TRI", 100), o("8", "SQR", 55, 5), o("4", "TRI", 60, -6)),
          cutoff=801, emph=2.7, cntr=co(1.5), kbd=90, fenv=(0, 500, 25), env=(5, 700, 40, 500), poly=True,
          send=(0, 60, 80, 80), rv=("CLOUD", 100, 70), dl=("TAPE", 500, 50), ch=("STEREO", 50, 0.3),
          arp=A("UPDN", "8T", 2, 0.8, 0, 127), arp_on=True, pat=13)
    b.add("WHISTLE", "lead", (o("4", "TRI", 110), o("8", "TRI", 40), osc_off()), noise=8, pink=True,
          cutoff=1290, emph=0.3, cntr=0, kbd=100, fenv=(0, 200, 100), env=(80, 300, 120, 300), glide=90,
          vib=34, mosc=True, lfo=(5.8, "SIN", 1200, "OFF"),
          send=(0, 30, 30, 60), rv=("PLATE", 45, 85), pat=4)
    b.add("MELLOW PULSE", "lead", (o("8", "SQR", 100), o("8", "TRI", 80, -14), o("16", "SQR", 40)),
          cutoff=466, emph=2.1, cntr=co(1.2), kbd=80, fenv=(0, 500, 55), env=(10, 400, 118, 300),
          vib=18, mosc=True, lfo=(5.2, "SIN", 500, "OFF"),
          send=(0, 100, 45, 55), rv=("HALL", 80, 70), dl=("ANALOG", 375, 35), ch=("JUNO", 100, 0.4),
          arp=A("UPDN", "1/8", 2, 0.7, 10, 127), arp_on=True, pat=13)
    b.add("SAX LEAD", "lead", (o("8", "SHK", 100), o("8", "PW", 60, 3), o("16", "SQR", 30)), noise=10, pink=True,
          cutoff=801, emph=3.1, cntr=co(1.4), kbd=90, fenv=(25, 250, 55), env=(25, 300, 118, 160), glide=40,
          vib=30, mosc=True, lfo=(5.4, "SIN", 800, "OFF"),
          send=(30, 25, 30, 40), rv=("ROOM", 45, 75), dr=("TUBE", 60, 64), pat=4)


def basses(b):                           # 30-39
    b.add("MOOG BASS", "bass", (o("16", "SAW", 110), o("16", "SAW", 100, -9), o("8", "TRI", 60)),
          cutoff=175, emph=3.0, cntr=co(2.2), kbd=40, fenv=(0, 600, 35), env=(3, 400, 118, 150),
          send=(30, 0, 0, 8), rv=("ROOM", 20, 60), dr=("TUBE", 60, 64), oct=-1, pat=2)
    b.add("CHAMELEON", "bass", (o("16", "SQR", 110), o("16", "SQR", 95, 5), o("8", "SAW", 55, -5)),
          cutoff=210, emph=5.5, cntr=co(3.9), kbd=55, fenv=(0, 95, 0), env=(1, 170, 45, 50),
          send=(35, 0, 25, 10), rv=("ROOM", 20, 60), dl=("SLAP", 100, 25), dr=("TUBE", 70, 64), oct=-1, pat=2)
    b.add("SUB BASS", "bass", (o("32", "TRI", 110), o("16", "TRI", 100), osc_off()),
          cutoff=130, emph=0.6, cntr=co(0.5), kbd=0, fenv=(0, 200, 60), env=(8, 300, 118, 120),
          send=(40, 0, 0, 0), dr=("SOFT", 40, 64), oct=-1, pat=8)
    b.add("WOBBLE BASS", "bass", (o("16", "SAW", 105), o("16", "SAW", 105, -23), o("32", "TRI", 70)),
          cutoff=238, emph=5.1, cntr=co(0.5), kbd=40, fenv=(0, 200, 60), env=(4, 300, 118, 150),
          vib=110, mflt=True, lfo=(2.0, "TRI", 0, "1/8"),
          send=(80, 0, 0, 15), dr=("TUBE", 60, 64), oct=-1, pat=2)
    b.add("OCTAVE PUMP", "bass", (o("16", "SQR", 110), o("8", "SAW", 75, 7), osc_off()),
          cutoff=531, emph=2.5, cntr=co(3.0), kbd=50, fenv=(0, 90, 0), env=(1, 160, 40, 50),
          send=(30, 0, 25, 12), dl=("DIGITAL", 188, 30), arp=A("UP+8", "1/8", 1, 0.55, 0, 127), arp_on=True, oct=-1, pat=2)
    b.add("SEQ BASS", "bass", (o("16", "SAW", 110), o("16", "SAW", 100, 13), o("32", "SQR", 70)),
          cutoff=392, emph=4.8, cntr=co(2.8), kbd=60, fenv=(0, 110, 0), env=(1, 140, 0, 40),
          send=(50, 0, 40, 15), dl=("DIGITAL", 188, 35), dr=("TUBE", 70, 64),
          arp=A("ORD", "1/16", 1, 0.35, 25, 127), arp_on=True, oct=-1, pat=13)
    b.add("REESE", "bass", (o("16", "SAW", 105, -25), o("16", "SAW", 105, 25), o("8", "SAW", 60)),
          cutoff=254, emph=2.5, cntr=co(0.8), kbd=40, fenv=(0, 500, 60), env=(8, 400, 120, 300),
          vib=30, mflt=True, lfo=(0.25, "TRI", 0, "OFF"),
          send=(60, 100, 0, 35), ch=("MULTI", 100, 0.25), dr=("TUBE", 50, 64), oct=-1, pat=8)
    b.add("RUBBER", "bass", (o("16", "TRI", 127), o("16", "SQR", 115), o("8", "TRI", 60)),
          cutoff=316, emph=3.7, cntr=co(2.2), kbd=50, fenv=(0, 120, 0), env=(1, 500, 0, 100), penv=st_pit(2.5),
          send=(25, 0, 18, 0), dr=("TUBE", 50, 64), oct=-1, pat=2)
    b.add("FUZZ BASS", "bass", (o("16", "SQR", 120), o("8", "SQR", 100, 7), o("16", "SQR", 70, -7)),
          cutoff=767, emph=2.7, cntr=co(1.0), kbd=70, fenv=(0, 200, 60), env=(2, 250, 115, 100),
          send=(127, 0, 0, 10), dr=("SOFT", 55, 55), arp=A("REPEAT", "1/8", 1, 0.6, 0, 127), arp_on=True, oct=-1, pat=2)
    b.add("WONDER BASS", "bass", (o("16", "SAW", 100), o("8", "SQR", 95), o("16", "TRI", 70)),
          cutoff=283, emph=3.6, cntr=co(2.8), kbd=60, fenv=(0, 260, 30), env=(3, 400, 100, 120), glide=35,
          send=(20, 0, 20, 20), dl=("ANALOG", 250, 30), oct=-1, pat=2)


def pads(b):                             # 40-49: pads and strings
    b.add("SOLINA", "pad", (o("8", "SAW", 90), o("8", "SAW", 90, 17), o("4", "PW", 55, -9)),
          cutoff=2130, emph=0.5, cntr=0, kbd=100, fenv=(0, 100, 127), env=(90, 400, 120, 400), poly=True,
          vib=8, mosc=True, lfo=(5.5, "SIN", 0, "OFF"),
          send=(0, 127, 0, 35), rv=("ROOM", 50, 100), ch=("JUNO", 120, 0.5), pat=5)
    b.add("WARM PAD", "pad", (o("8", "SAW", 85), o("8", "SQR", 75, -9), o("16", "SAW", 60, 5)),
          cutoff=520, emph=2.4, cntr=co(1.0), kbd=70, fenv=(900, 2000, 60), env=(700, 1200, 115, 1500), poly=True,
          send=(0, 70, 0, 80), rv=("HALL", 110, 60), ch=("STEREO", 60, 0.25), pat=5)
    b.add("JUNO PWM", "pad", (o("8", "PW", 90), o("8", "PN", 75, 13), o("8", "SQR", 60, -13)),
          cutoff=1320, emph=2.1, cntr=co(0.5), kbd=80, fenv=(0, 600, 90), env=(120, 400, 118, 500), poly=True,
          vib=6, mosc=True, lfo=(5.0, "SIN", 0, "OFF"),
          send=(0, 127, 0, 35), rv=("PLATE", 50, 80), ch=("JUNO", 127, 0.3),
          arp=A("UP", "1/8", 2, 0.85, 0, 127), arp_on=True, pat=13)
    b.add("CHOIR", "pad", (o("8", "TRI", 95), o("8", "SQR", 60, 6), o("4", "TRI", 60, -6)), noise=6, pink=True,
          cutoff=854, emph=3.5, cntr=co(0.6), kbd=60, fenv=(200, 800, 80), env=(500, 600, 118, 900), poly=True,
          vib=30, mosc=True, lfo=(5.0, "SIN", 1000, "OFF"),
          send=(0, 50, 0, 85), rv=("HALL", 100, 55), ch=("STEREO", 50, 0.3), pat=5)
    b.add("SWEEP PAD", "pad", (o("8", "SAW", 90), o("8", "SAW", 90, -14), o("16", "SAW", 65, 7)),
          cutoff=184, emph=3.8, cntr=co(3.5), kbd=40, fenv=(2500, 3500, 40), env=(1200, 1500, 115, 1800), poly=True,
          send=(0, 60, 50, 70), rv=("HALL", 90, 70), dl=("TAPE", 500, 50), ch=("STEREO", 50, 0.2), pat=5)
    b.add("ANGELS", "pad", (o("8", "TRI", 90), o("4", "TRI", 70), o("2", "TRI", 50, 7)),
          cutoff=4200, emph=0.0, cntr=0, kbd=100, fenv=(0, 100, 127), env=(900, 800, 110, 2500), poly=True,
          vib=14, mosc=True, lfo=(4.0, "SIN", 1500, "OFF"),
          send=(0, 60, 35, 110), rv=("CLOUD", 120, 90), dl=("TAPE", 750, 45), ch=("STEREO", 70, 0.2), pat=5)
    b.add("ECLIPSE", "pad", (o("32", "SAW", 100), o("16", "SAW", 95, -15), o("16", "SQR", 60, 15)),
          cutoff=175, emph=3.0, cntr=co(1.2), kbd=40, fenv=(1500, 3000, 50), env=(1100, 1500, 118, 2200), poly=True,
          vib=40, mflt=True, lfo=(0.12, "SIN", 0, "OFF"),
          send=(40, 50, 0, 90), rv=("HALL", 127, 35), dr=("TUBE", 30, 60), pat=5)
    b.add("PULSE PAD", "pad", (o("8", "SAW", 85), o("8", "PW", 70, 10), o("4", "SAW", 45, -10)),
          cutoff=767, emph=3.0, cntr=co(1.5), kbd=70, fenv=(0, 400, 35), env=(30, 600, 60, 900), poly=True,
          send=(0, 80, 60, 80), rv=("CLOUD", 100, 70), dl=("TAPE", 375, 45), ch=("STEREO", 60, 0.3),
          arp=A("DIVG", "1/8", 2, 0.9, 0, 127), arp_on=True, pat=13)
    b.add("CELLOS", "pad", (o("16", "SAW", 100), o("16", "SAW", 75, 12), o("8", "TRI", 50)),
          cutoff=337, emph=2.1, cntr=0, kbd=80, fenv=(0, 100, 127), env=(220, 400, 118, 500), poly=True,
          vib=42, mosc=True, lfo=(5.0, "SIN", 1200, "OFF"),
          send=(0, 40, 0, 60), rv=("HALL", 80, 60), ch=("STEREO", 40, 0.4), pat=5)
    b.add("AIR PAD", "pad", (o("8", "TRI", 50), osc_off(), osc_off()), noise=70, pink=True,
          cutoff=1040, emph=5.1, cntr=co(1.5), kbd=60, fenv=(1500, 2500, 50), env=(900, 1000, 118, 1800), poly=True,
          vib=60, mflt=True, lfo=(0.18, "TRI", 0, "OFF"),
          send=(0, 70, 0, 90), rv=("CLOUD", 110, 60), ch=("MULTI", 80, 0.2), pat=5)


def brass(b):                            # 50-59: all come in with OCT-1
    b.add("MOOG BRASS", "brass", (o("8", "SAW", 95), o("8", "SAW", 95, 10), o("16", "SQR", 60, -6)),
          cutoff=401, emph=3.0, cntr=co(2.6), kbd=70, fenv=(70, 300, 50), env=(35, 300, 118, 200), poly=True, oct=-1,
          send=(0, 40, 0, 40), rv=("HALL", 60, 80), ch=("STEREO", 40, 0.4), pat=5)
    b.add("JUMP BRASS", "brass", (o("8", "SAW", 100, -14), o("8", "SAW", 100, 14), o("4", "SAW", 50)),
          cutoff=428, emph=2.4, cntr=co(3.0), kbd=70, fenv=(25, 550, 40), env=(18, 500, 95, 300), poly=True, oct=-1,
          send=(15, 70, 0, 50), rv=("PLATE", 60, 85), ch=("JUNO", 70, 0.4), pat=5)
    b.add("TRUMPET", "brass", (o("8", "SAW", 100), o("8", "SQR", 70, 6), o("4", "SAW", 40)), noise=5, pink=True,
          cutoff=476, emph=2.8, cntr=co(2.6), kbd=90, fenv=(45, 200, 50), env=(30, 300, 118, 140), glide=25, oct=-1,
          vib=30, mosc=True, lfo=(5.5, "SIN", 900, "OFF"),
          send=(20, 0, 25, 35), rv=("ROOM", 50, 80), dl=("ANALOG", 250, 30), pat=4)
    b.add("FRENCH HORN", "brass", (o("8", "TRI", 100), o("4", "SAW", 40), o("16", "SQR", 45)),
          cutoff=323, emph=2.4, cntr=co(1.6), kbd=60, fenv=(120, 500, 60), env=(100, 400, 118, 400), poly=True, oct=-1,
          vib=14, mosc=True, lfo=(5.0, "SIN", 1000, "OFF"),
          send=(0, 35, 0, 70), rv=("HALL", 110, 45), pat=5)
    b.add("BRASS STAB", "brass", (o("8", "SAW", 105), o("8", "SAW", 105, 12), o("8", "SQR", 70, -12)),
          cutoff=520, emph=2.8, cntr=co(3.0), kbd=70, fenv=(0, 220, 0), env=(2, 260, 0, 140), poly=True, oct=-1,
          send=(40, 0, 55, 40), rv=("ROOM", 40, 85), dl=("ANALOG", 250, 35), pat=6)
    b.add("BRASS SWELL", "brass", (o("8", "SAW", 100), o("8", "SQR", 80, -9), o("4", "SAW", 45, 6)), noise=6, pink=True,
          cutoff=376, emph=3.5, cntr=co(3.6), kbd=70, fenv=(650, 700, 75), env=(550, 600, 118, 700), poly=True, oct=-1,
          vib=28, mosc=True, lfo=(5.6, "SIN", 1200, "OFF"),
          send=(40, 30, 35, 60), rv=("PLATE", 80, 80), dl=("ANALOG", 375, 30), dr=("SOFT", 70, 64), pat=5)
    b.add("HORN RIFF", "brass", (o("8", "SAW", 100), o("8", "SAW", 100, 8), o("16", "SQR", 60)),
          cutoff=457, emph=2.6, cntr=co(2.8), kbd=70, fenv=(10, 250, 25), env=(8, 300, 60, 120), poly=True, oct=-1,
          send=(25, 40, 40, 40), rv=("PLATE", 45, 85), dl=("ANALOG", 250, 30),
          arp=A("CHORD", "1/8", 1, 0.45, 18, 127), arp_on=True, pat=13)
    b.add("TROMBONE", "brass", (o("8", "SAW", 100), o("16", "SAW", 70, 5), o("8", "SQR", 50, -5)),
          cutoff=302, emph=2.5, cntr=co(2.0), kbd=60, fenv=(60, 400, 55), env=(50, 300, 118, 220), glide=90, oct=-1,
          vib=22, mosc=True, lfo=(5.0, "SIN", 1000, "OFF"),
          send=(10, 0, 15, 45), rv=("HALL", 65, 70), pat=4)
    b.add("FLUGEL", "brass", (o("8", "TRI", 100), o("4", "SAW", 30), osc_off()),
          cutoff=428, emph=2.1, cntr=co(1.2), kbd=70, fenv=(40, 400, 70), env=(40, 300, 118, 300), poly=True, oct=-1,
          vib=18, mosc=True, lfo=(5.4, "SIN", 800, "OFF"),
          send=(0, 60, 0, 60), rv=("HALL", 70, 75), ch=("STEREO", 60, 0.3), pat=5)
    b.add("FANFARE", "brass", (o("8", "SAW", 100), o("4", "SAW", 90), o("16", "SQR", 75)),
          cutoff=1020, emph=2.7, cntr=co(1.6), kbd=80, fenv=(0, 700, 70), env=(12, 400, 120, 450), poly=True, oct=-1,
          send=(55, 40, 30, 75), rv=("HALL", 110, 80), dl=("ANALOG", 375, 30), ch=("STEREO", 50, 0.35), dr=("TUBE", 80, 64), pat=5)


def plucks(b):                           # 60-69
    b.add("PLUCK SAW", "pluck", (o("8", "SAW", 100), o("8", "SAW", 100, 12), o("4", "SAW", 50, -12)),
          cutoff=352, emph=3.3, cntr=co(3.0), kbd=70, fenv=(0, 200, 0), env=(1, 400, 0, 200), poly=True,
          send=(0, 50, 50, 40), rv=("PLATE", 45, 85), dl=("DIGITAL", 250, 40), pat=3)
    b.add("MARIMBA", "pluck", (o("8", "TRI", 110), o("2", "TRI", 35), osc_off()),
          cutoff=1060, emph=0.2, cntr=co(1.5), kbd=100, fenv=(0, 80, 0), env=(1, 500, 0, 250), poly=True,
          send=(0, 0, 25, 45), rv=("ROOM", 55, 80),
          arp=A("UPDN", "1/16", 2, 0.45, 0, 127), arp_on=True, pat=13)
    b.add("KALIMBA", "pluck", (o("4", "TRI", 100), o("2", "TRI", 25, 516), osc_off()),
          cutoff=1470, emph=0.8, cntr=co(1.0), kbd=100, fenv=(0, 200, 0), env=(1, 500, 0, 400), poly=True,
          send=(0, 30, 70, 60), rv=("PLATE", 70, 95), dl=("TAPE", 375, 55),
          arp=A("UPDN", "1/8", 2, 0.6, 0, 127), arp_on=True, pat=13)
    b.add("FUNK ARP", "pluck", (o("8", "SAW", 100), o("8", "PW", 70, 10), osc_off()),
          cutoff=316, emph=4.7, cntr=co(3.5), kbd=80, fenv=(30, 200, 0), env=(1, 260, 0, 100), poly=True,
          send=(30, 0, 45, 25), rv=("ROOM", 35, 85), dl=("DIGITAL", 188, 30), dr=("TUBE", 70, 64),
          arp=A("PINKY", "1/16", 2, 0.4, 30, 127), arp_on=True, pat=13)
    b.add("NYLON", "pluck", (o("8", "TRI", 100), o("8", "SQR", 40, 4), o("4", "TRI", 30)),
          cutoff=854, emph=2.3, cntr=co(1.6), kbd=90, fenv=(0, 180, 0), env=(1, 700, 0, 350), poly=True,
          send=(0, 20, 20, 50), rv=("ROOM", 50, 80), pat=3)
    b.add("STEELDRUM", "pluck", (o("8", "TRI", 100), o("4", "TRI", 60), o("4", "TRI", 40, 693)),
          cutoff=1370, emph=2.0, cntr=co(1.2), kbd=100, fenv=(0, 250, 0), env=(1, 600, 0, 300), poly=True,
          send=(0, 40, 50, 55), rv=("ROOM", 70, 85), dl=("ANALOG", 375, 40),
          arp=A("WALK", "1/8", 2, 0.55, 0, 115), arp_on=True, pat=13)
    b.add("8-BIT ARP", "pluck", (o("8", "PW", 100), o("4", "PN", 40), osc_off()),
          cutoff=8470, emph=1.2, cntr=0, kbd=0, fenv=(0, 100, 127), env=(1, 60, 90, 25), poly=True,
          send=(0, 0, 30, 12), rv=("ROOM", 15, 100), dl=("DIGITAL", 188, 30),
          arp=A("UP", "1/16", 3, 0.5, 0, 127), arp_on=True, pat=13)
    b.add("PIZZICATO", "pluck", (o("8", "SAW", 90), o("8", "SAW", 90, 10), osc_off()),
          cutoff=376, emph=2.3, cntr=co(2.4), kbd=70, fenv=(0, 120, 0), env=(1, 220, 0, 120), poly=True,
          send=(0, 60, 25, 50), rv=("HALL", 70, 70), ch=("STEREO", 50, 0.4),
          arp=A("UPDN", "1/8", 2, 0.3, 0, 127), arp_on=True, pat=13)
    b.add("POP PLUCK", "pluck", (o("8", "SAW", 100), o("8", "SQR", 60, -8), o("4", "SAW", 40)),
          cutoff=801, emph=3.1, cntr=co(2.0), kbd=90, fenv=(0, 260, 10), env=(1, 420, 0, 220), poly=True,
          send=(0, 50, 90, 45), rv=("PLATE", 50, 85), dl=("ANALOG", 375, 55),
          arp=A("UP", "1/8", 2, 0.5, 0, 127), arp_on=True, pat=13)
    b.add("HARP", "pluck", (o("8", "TRI", 100), o("4", "TRI", 50), osc_off()),
          cutoff=1290, emph=0.3, cntr=co(1.0), kbd=100, fenv=(0, 250, 0), env=(1, 1400, 0, 700), poly=True,
          send=(0, 50, 40, 80), rv=("HALL", 90, 90), dl=("TAPE", 500, 40),
          arp=A("UP", "1/32", 4, 0.9, 0, 127), arp_on=True, pat=13)


def keys(b):                             # 70-79
    b.add("CLAVINET", "keys", (o("8", "PN", 100), o("8", "PW", 80, 3), o("4", "SQR", 35)),
          cutoff=836, emph=2.5, cntr=co(2.8), kbd=110, fenv=(0, 160, 10), env=(1, 600, 0, 60), poly=True,
          send=(25, 0, 0, 15), rv=("ROOM", 30, 85), dr=("TUBE", 60, 64), pat=6)
    b.add("PIPE ORGAN", "keys", (o("16", "TRI", 80), o("8", "SQR", 95), o("4", "SQR", 70)),
          cutoff=2130, emph=0.5, cntr=0, kbd=100, fenv=(0, 100, 127), env=(18, 100, 127, 80), poly=True,
          send=(0, 25, 0, 100), rv=("HALL", 127, 50), pat=5)
    b.add("SYNTH PIANO", "keys", (o("8", "SAW", 90), o("4", "SQR", 50), o("8", "TRI", 60, 2)), noise=10, pink=True,
          cutoff=688, emph=2.0, cntr=co(3.2), kbd=110, fenv=(0, 300, 0), env=(1, 2500, 0, 300), poly=True,
          send=(0, 25, 0, 45), rv=("PLATE", 60, 85), ch=("STEREO", 25, 0.4), pat=6)
    b.add("HARPSICHORD", "keys", (o("8", "SAW", 90), o("4", "SQR", 70), o("8", "PN", 50, 5)),
          cutoff=1560, emph=2.5, cntr=co(1.2), kbd=100, fenv=(0, 150, 0), env=(1, 1000, 0, 80), poly=True,
          send=(0, 0, 0, 40), rv=("ROOM", 45, 90), pat=3)
    b.add("VIBES", "keys", (o("8", "TRI", 110), o("2", "TRI", 25), osc_off()),
          cutoff=1290, emph=0.3, cntr=co(1.0), kbd=100, fenv=(0, 300, 0), env=(1, 3000, 0, 1200), poly=True,
          lfo=(5.0, "SIN", 0, "OFF"), trem=40,
          send=(0, 30, 0, 70), rv=("PLATE", 70, 90), pat=3)
    b.add("WURLY", "keys", (o("8", "SQR", 95), o("8", "TRI", 75, -5), o("4", "SQR", 25)),
          cutoff=1210, emph=2.3, cntr=co(1.2), kbd=100, fenv=(0, 200, 15), env=(2, 1400, 10, 200), poly=True,
          lfo=(5.5, "SIN", 0, "OFF"), trem=22,
          send=(30, 50, 0, 35), rv=("SPRING", 45, 80), ch=("STEREO", 70, 0.5), dr=("TUBE", 70, 64), pat=6)
    b.add("TUBULAR", "keys", (o("8", "TRI", 100), o("4", "TRI", 60, 552), o("2", "TRI", 40, 516)), noise=6,
          cutoff=2000, emph=0.0, cntr=co(1.0), kbd=100, fenv=(0, 400, 0), env=(1, 5000, 0, 2500), poly=True,
          send=(0, 20, 30, 100), rv=("HALL", 110, 80), pat=7)
    b.add("MUSIC BOX", "keys", (o("2", "TRI", 100), o("4", "SQR", 35), osc_off()),
          cutoff=2900, emph=0.8, cntr=co(1.0), kbd=100, fenv=(0, 200, 0), env=(1, 500, 0, 500), poly=True,
          send=(0, 20, 45, 60), rv=("ROOM", 60, 85), dl=("DIGITAL", 375, 35),
          arp=A("DN", "1/8", 2, 0.7, 0, 127), arp_on=True, pat=7)
    b.add("ACCORDION", "keys", (o("8", "PN", 90), o("8", "PN", 90, 25), o("4", "PW", 60, -12)),
          cutoff=1430, emph=2.5, cntr=co(0.5), kbd=90, fenv=(0, 100, 127), env=(35, 200, 118, 90), poly=True,
          vib=14, mosc=True, lfo=(5.8, "SIN", 0, "OFF"), trem=15,
          send=(0, 30, 0, 30), rv=("ROOM", 50, 80), pat=6)
    b.add("FARFISA", "keys", (o("8", "SQR", 100), o("4", "SQR", 70), o("16", "PW", 40)),
          cutoff=1670, emph=2.9, cntr=co(0.5), kbd=100, fenv=(0, 100, 100), env=(4, 100, 120, 70), poly=True,
          vib=16, mosc=True, lfo=(6.0, "SIN", 300, "OFF"),
          send=(0, 70, 0, 30), rv=("ROOM", 40, 80), ch=("STEREO", 80, 5.5), pat=6)


def atmospheres(b):                      # 80-89: textures and drones
    b.add("DARK DRONE", "atmo", (o("32", "SAW", 100), o("16", "SAW", 100, -9), o("16", "SAW", 80, 15)), noise=15, pink=True,
          cutoff=158, emph=3.5, cntr=co(0.5), kbd=0, fenv=(3000, 4000, 70), env=(2500, 3000, 118, 3200),
          vib=70, mflt=True, lfo=(0.05, "SIN", 0, "OFF"),
          send=(40, 0, 0, 80), rv=("HALL", 127, 30), dr=("TUBE", 40, 64), pat=8)
    b.add("STAR DUST", "atmo", (o("2", "TRI", 90), o("4", "TRI", 70, 5), o("2", "SQR", 30, -7)),
          cutoff=2720, emph=0.0, cntr=0, kbd=100, fenv=(0, 100, 127), env=(3, 900, 0, 900), poly=True,
          send=(0, 50, 70, 110), rv=("CLOUD", 127, 90), dl=("P-PONG", 375, 60), ch=("STEREO", 50, 0.3),
          arp=A("RND", "1/16", 4, 0.4, 0, 95), arp_on=True, pat=13)
    b.add("ALIEN CHOIR", "atmo", (o("8", "PW", 90), o("8", "PN", 90, 23), o("4", "TRI", 50)),
          cutoff=567, emph=5.8, cntr=co(1.0), kbd=50, fenv=(1500, 2000, 60), env=(1500, 1500, 118, 2500), poly=True,
          vib=60, mflt=True, lfo=(0.8, "S&H", 0, "OFF"),
          send=(0, 90, 60, 100), rv=("CLOUD", 120, 60), dl=("REVERSE", 560, 50), pat=5)
    b.add("GLASS HARP", "atmo", (o("4", "TRI", 90), o("4", "TRI", 60, 693), o("2", "TRI", 40, 516)),
          cutoff=3080, emph=2.4, cntr=co(0.5), kbd=100, fenv=(0, 100, 127), env=(1800, 2000, 118, 3500), poly=True,
          vib=20, mosc=True, lfo=(0.4, "SIN", 2000, "OFF"),
          send=(0, 60, 50, 110), rv=("CLOUD", 127, 100), dl=("TAPE", 750, 55), ch=("MULTI", 60, 0.15), pat=5)
    b.add("OCEAN TIDE", "atmo", (osc_off(), osc_off(), osc_off()), noise=100, pink=True,
          cutoff=303, emph=3.3, cntr=co(2.0), kbd=0, fenv=(3500, 4500, 20), env=(3000, 4000, 118, 3200),
          vib=80, mflt=True, lfo=(0.3, "SIN", 0, "2BAR"),
          send=(0, 60, 0, 90), rv=("CLOUD", 120, 45), pat=8)
    b.add("PULSAR", "atmo", (o("8", "SAW", 90), o("8", "SAW", 90, -13), o("4", "TRI", 50)),
          cutoff=555, emph=3.0, cntr=co(1.0), kbd=70, fenv=(0, 600, 70), env=(200, 600, 118, 900), poly=True,
          lfo=(4.0, "SIN", 0, "1/8"), trem=95,
          send=(0, 70, 70, 90), rv=("HALL", 100, 60), dl=("DIGITAL", 375, 55), pat=5)
    b.add("IRON DRONE", "atmo", (o("16", "SAW", 90), o("16", "SAW", 90, 693), o("8", "SQR", 70, -420)),
          cutoff=419, emph=7.3, cntr=co(1.0), kbd=0, fenv=(2500, 3500, 50), env=(2000, 2500, 118, 3200),
          vib=55, mflt=True, lfo=(0.7, "S&H", 0, "OFF"), trem=30,
          send=(70, 0, 50, 70), rv=("HALL", 120, 25), dl=("REVERSE", 700, 50), dr=("TUBE", 40, 70), pat=8)
    b.add("GHOST SEQ", "atmo", (o("8", "PW", 90), o("4", "TRI", 60), osc_off()),
          cutoff=873, emph=3.6, cntr=co(1.8), kbd=70, fenv=(0, 400, 15), env=(5, 900, 15, 900), poly=True,
          send=(0, 60, 80, 100), rv=("CLOUD", 110, 55), dl=("P-PONG", 375, 60),
          arp=A("WALK", "1/8", 3, 0.5, 0, 100), arp_on=True, pat=13)
    b.add("HORROR", "atmo", (o("8", "SAW", 90), o("8", "SAW", 85, 100), o("8", "SAW", 60, 600)),
          cutoff=376, emph=3.6, cntr=co(1.5), kbd=40, fenv=(3000, 4000, 40), env=(2500, 2500, 118, 3500), poly=True,
          vib=80, mosc=True, lfo=(4.5, "SIN", 3000, "OFF"), trem=50,
          send=(30, 50, 40, 100), rv=("HALL", 127, 30), dl=("REVERSE", 560, 50), pat=5)
    b.add("SINGING BOWL", "atmo", (o("8", "TRI", 100), o("4", "TRI", 40, 552), o("8", "TRI", 70, 18)),
          cutoff=1770, emph=0.0, cntr=0, kbd=100, fenv=(0, 100, 127), env=(30, 4500, 0, 3500), poly=True,
          lfo=(2.0, "SIN", 0, "OFF"), trem=25,
          send=(0, 40, 30, 100), rv=("HALL", 120, 85), pat=7)


def sfx(b):                              # 90-99: effects
    b.add("JET SWEEP", "fx", (osc_off(), osc_off(), osc_off()), noise=127,
          cutoff=192, emph=6.6, cntr=co(4.5), kbd=0, fenv=(3500, 3500, 0), env=(1500, 3000, 100, 3000),
          send=(0, 100, 0, 50), rv=("PLATE", 70, 90), ch=("JUNO", 127, 0.2), pat=7)
    b.add("THUNDER", "fx", (o("32", "SQR", 40), osc_off(), osc_off()), noise=127, pink=True,
          cutoff=107, emph=0.6, cntr=co(3.0), kbd=0, fenv=(10, 1800, 0), env=(30, 4500, 0, 2500),
          lfo=(1.8, "S&H", 0, "OFF"), trem=70,
          send=(60, 0, 0, 127), rv=("HALL", 127, 25), dr=("SOFT", 40, 64), pat=7)
    b.add("WIND", "fx", (osc_off(), osc_off(), osc_off()), noise=100, pink=True,
          cutoff=477, emph=3.5, cntr=co(0.5), kbd=0, fenv=(0, 100, 127), env=(1500, 1000, 118, 2500),
          vib=90, mflt=True, lfo=(0.6, "S&H", 0, "OFF"), trem=35,
          send=(0, 50, 0, 70), rv=("HALL", 90, 50), pat=7)
    b.add("SIREN", "fx", (o("8", "SQR", 110), osc_off(), osc_off()),
          cutoff=1470, emph=2.7, cntr=0, kbd=0, fenv=(0, 100, 127), env=(80, 300, 120, 300),
          lfo=(0.45, "TRI", 0, "OFF"), pit=st_pit(7),
          send=(0, 0, 40, 45), rv=("PLATE", 60, 80), dl=("ANALOG", 250, 30), pat=7)
    b.add("TELEPHONE", "fx", (o("4", "TRI", 100), o("4", "TRI", 100, 230), osc_off()),
          cutoff=2740, emph=0.0, cntr=0, kbd=0, fenv=(0, 100, 127), env=(1, 100, 127, 30), poly=True,
          lfo=(20.0, "SQR", 0, "OFF"), trem=127,
          send=(0, 0, 0, 25), rv=("ROOM", 25, 90),
          arp=A("REPEAT", "1/2", 1, 0.75, 0, 127), arp_on=True, pat=7)
    b.add("POWER UP", "fx", (o("8", "TRI", 100), o("4", "SQR", 55), osc_off()),
          cutoff=2270, emph=2.9, cntr=co(1.0), kbd=0, fenv=(0, 100, 127), env=(0, 80, 100, 40), poly=True,
          send=(0, 0, 25, 20), rv=("ROOM", 20, 100), dl=("DIGITAL", 188, 30),
          arp=A("UP", "1/32", 4, 0.8, 0, 127), arp_on=True, pat=13)
    b.add("LASER ZAP", "fx", (o("2", "SAW", 100), o("4", "SAW", 70), osc_off()),
          cutoff=477, emph=4.8, cntr=co(4.5), kbd=0, fenv=(0, 200, 0), env=(0, 220, 0, 100), penv=st_pit(11.8),
          send=(0, 0, 35, 40), rv=("PLATE", 50, 85), dl=("DIGITAL", 188, 40), pat=7)
    b.add("HELICOPTER", "fx", (o("32", "SQR", 100), o("16", "SAW", 60, 0), osc_off()), noise=127,
          cutoff=954, emph=2.8, cntr=0, kbd=0, fenv=(0, 100, 127), env=(300, 500, 118, 600),
          lfo=(8.0, "TRI", 0, "1/16"), trem=110,
          send=(0, 0, 0, 40), rv=("ROOM", 40, 60), pat=7)
    b.add("ROBOT", "fx", (o("8", "SQR", 127), o("8", "SAW", 90, 17), osc_off()),
          cutoff=1010, emph=6.8, cntr=co(2.0), kbd=0, fenv=(0, 80, 0), env=(1, 120, 0, 80), poly=True,
          lfo=(12.0, "S&H", 0, "1/16"), pit=st_pit(5),
          send=(10, 0, 70, 50), rv=("ROOM", 40, 85), dl=("P-PONG", 188, 55),
          arp=A("RND", "1/16", 4, 0.4, 0, 100), arp_on=True, pat=13)
    b.add("UFO", "fx", (o("8", "SQR", 100), o("4", "TRI", 50), osc_off()),
          cutoff=1300, emph=3.7, cntr=0, kbd=0, fenv=(0, 100, 127), env=(400, 500, 118, 1500), glide=250,
          lfo=(6.0, "SIN", 1500, "OFF"), pit=st_pit(9),
          send=(0, 100, 60, 90), rv=("CLOUD", 110, 80), dl=("REVERSE", 560, 50), pat=7)


def build(b):
    showcase(b)
    hard_leads(b)
    soft_leads(b)
    basses(b)
    pads(b)
    brass(b)
    plucks(b)
    keys(b)
    atmospheres(b)
    sfx(b)
    if os.environ.get("FMUG_PARTIAL"):
        while len(b.p) < 100:       # a development filler: the groups still to write
            i = len(b.p)
            b.add("TEMP %02d" % i, "lead", (o("8", "SAW", 100), o("8", "SAW", 100, 11), osc_off()), cutoff=1500, emph=3.0,
                  cntr=co(2), env=(5, 300, 100, 150), poly=(i % 3 == 0), oct=-1 if 50 <= i < 60 else 0, arp_on=(i % 4 == 0))
    return b
