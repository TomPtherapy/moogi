/* SPDX-License-Identifier: GPL-3.0-only
 * FMUG (FMini-1): a monosynth in the style of the classic three-oscillator Moog instruments.
 *
 * Signal path (per voice): three oscillators (TRI, SHARK, SAW, SQR, wide and narrow PULSE, band-limited by polyBLEP,
 * ranges LO 32' 16' 8' 4' 2') and a noise source (white or pink) -> the mixer (a level each, the sum saturates softly:
 * the "overload" of the original) -> a 24 dB/oct transistor-ladder low-pass (four one-pole stages, tanh at the input,
 * the resonance fed back around the whole ladder, so EMPHASIS ends in self-oscillation) with its own contour
 * (ATTACK DECAY SUSTAIN; the release takes the DECAY time) and keyboard tracking. The loudness contour is the host's
 * ADSR (P_ATK .. P_REL). Modulation: the track's LFO (or noise, MOD MIX) to the pitch (MOD OSC) and the filter
 * (MOD FLT), its depth E7 MOD (the matrix's MODW source can drive it).
 *
 * The sound is the track's patch (fmug_patch, FG_SIZE bytes, stored in projects and user presets in the slot of the
 * FM6 patch: the track plays one engine at a time, project.c, up_fm6.c); the eight EDIT values are the ones to play
 * with (E0 CUT, E1 EMPH, E2 CNTR the contour's amount, E3 F.ATK, E4 F.DEC, E5 F.SUS, E6 MOD, E7 SLOT 1..100 / OWN).
 * The patch holds the rest (ranges, waves, tuning, mixer, switches, keyboard tracking, TUNE). Edited with the FMUG
 * pages (ui_fmug.c). State per voice: s[0..3] the ladder, s[4] its last output, s[7] the contour (stage << 24 |
 * level, Q23); the noise is the part's (fmug_nbuf: one source, made once a block). */
#define ENGI_FMUG (13u + FELUCCA_SLICE)   /* engines.c ENGINES[] (append-only: after SLICE) */
#if FELUCCA_FMUG_ONLY
#define FMUG_POLY 12             /* FMini-1: POLY mode plays 12 voices */
#else
#define FMUG_POLY 4
#endif
#define FG_SIZE 32u
#ifndef FMUG_KMAX
#define FMUG_KMAX 18400
#endif
#define FMUG_NFACTORY 100u
#define FMUG_OWN FMUG_NFACTORY
#define FMUG_PACKED 128u          /* the slot of an FM6 patch in a project / a user preset (eng_fm6.c) */

enum {                           /* the patch's bytes */
    FG_R1, FG_R2, FG_R3,         /* range: 0 LO, 1 32', 2 16', 3 8', 4 4', 5 2' */
    FG_W1, FG_W2, FG_W3,         /* wave: 0 TRI, 1 SHARK, 2 SAW, 3 SQR, 4 PLS W, 5 PLS N */
    FG_F2, FG_F3,                /* OSC 2 / 3 frequency, 64 = 0, +-7 semitones */
    FG_L1, FG_L2, FG_L3, FG_LN,  /* mixer levels 0..127 (a squared curve) */
    FG_SW1,                      /* FS_ bits */
    FG_SW2,                      /* FT_ bits */
    FG_TUNE,                     /* 64 = A 440, +-100 cents */
    FG_MMIX,                     /* modulation source: 0 LFO .. 127 noise */
    FG_KBD,                      /* keyboard tracking of the cutoff, 0..127 = 0..1 */
    FG_GLID,                     /* glide time kept while the GLIDE switch is off (P_GLIDE holds it while on) */
    FG_NUM
};
enum { FS_O1 = 1, FS_O2 = 2, FS_O3 = 4, FS_NZ = 8, FS_PINK = 16, FS_MOSC = 32, FS_MFLT = 64 };   /* FG_SW1 */
enum { FT_GLIDE = 1, FT_SQR = 2 };                                                                 /* FG_SW2 */

static uint8_t fmug_patch[NTRK][FG_SIZE];        /* the tracks' patches (main loop writes, then fmug_pgen) */
static volatile uint8_t fmug_pgen[NTRK];         /* +1 after each write of fmug_patch[t] */
static uint8_t fmug_slot[NTRK];                  /* the SLOT value last loaded (main loop); 0xFF = none */
static uint8_t fmug_own[NTRK][FG_SIZE];          /* the own patch while SLOT shows a factory one (fmug_poll) */
static uint8_t fmug_own_ok;                      /* bit tr: fmug_own[tr] holds it */
static struct {                                  /* the patch as the audio ISR sees it (taken at a block's start) */
    uint8_t p[FG_SIZE];
    uint8_t gen, ok;
} fmug_eff[NTRK];
static int32_t fmug_nst[NTRK] = {0x2F6E2B1, 0x1A2B3C5, 0x3C4D5E7, 0x0F1E2D3};   /* a part's noise generator */
static int32_t fmug_pk[NTRK][3];                 /* its pink filter */
static int32_t fmug_nbuf[NTRK][CTL];             /* the part's noise for this block, +-32768: one source, all its voices */
static int32_t fmug_mn[NTRK];                    /* the modulation noise of this block, +-16384 */

/* the highest value of each byte */
static uint32_t fmug_max(uint32_t i)
{
    static const uint8_t MAXV[FG_NUM] = {5, 5, 5, 5, 5, 5, 127, 127, 127, 127, 127, 127, 127, 3, 127, 127, 127, 127};
    return i < FG_NUM ? MAXV[i] : 0u;
}

static void fmug_sanitize(uint8_t *v)
{
    uint32_t i;
    for (i = 0; i < FG_SIZE; i++)
        v[i] = i < FG_NUM ? (uint8_t)(v[i] > fmug_max(i) ? fmug_max(i) : v[i]) : 0;
}

/* --------------------------------------------------------- factory patches --- */
#define FGP(r1, r2, r3, w1, w2, w3, f2, f3, l1, l2, l3, ln, s1, s2, tn, mm, kb, gl) \
    {r1, r2, r3, w1, w2, w3, f2, f3, l1, l2, l3, ln, s1, s2, tn, mm, kb, gl}
#define ALLO (FS_O1 | FS_O2 | FS_O3)
#include "eng_fmug_presets.h"          /* FMUG_FACTORY (the 100 patches) and FMUG_PRESETS: tools/gen_fmug_presets.py */

/* --------------------------------------------------------------- the patch --- */
/* track tr's patch = v (FG_SIZE bytes, sanitized). Main loop: the ISR takes it at its next block */
static void fmug_set_patch(uint32_t tr, const uint8_t *v)
{
    uint8_t s[FG_SIZE];
    tr %= NTRK;
    memcpy(s, v, FG_SIZE);
    fmug_sanitize(s);
    memcpy(fmug_patch[tr], s, FG_SIZE);
    RING_PUBLISH();
    fmug_pgen[tr]++;
}

/* SLOT value s < FMUG_NFACTORY: that factory patch into track tr */
static void fmug_load_slot(uint32_t tr, uint32_t s)
{
    s %= FMUG_NFACTORY;
    fmug_set_patch(tr, FMUG_FACTORY[s]);
    fmug_slot[tr % NTRK] = (uint8_t)s;
}

/* track tr's patch was just put in as its own (a project, a user preset, the editor): SLOT shows the factory patch it
 * equals unchanged (F n), else OWN; nothing reloads over it (fmug_poll) */
static void fmug_adopt(uint32_t tr)
{
    track_t *t = &trk[tr % NTRK];
    int32_t s = t->p[P_E7];
    uint32_t k, f = s >= 0 && s < (int32_t)FMUG_NFACTORY ? (uint32_t)s : 0u;
    tr %= NTRK;
    fmug_own_ok &= (uint8_t)~(1u << tr);
    if (t->eng_req == ENGI_FMUG) {
        for (k = 0; k < FMUG_NFACTORY; k++)
            if (!memcmp(FMUG_FACTORY[(f + k) % FMUG_NFACTORY], fmug_patch[tr], FG_SIZE))
                break;
        s = k < FMUG_NFACTORY ? (int32_t)((f + k) % FMUG_NFACTORY) : (int32_t)FMUG_OWN;
        t->p[P_E7] = (int16_t)s;
    }
    fmug_slot[tr] = (uint8_t)s;
}

/* a factory sound load put a SLOT value in (a preset, an engine change): its patch (OWN: the patch stays) */
static void fmug_track_loaded(const track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr < NTRK && t->eng_req == ENGI_FMUG) {
        fmug_own_ok &= (uint8_t)~(1u << tr);
        if (t->p[P_E7] >= 0 && t->p[P_E7] < (int32_t)FMUG_NFACTORY)
            fmug_load_slot(tr, (uint32_t)t->p[P_E7]);
        else
            fmug_adopt(tr);
    }
}

static void fmug_init(void)                      /* power-on: every track F1 */
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++) {
        fmug_set_patch(tr, FMUG_FACTORY[0]);
        fmug_slot[tr] = 0xFFu;
    }
}

/* main loop: SLOT turned (a knob, the editor, MIDI, motion) -> that factory patch; from OWN the own patch is kept
 * aside (fmug_own), and turning back to OWN brings it back */
static void fmug_poll(void)
{
    uint32_t tr, bit;
    for (tr = 0; tr < NTRK; tr++) {
        int32_t s = trk[tr].p[P_E7];
        if (trk[tr].eng_req != ENGI_FMUG || s == fmug_slot[tr])
            continue;
        bit = 1u << tr;
        if (s < 0 || s >= (int32_t)FMUG_NFACTORY) {                                /* OWN */
            if (fmug_own_ok & bit)
                fmug_set_patch(tr, fmug_own[tr]);
            fmug_own_ok &= (uint8_t)~bit;
            fmug_slot[tr] = FMUG_OWN;
            if (s != (int32_t)FMUG_OWN)
                trk[tr].p[P_E7] = FMUG_OWN;
            continue;
        }
        if (fmug_slot[tr] == FMUG_OWN && !(fmug_own_ok & bit)) {
            memcpy(fmug_own[tr], fmug_patch[tr], FG_SIZE);
            fmug_own_ok |= (uint8_t)bit;
        }
        fmug_load_slot(tr, (uint32_t)s);
    }
}

/* once a block and part, before its voices: the ISR's copy of the patch, the noise (one source for all the part's
 * voices, white or pink: Kellet's filter) and the modulation noise */
static void fmug_block(track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk), i;
    const uint8_t *g;
    if (tr >= NTRK)
        return;
    if (!fmug_eff[tr].ok || fmug_eff[tr].gen != fmug_pgen[tr]) {
        fmug_eff[tr].gen = fmug_pgen[tr];
        memcpy(fmug_eff[tr].p, fmug_patch[tr], FG_SIZE);
        fmug_eff[tr].ok = 1;
    }
    for (i = 0; i < NVOICE && !t->v[i].active; i++)
        ;
    if (i == NVOICE)
        return;                                              /* nothing sounds: no noise to make */
    g = fmug_eff[tr].p;
    fmug_mn[tr] = (int32_t)(noise32(&fmug_nst[tr]) >> 17) - 16384;
    if (g[FG_SW1] & FS_NZ) {
        int32_t b0 = fmug_pk[tr][0], b1 = fmug_pk[tr][1], b2 = fmug_pk[tr][2];
        for (i = 0; i < CTL; i++) {
            int32_t w = (int32_t)(noise32(&fmug_nst[tr]) >> 16) - 32768;   /* white, +-32768 */
            if (g[FG_SW1] & FS_PINK) {
                b0 += ((w * 3245) >> 15) - ((b0 * 77) >> 15);              /* 0.0990 in, 0.99765 kept */
                b1 += ((w * 9716) >> 15) - ((b1 * 1212) >> 15);            /* 0.2965 in, 0.963 kept */
                b2 += ((w * 34495) >> 15) - ((b2 * 14090) >> 15);          /* 1.0527 in, 0.57 kept */
                w = (b0 + b1 + b2 + ((w * 6056) >> 15)) >> 1;
            }
            fmug_nbuf[tr][i] = w;
        }
        fmug_pk[tr][0] = b0;
        fmug_pk[tr][1] = b1;
        fmug_pk[tr][2] = b2;
    }
}

/* ------------------------------------------------------------------ voice --- */
static inline int32_t fmug_shark(uint32_t ph)            /* a triangle that rises for 3/4 of the cycle */
{
    uint32_t x = ph >> 16;
    return x < 49152u ? (int32_t)((x * 43691u) >> 15) - 32768 : 32767 - (int32_t)((x - 49152u) * 4u);
}

/* one oscillator of the mix: mix[i] += its sample at level g (Q15); a loop per wave */
static void fmug_wave(uint32_t w, uint32_t *php, uint32_t inc, int32_t g, int32_t *mix, uint32_t n)
{
    uint32_t ph = *php, i;
    switch (w) {
    case 0:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_tri(ph), g);
            ph += inc;
        }
        break;
    case 1:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(fmug_shark(ph), g);
            ph += inc;
        }
        break;
    case 2:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_saw(ph, inc), g);
            ph += inc;
        }
        break;
    case 3:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_pulse(ph, inc, 0x80000000u), g);
            ph += inc;
        }
        break;
    case 4:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_pulse(ph, inc, 0x55555555u), g);
            ph += inc;
        }
        break;
    default:
        for (i = 0; i < n; i++) {
            mix[i] += mulq15(osc_pulse(ph, inc, 0x1C71C71Cu), g);
            ph += inc;
        }
        break;
    }
    *php = ph;
}

/* range of an oscillator in 1/16 semitones (LO: very low) */
static int32_t fmug_range16(uint32_t r)
{
    static const int16_t R[6] = {-48 * 16, -24 * 16, -12 * 16, 0, 12 * 16, 24 * 16};
    return R[r % 6u];
}

static void fmug_note_on(track_t *t, voice_t *v)
{
    if (!v->env && !v->env_out) {                          /* from silence: the ladder and the contour from 0 */
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
        v->s[4] = 0;
        v->s[7] = 0;
        v->s[5] = (int32_t)(noise32(&fmug_nst[(uint32_t)(t - trk) % NTRK]) >> 20) - 2048;   /* the VCOs start a little apart: drift, cents * 256 */
        v->s[6] = (int32_t)(noise32(&fmug_nst[(uint32_t)(t - trk) % NTRK]) >> 20) - 2048;
    }
    v->s[7] = (int32_t)(1u << 24) | (v->s[7] & 0xFFFFFF);  /* the contour attacks from where it is */
}

/* the contour, once a block: stage 1 attack, 2 decay to the sustain, 3 release (the DECAY time); level Q23 */
static int32_t fmug_contour(const int16_t *p, voice_t *v)
{
    uint32_t st = (uint32_t)v->s[7] >> 24;
    int32_t e = v->s[7] & 0xFFFFFF, sus = (int32_t)p[P_E5] << 16, k = (int32_t)ENV_EXP[p[P_E4] & 127];
    if (!v->gate && st && st != 3u)
        st = 3;
    switch (st) {
    case 1:
        {   /* the capacitor charges toward a level above full: a concave rise that ends in a corner, as in the Minimoog */
            int32_t step = (int32_t)(ENV_LIN[p[P_E3] & 127] >> 1), d = (int32_t)(((int64_t)((5 << 21) - e) * step * 824) >> 32);
            e += d < 1 ? 1 : d;
        }
        if (e >= (1 << 23)) {
            e = 1 << 23;
            st = 2;
        }
        break;
    case 2:
        e += mulq16(sus - e, (uint32_t)k);
        break;
    case 3:
        e -= mulq16(e, (uint32_t)k);
        break;
    default:
        break;
    }
    v->s[7] = (int32_t)(st << 24) | (e & 0xFFFFFF);
    return e >> 8;                                          /* Q15 */
}

static void fmug_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const uint8_t *g = fmug_eff[(uint32_t)(t - trk) % NTRK].p;
    int32_t mix[CTL];
    int32_t env15, mod, lfo, cut, p16, fine, k, G16, gain;
    int32_t l1 = 0, l2 = 0, l3 = 0, ln = 0, dr1, dr2, dr3;
    uint32_t tr = (uint32_t)(t - trk) % NTRK, i, sw = g[FG_SW1];
    uint32_t inc1, inc2, inc3;
    int32_t s0 = v->s[0], s1 = v->s[1], s2 = v->s[2], s3 = v->s[3], y4 = v->s[4];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    if (n > CTL)
        n = CTL;
    env15 = fmug_contour(p, v);

    /* modulation: the LFO (a square when FT_SQR) or noise held for the block, MOD deep */
    lfo = mulq15(t->lfo_val, t->lfo_fade);
    if (g[FG_SW2] & FT_SQR)
        lfo = lfo >= 0 ? 32767 : -32767;
    mod = mulq15(lfo, 32767 - (int32_t)g[FG_MMIX] * 258) + mulq15(fmug_mn[tr] << 1, (int32_t)g[FG_MMIX] * 258);
    {   /* the depth: the patch's MOD, and the mod wheel (CC1, t->mw) takes it up to the full depth */
        int32_t depth = p[P_E6] + ((127 - p[P_E6]) * (int32_t)t->mw + 63) / 127;
        mod = mulq15(mod, depth * 258);                                 /* Q15 of the full depth */
    }

    /* oscillator pitches */
    p16 = m->pitch16 + (sw & FS_MOSC ? (mod * 16) >> 14 : 0);    /* full depth: +-2 semitones */
    fine = m->fine + ((int32_t)g[FG_TUNE] - 64) * 237 / 64;
    {   /* the VCOs wander a few cents (temperature, the integrators): a slow random walk per oscillator, about +-2 cents */
        int32_t d1 = v->s[5], d2 = v->s[6];
        d1 += ((int32_t)(noise32(&fmug_nst[tr]) >> 20) - 2048 >> 6) - (d1 >> 11);
        d2 += ((int32_t)(noise32(&fmug_nst[tr]) >> 20) - 2048 >> 6) - (d2 >> 11);
        v->s[5] = d1;
        v->s[6] = d2;
        dr1 = d1 >> 8;
        dr2 = d2 >> 8;
        dr3 = (d1 - d2) >> 9;
    }
    inc1 = cents_inc(clamp(p16 + fmug_range16(g[FG_R1]), 0, 2047), dr1, fine);
    inc2 = cents_inc(clamp(p16 + fmug_range16(g[FG_R2]), 0, 2047), ((int32_t)g[FG_F2] - 64) * 11 + dr2, fine);
    inc3 = cents_inc(clamp(p16 + fmug_range16(g[FG_R3]), 0, 2047), ((int32_t)g[FG_F3] - 64) * 11 + dr3, fine);

    /* mixer levels: a squared curve */
    if (sw & FS_O1) l1 = (int32_t)(g[FG_L1] * g[FG_L1] * 2) + 0;
    if (sw & FS_O2) l2 = (int32_t)(g[FG_L2] * g[FG_L2] * 2);
    if (sw & FS_O3) l3 = (int32_t)(g[FG_L3] * g[FG_L3] * 2);
    if (sw & FS_NZ) ln = (int32_t)(g[FG_LN] * g[FG_LN] * 2);
    for (i = 0; i < n; i++)
        mix[i] = 0;
    if (l1)
        fmug_wave(g[FG_W1], &ph0, inc1, l1, mix, n);
    else
        ph0 += inc1 * n;
    if (l2)
        fmug_wave(g[FG_W2], &ph1, inc2, l2, mix, n);
    else
        ph1 += inc2 * n;
    if (l3)
        fmug_wave(g[FG_W3], &ph2, inc3, l3, mix, n);
    else
        ph2 += inc3 * n;
    if (ln)
        for (i = 0; i < n; i++)
            mix[i] += mulq15(fmug_nbuf[tr][i], ln);

    /* the cutoff: the knob, the contour, the keyboard, the modulation, the host's (LFO / matrix / ENV routings) */
    cut = ((int32_t)p[P_E0] << 8) + m->cutoff;
    cut += (env15 * (int32_t)p[P_E2] / 127) * 70 >> 7;                          /* the contour: up to ~5 octaves */
    cut += ((v->pitch_cur - 60 * 16) * 19 * (int32_t)g[FG_KBD]) >> 7;           /* keyboard tracking */
    if (sw & FS_MFLT)
        cut += (mod * 40) >> 7;
    cut = clamp(cut, 0, 127 << 8);
    {   /* ladder coefficient: g = tan(pi fc / FS) (Q12), the one-pole's G = g / (1 + g) (Q16) */
        int32_t i0 = cut >> 8, gg = SVF_G[i0];
        if (i0 < 127)
            gg += ((SVF_G[i0 + 1] - gg) * (cut & 255)) >> 8;
        G16 = (int32_t)(((uint32_t)gg << 16) / (uint32_t)(4096 + gg));
    }
    k = (int32_t)p[P_E1] * FMUG_KMAX / 127;                         /* the resonance, Q12 (4 * 4096 = the ladder's self-oscillation) */
    k = mulq16(k, (uint32_t)(65536 - G16));                        /* the delayed feedback needs less as the cutoff rises */
    gain = 4096 + (k >> 3);                                       /* the real ladder loses its bass as EMPHASIS rises: only an eighth is given back */

    for (i = 0; i < n; i++) {
        int32_t x = softclip(mix[i]), u, v1, v2, v3, v4;           /* the mixer overloads softly */
        u = softclip(x - ((k * y4) >> 12));                       /* the ladder's input, the feedback around it */
        v1 = mulq16(softclip(u) - s0, (uint32_t)G16);     /* each stage is a differential pair: tanh in front of it */
        u = v1 + s0;
        s0 = u + v1;
        v2 = mulq16(softclip(u) - s1, (uint32_t)G16);
        u = v2 + s1;
        s1 = u + v2;
        v3 = mulq16(softclip(u) - s2, (uint32_t)G16);
        u = v3 + s2;
        s2 = u + v3;
        v4 = mulq16(softclip(u) - s3, (uint32_t)G16);
        y4 = v4 + s3;
        s3 = y4 + v4;
        out[i] += voice_amp((y4 * gain) >> 12, m, i) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = ph2;
    v->s[0] = s0;
    v->s[1] = s1;
    v->s[2] = s2;
    v->s[3] = s3;
    v->s[4] = y4;
}

/* --------------------------------------------------------------- the engine --- */
static const engine_t ENG_FMUG = {
    .name = "FMUG",
    .page_title = {"FILTER", "PATCH"},
    .edit = {
        {"CUT", F_CUTOFF, 0, 127, 64, 0, 0},
        {"EMPH", F_PCT, 0, 127, 40, 0, 0},
        {"CNTR", F_PCT, 0, 127, 64, 0, 0},
        {"F.ATK", F_TIME, 0, 127, 0, 0, 0},
        {"F.DEC", F_TIME, 0, 127, 64, 0, 0},
        {"F.SUS", F_PCT, 0, 127, 40, 0, 0},
        {"MOD", F_PCT, 0, 127, 0, 0, 0},
        {"SLOT", F_INT, 0, FMUG_OWN, 0, 0, 0},
    },
    .presets = FMUG_PRESETS,
    .npresets = NELEM(FMUG_PRESETS),
    .note_on = fmug_note_on,
    .render = fmug_render,
    .knob = {P_E0, P_E1, P_E2, P_E4},
    .poly = FMUG_POLY,
    .keep = 0x13,
    .block = fmug_block,
};
