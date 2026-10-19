#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: the preset lab. Renders the factory patches through the real firmware (the browser build) and measures them:
# level, brightness, attack, decay, release, stereo width, the likeness of each patch to the others, and a picture of each group.
#   python3 tools/preset_lab.py [--wasm felucca.wasm] [--out DIR] [--only 0-9,31]   (needs numpy, scipy, matplotlib, node)
# The wasm: WASI_SDK=... OUT=DIR web/emu/build_wasi.sh after tools/gen_fmug_presets.py.
import argparse, json, os, pathlib, subprocess, sys
import numpy as np
from scipy.io import wavfile
from scipy import signal

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gen_fmug_presets as G  # noqa: E402
import fmug_bank  # noqa: E402

FS = 44100
CAT_TARGET = {"bass": -2.0, "atmo": -1.5, "pad": -1.0, "fx": -1.0}
GROUPS = ["SHOWCASE", "HARD LEADS", "SOFT LEADS", "BASSES", "PADS & STRINGS", "SYNTH BRASS", "PLUCKS", "KEYS", "ATMOSPHERES", "EFFECTS"]


def probe(i, x):
    """what to play to judge patch i: (events, duration). Chords for the polyphonic ones, a legato / a second note for the lead"""
    cat, poly = x["cat"], x["poly"]
    arp = x["arp"][0]
    if cat == "bass":
        ev, dur = [[0.1, "on", 40, 100], [2.1, "off", 40], [2.6, "on", 45, 100], [3.4, "off", 45]], 6.0
    elif cat in ("lead",) and not poly:
        ev, dur = [[0.1, "on", 60, 100], [1.5, "on", 67, 100], [1.6, "off", 60], [2.9, "off", 67]], 6.0
    elif cat == "brass":
        ev, dur = ([[0.1, "on", n, 100] for n in (48, 52, 55)] + [[2.1, "off", n] for n in (48, 52, 55)], 6.0) if poly else \
                  ([[0.1, "on", 55, 100], [1.5, "on", 60, 100], [1.6, "off", 55], [2.6, "off", 60]], 6.0)
    elif poly:
        ev, dur = [[0.1, "on", n, 100] for n in (48, 55, 64)] + [[3.6, "off", n] for n in (48, 55, 64)], 9.0
        if cat == "keys":
            ev, dur = [[0.1, "on", n, 100] for n in (48, 55, 64)] + [[2.6, "off", n] for n in (48, 55, 64)], 6.0
    elif cat == "pluck":
        ev, dur = [[0.1, "on", 60, 100], [1.6, "on", 67, 100], [2.8, "off", 60], [3.0, "off", 67]], 6.0
    elif cat in ("atmo", "fx"):
        ev, dur = [[0.1, "on", 48, 100], [4.1, "off", 48]], 9.0
    else:
        ev, dur = [[0.1, "on", 60, 100], [2.6, "off", 60]], 6.0
    if arp and cat in ("bass", "lead", "pluck", "keys", "pad", "atmo"):
        pass
    return ev, dur


def scenarios(bank, only):
    sc = []
    for i, x in enumerate(bank.p):
        if only is not None and i not in only:
            continue
        ev, dur = probe(i, x)
        sc.append(dict(id="%02d" % i, preset=i, events=ev, dur=dur))
    return sc


def db(v):
    return 20 * np.log10(np.maximum(v, 1e-9))


def aweight(f):
    f = np.maximum(f, 1.0)
    ra = (12194 ** 2 * f ** 4) / ((f ** 2 + 20.6 ** 2) * np.sqrt((f ** 2 + 107.7 ** 2) * (f ** 2 + 737.9 ** 2)) * (f ** 2 + 12194 ** 2))
    return 20 * np.log10(ra) + 2.0


def loudness(d, sr, t0, t1):
    """the loudest 0.3 s of the stereo signal between t0 and t1, its frequencies weighted by half an A curve (a bass counts for more than
    a plain A weighting would give it, a bright lead for less): dB re full scale"""
    n = d.shape[0]
    nf = 1 << int(np.ceil(np.log2(n)))
    fr = np.fft.rfftfreq(nf, 1 / sr)
    wt = 10 ** (0.5 * aweight(fr) / 20)
    w = int(0.3 * sr)
    best = -120.0
    xs = [np.fft.irfft(np.fft.rfft(d[:, c], nf) * wt, nf)[:n] for c in range(2)]
    for a in range(int(t0 * sr), min(int(t1 * sr), n - w), int(0.05 * sr)):
        e = 0.5 * sum(np.mean(x[a:a + w] ** 2) for x in xs)
        best = max(best, 10 * np.log10(e + 1e-12))
    return float(best)


def analyse(path, ev):
    sr, d = wavfile.read(path)
    d = d.astype(np.float64)
    L, R = d[:, 0], d[:, 1]
    M, S = (L + R) / 2, (L - R) / 2
    t_on = min(e[0] for e in ev if e[1] == "on")
    t_off = max(e[0] for e in ev if e[1] == "off")
    t_on = t_on - 0.0                                              # (the render starts after the program change)
    i0, i1 = int((t_on + 0.15) * sr), int(t_off * sr)
    i1 = max(i1, i0 + 2048)
    held = np.stack([L[i0:i1], R[i0:i1]])
    peak = max(np.abs(L).max(), np.abs(R).max())
    rms = np.sqrt(np.mean(held ** 2))
    clip = int((np.abs(d) >= 0.999).sum())
    # envelope: 10 ms windows of the mid
    w = int(0.010 * sr)
    n = len(M) // w
    env = np.sqrt((M[: n * w].reshape(n, w) ** 2).mean(1))
    env_db = db(env)
    ton = int(t_on / 0.010)
    pk = env.max()
    att = np.argmax(env[ton:] >= 0.8 * pk) * 0.010 if pk > 0 else 0
    def at(sec):
        k = min(len(env_db) - 1, ton + int(sec / 0.010))
        return float(env_db[k] - db(pk))
    # release: time after the last note-off for the mid envelope to fall 30 dB below the level at that moment
    toff = int(t_off / 0.010)
    ref = env_db[max(toff - 1, 0)]
    rel = next((k * 0.010 for k in range(1, len(env_db) - toff) if env_db[toff + k] < ref - 30), len(env_db) * 0.010 - t_off)
    # spectrum of the held part
    seg = M[i0:i1]
    if len(seg) > 8192:
        seg = seg[: 8192 * (len(seg) // 8192)]
        spec = np.abs(np.fft.rfft(seg.reshape(-1, 8192) * np.hanning(8192), axis=1)).mean(0)
    else:
        spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), 8192))
    fr = np.fft.rfftfreq(8192, 1 / sr)
    cen = float((spec * fr).sum() / (spec.sum() + 1e-12))
    flat = float(np.exp(np.mean(np.log(spec[5:] + 1e-9))) / (np.mean(spec[5:]) + 1e-9))
    low = float((spec[fr < 150] ** 2).sum() / ((spec ** 2).sum() + 1e-12))
    # the mel-ish band shape of the held part (24 log bands 60 Hz .. 12 kHz), dB re its loudest
    edges = np.geomspace(60, 12000, 25)
    bands = np.array([np.sqrt((spec[(fr >= edges[k]) & (fr < edges[k + 1])] ** 2).sum() + 1e-12) for k in range(24)])
    bands = db(bands)
    bands -= bands.max()
    side = db(np.sqrt(np.mean(S[i0:i1] ** 2))) - db(np.sqrt(np.mean(M[i0:i1] ** 2)))
    tail_i = int((t_off + 1.0) * sr)
    tail = db(np.sqrt(np.mean(M[tail_i:tail_i + sr // 2] ** 2))) - db(np.sqrt(np.mean(M[i0:i1] ** 2))) if tail_i + sr // 2 < len(M) else -99
    loud = loudness(d, sr, t_on, t_off + 0.2)
    return dict(loud=loud, peak=float(db(peak)), rms=float(db(rms)), clip=clip, cen=cen, flat=flat, low=low, att=float(att),
                d25=at(0.25), d50=at(0.5), d100=at(1.0), d200=at(2.0), rel=float(rel), side=float(side), tail=float(tail),
                bands=bands.tolist(), env=env_db.tolist())


def feature(m):
    b = np.array(m["bands"])
    return np.concatenate([b / 12.0, [np.log2(m["cen"] + 50) * 2, m["d25"] / 12, m["d100"] / 12, np.log10(m["att"] + 0.003) * 2,
                                      m["side"] / 12, np.log10(m["rel"] + 0.05) * 2]])


def picture(path, items):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    n = len(items)
    fig, axs = plt.subplots((n + 1) // 2, 2, figsize=(15, 2.5 * ((n + 1) // 2)), squeeze=False)
    for ax in axs.ravel():
        ax.axis("off")
    for k, (name, f, m) in enumerate(items):
        ax = axs[k // 2][k % 2]
        ax.axis("on")
        sr, d = wavfile.read(f)
        M = d.astype(np.float64).mean(1)
        fr, tt, Sx = signal.spectrogram(M, sr, nperseg=2048, noverlap=1536, window="hann")
        Sd = 10 * np.log10(Sx + 1e-14)
        ax.pcolormesh(tt, fr, Sd, vmin=Sd.max() - 80, vmax=Sd.max(), cmap="magma", shading="auto")
        ax.set_yscale("symlog", linthresh=500)
        ax.set_ylim(40, 16000)
        ax.set_yticks([100, 500, 2000, 8000])
        ax.set_yticklabels(["100", "500", "2k", "8k"], fontsize=7)
        ax.tick_params(axis="x", labelsize=7)
        env = np.array(m["env"])
        ax2 = ax.twinx()
        ax2.plot(np.arange(len(env)) * 0.010, env, color="cyan", lw=0.7)
        ax2.set_ylim(-70, 6)
        ax2.set_yticks([])
        ax.set_title("%s   pk %.1f  rms %.1f  cen %.0f Hz  att %.0f ms  rel %.2f s  side %.0f dB" %
                     (name, m["peak"], m["rms"], m["cen"], m["att"] * 1000, m["rel"], m["side"]), fontsize=8, loc="left")
    fig.tight_layout()
    fig.savefig(path, dpi=75)
    plt.close(fig)


def parse_only(s):
    if not s:
        return None
    out = set()
    for part in s.split(","):
        if "-" in part:
            a, b = part.split("-")
            out |= set(range(int(a), int(b) + 1))
        else:
            out.add(int(part))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wasm", default=None)
    ap.add_argument("--out", default="/tmp/fmug_lab")
    ap.add_argument("--only", default="")
    ap.add_argument("--pics", action="store_true")
    ap.add_argument("--norender", action="store_true")
    ap.add_argument("--calibrate", action="store_true", help="write tools/fmug_levels.json: the LEVEL that gives each patch the target loudness")
    ap.add_argument("--target", type=float, default=-21.0)
    a = ap.parse_args()
    only = parse_only(a.only)
    bank = fmug_bank.build(G.Bank())
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    sc = scenarios(bank, only)
    if not a.norender:
        (out / "scen.json").write_text(json.dumps(sc))
        subprocess.run(["node", str(HERE / "preset_lab.mjs"), a.wasm, str(out / "scen.json"), str(out / "wav")], check=True)
    M = {}
    for s in sc:
        M[s["id"]] = analyse(out / "wav" / (s["id"] + ".wav"), s["events"])
    (out / "metrics.json").write_text(json.dumps(M))
    print(" id  name          cat    pk    rms  loud clip   cen  flat   low att(ms)  d.25 d.5  d1   d2  rel   side  tail")
    for s in sc:
        x, m = bank.p[s["preset"]], M[s["id"]]
        print("%3s  %-12s %-5s %5.1f %5.1f %5.1f %4d %5.0f %5.2f %4.2f %6.0f %5.0f %4.0f %4.0f %4.0f %5.2f %5.0f %5.0f" % (
            s["id"], x["name"], x["cat"][:5], m["peak"], m["rms"], m["loud"], m["clip"], m["cen"], m["flat"], m["low"], m["att"] * 1000,
            m["d25"], m["d50"], m["d100"], m["d200"], m["rel"], m["side"], m["tail"]))
    if a.calibrate:
        lv = json.loads((HERE / "fmug_levels.json").read_text()) if (HERE / "fmug_levels.json").exists() else {}
        for s in sc:
            x, m = bank.p[s["preset"]], M[s["id"]]
            tgt = a.target + CAT_TARGET.get(x["cat"], 0.0)            # (a bass counts for less than a lead: its fundamental is felt more than heard)
            new = x["level"] + 2.0 * (tgt - m["loud"])               # LEVEL is 0.5 dB a step
            new = min(new, x["level"] + 2.0 * (-3.0 - m["peak"]))     # and never past -3 dBFS peak
            lv[x["name"]] = int(max(1, min(127, round(new))))
        (HERE / "fmug_levels.json").write_text(json.dumps(lv, indent=0, sort_keys=True))
        print("levels ->", HERE / "fmug_levels.json")
    ids = [s["id"] for s in sc]
    F = np.array([feature(M[i]) for i in ids])
    if len(ids) > 3:
        D = np.sqrt(((F[:, None, :] - F[None, :, :]) ** 2).sum(-1))
        np.fill_diagonal(D, 1e9)
        close = []
        for k in range(len(ids)):
            j = int(D[k].argmin())
            if k < j:
                close.append((D[k, j], ids[k], ids[j]))
        close.sort()
        print("\nthe closest pairs (distance; < %.1f is very alike):" % 3.0)
        for d, p, q in close[:14]:
            print("  %.2f  %s %-12s ~ %s %-12s" % (d, p, bank.p[int(p)]["name"], q, bank.p[int(q)]["name"]))
    if a.pics:
        for g in range(10):
            items = []
            for s in sc:
                if s["preset"] // 10 == g:
                    items.append(("%s %s" % (s["id"], bank.p[s["preset"]]["name"]), out / "wav" / (s["id"] + ".wav"), M[s["id"]]))
            if items:
                picture(out / ("group%d.png" % g), items)
                print("picture", out / ("group%d.png" % g), GROUPS[g])


if __name__ == "__main__":
    main()
