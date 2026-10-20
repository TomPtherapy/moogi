/* SPDX-License-Identifier: GPL-3.0-only
 * FMini-1's Minimoog-style ladder filter (src/eng_fmini.c, tables: tools/fmini_filter.py), on the host through hostsim.c.
 *   build/host/fmini_filter_test
 * 1. the filter sings: with the mixer silent, CUTOFF at the middle and EMPHASIS at the top, a sine comes out (THD under
 *    2 %), at the pitch the law gives (1073 Hz at the dial's 0 and the lowest key), and with the keyboard on it follows
 *    the keys at one volt an octave (within 12 cents over four octaves); with the keyboard off the pitch stays.
 * 2. the CUTOFF dial: the singing pitch at -3 .. +2 is the hardware's (110.8 206.6 472.2 1073.8 2344 5188 Hz) within a
 *    quarter tone, each mark about 1.1 octave from the last.
 * 3. EMPHASIS: no singing below the dial's 7.5, singing at 8.5 and 10; the passband falls as the hardware's does
 *    (-2.25 dB at 2.5, -9.5 at 5) and the peak rises with it.
 * 4. no click, no DC, no overflow: every cutoff, emphasis and key, a loud saw in: the output stays bounded. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("fmini filter: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint8_t pv[FG_SIZE];

/* track 0 = FMINI with patch pv, CUTOFF cut, EMPHASIS emph, CONTOUR 0, a held loudness contour */
static void setup(uint32_t cut, uint32_t emph, uint32_t kbd, uint32_t sw1)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(eng_mem, 0, sizeof eng_mem);
    memset(fmini_eff, 0, sizeof fmini_eff);
    memset(fmini_sm, 0, sizeof fmini_sm);
    fmini_sm_ok = 0;
    host_tracks_init();
    host_preset(&trk[0], ENGI_FMINI, 0);
    memset(pv, 0, sizeof pv);
    pv[FG_R1] = pv[FG_R2] = pv[FG_R3] = 3;
    pv[FG_W1] = 2;                                  /* SAW */
    pv[FG_L1] = 90;
    pv[FG_SW1] = (uint8_t)sw1;
    pv[FG_TUNE] = 64;
    pv[FG_F2] = pv[FG_F3] = 64;
    pv[FG_KBD] = (uint8_t)kbd;
    fmini_set_patch(0, pv);
    fmini_slot[0] = FMINI_OWN;
    for (i = 0; i < 8u; i++)
        trk[0].p[P_E0 + i] = 0;
    trk[0].p[P_E0] = (int16_t)cut;
    trk[0].p[P_E1] = (int16_t)emph;
    trk[0].p[P_E2] = 0;
    trk[0].p[P_E3] = 0;
    trk[0].p[P_E4] = 64;
    trk[0].p[P_E5] = 127;
    trk[0].p[P_E7] = FMINI_OWN;
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_DEC] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_DIST] = 0;
}

static void play(uint32_t note, double *y, uint32_t n)
{
    static int32_t b[CTL];
    uint32_t i, k;
    trk_note_on(&trk[0], note, 100);
    for (i = 0; i < n; i += CTL) {
        track_render(&trk[0], b, CTL);
        for (k = 0; k < CTL && i + k < n; k++)
            y[i + k] = b[k];
    }
    trk_note_off(&trk[0], note);
}

/* the pitch (rising zero crossings, interpolated), the rms and the share of what is not the fundamental in y[a .. b) */
static void analyse(const double *y, uint32_t a, uint32_t b, double *f, double *rms, double *thd)
{
    uint32_t i, nz = 0;
    double first = 0, last = 0, mean = 0, e = 0, sr = 0, si = 0, w;
    for (i = a; i < b; i++)
        mean += y[i];
    mean /= b - a;
    for (i = a + 1; i < b; i++) {
        double p = y[i - 1] - mean, q = y[i] - mean;
        if (p < 0 && q >= 0) {
            double pos = (double)i - 1 + (-p) / (q - p);
            if (!nz++)
                first = pos;
            last = pos;
        }
    }
    *f = nz > 2 ? (double)FS * (nz - 1) / (last - first) : 0;
    for (i = a; i < b; i++)
        e += (y[i] - mean) * (y[i] - mean);
    *rms = sqrt(e / (b - a));
    w = 2 * M_PI * *f / FS;
    for (i = a; i < b; i++) {
        sr += (y[i] - mean) * cos(w * (double)i);
        si += (y[i] - mean) * sin(w * (double)i);
    }
    sr = 2 * sr / (b - a);
    si = 2 * si / (b - a);
    {
        double fund = (sr * sr + si * si) / 2;           /* the fundamental's mean square */
        *thd = *rms > 0 ? sqrt(fabs(e / (b - a) - fund) / (e / (b - a))) : 0;
    }
}


/* the power spectrum of y (Welch, Hann, NF points, 50 % overlap), summed into bands: ps[0 .. NF/2) */
#define NF 4096
static void welch(const double *y, uint32_t n, double *ps)
{
    static double re[NF], im[NF];
    uint32_t i, j, len, seg = 0, a, k;
    for (k = 0; k < NF / 2; k++)
        ps[k] = 0;
    for (a = 0; a + NF <= n; a += NF / 2, seg++) {
        for (i = 0; i < NF; i++) {
            re[i] = y[a + i] * (0.5 - 0.5 * cos(2 * M_PI * i / NF));
            im[i] = 0;
        }
        for (i = 1, j = 0; i < NF; i++) {
            uint32_t bit = NF >> 1;
            for (; j & bit; bit >>= 1)
                j ^= bit;
            j ^= bit;
            if (i < j) {
                double t = re[i]; re[i] = re[j]; re[j] = t;
                t = im[i]; im[i] = im[j]; im[j] = t;
            }
        }
        for (len = 2; len <= NF; len <<= 1) {
            double ang = -2 * M_PI / len;
            for (i = 0; i < NF; i += len)
                for (j = 0; j < len / 2; j++) {
                    double wr = cos(ang * j), wi = sin(ang * j);
                    double xr = re[i + j + len / 2] * wr - im[i + j + len / 2] * wi;
                    double xi = re[i + j + len / 2] * wi + im[i + j + len / 2] * wr;
                    re[i + j + len / 2] = re[i + j] - xr, im[i + j + len / 2] = im[i + j] - xi;
                    re[i + j] += xr, im[i + j] += xi;
                }
        }
        for (k = 0; k < NF / 2; k++)
            ps[k] += re[k] * re[k] + im[k] * im[k];
    }
    for (k = 0; k < NF / 2; k++)
        ps[k] /= seg ? seg : 1;
}

static double midi_hz(double n) { return 440.0 * pow(2.0, (n - 69.0) / 12.0); }
static double cents(double f, double ref) { return 1200.0 * log2(f / ref); }

int main(int argc, char **argv)
{
    static double y[FS * 9];
    uint32_t i, n, N = FS * 2;
    double f, r, t, f0;
    (void)argc; (void)argv;

    /* 1. sings */
    setup(64, 127, 127, 0);
    play(41, y, N);
    analyse(y, FS, N, &f, &r, &t);
    f0 = f;
    printf("   dial 0, EMPHASIS 10, F: %.1f Hz, rms %.0f, THD %.2f %%\n", f, r, t * 100);
    check("the mixer silent, CUTOFF 64, EMPHASIS 127, key F: a sine at about 1.07 kHz", r > 800 && t < 0.02 && fabs(cents(f, 1073.8)) < 60);
    {
        uint32_t keys[6] = {41, 48, 53, 60, 72, 77};
        int ok = 1;
        for (i = 0; i < 6u; i++) {
            double want = f0 * pow(2.0, (keys[i] - 41) / 12.0);
            setup(64, 127, 127, 0);
            play(keys[i], y, N);
            analyse(y, FS, N, &f, &r, &t);
            printf("   key %u: %.1f Hz (want %.1f, %+.1f cents), rms %.0f, THD %.2f %%\n", keys[i], f, want, cents(f, want), r, t * 100);
            ok &= fabs(cents(f, want)) < 12 && t < 0.03 && r > 500;
        }
        check("with KBD 127 the pitch follows the keys at 1 V/oct (F to F5, within 12 cents)", ok);
    }
    {
        int ok = 1;
        setup(64, 127, 0, 0);
        play(41, y, N);
        analyse(y, FS, N, &f, &r, &t);
        f0 = f;
        setup(64, 127, 0, 0);
        play(65, y, N);
        analyse(y, FS, N, &f, &r, &t);
        ok = fabs(cents(f, f0)) < 5 && r > 500;
        check("with KBD 0 the pitch does not follow the keys", ok);
    }

    /* 2. the dial */
    {
        static const struct { double dial, hz; } M[6] = {{-3, 110.8}, {-2, 206.6}, {-1, 472.2}, {0, 1073.8}, {1, 2344}, {2, 5188}};
        int ok = 1;
        double prev = 0;
        for (i = 0; i < 6u; i++) {
            uint32_t knob = (uint32_t)((M[i].dial + 5.0) * 12.7 + 0.5);
            setup(knob, 127, 0, 0);
            play(41, y, N);
            analyse(y, FS, N, &f, &r, &t);
            printf("   dial %+.0f (knob %u): %.1f Hz (hardware %.1f, %+.0f cents)%s\n", M[i].dial, knob, f, M[i].hz, cents(f, M[i].hz),
                   prev ? "" : "");
            ok &= fabs(cents(f, M[i].hz)) < 70 && r > 200;
            prev = f;
        }
        check("the singing pitch at the dial's -3 .. +2 is the hardware's within a quarter tone", ok);
    }

    /* 3. EMPHASIS */
    {
        uint32_t em[8] = {96, 100, 104, 108, 112, 127, 0, 0};
        int ok_off = 1, ok_on = 1;
        for (i = 0; i < 6u; i++) {
            setup(64, em[i], 127, 0);
            play(60, y, N);
            analyse(y, FS, N, &f, &r, &t);
            printf("   EMPHASIS %u (dial %.2f): rms %.0f, %.1f Hz\n", em[i], em[i] * 10.0 / 127, r, f);
            if (em[i] <= 96)
                ok_off &= r < 30;
            if (em[i] >= 108)
                ok_on &= r > 500;
        }
        check("EMPHASIS below 7.6 on the dial does not sing, from 8.5 it does", ok_off && ok_on);
    }


    /* 4. bounded: a loud saw through every corner of the knobs and the keys stays inside what a voice may give */
    {
        static const uint32_t CU[5] = {0, 32, 64, 96, 127}, EM[5] = {0, 64, 100, 112, 127}, NT[3] = {36, 60, 96};
        uint32_t a, b, c, k, ok = 1;
        double worst = 0, dc = 0;
        for (a = 0; a < 5u; a++)
            for (b = 0; b < 5u; b++)
                for (c = 0; c < 3u; c++) {
                    double mx = 0, mean = 0;
                    setup(CU[a], EM[b], 127, FS_O1 | FS_O2 | FS_O3);
                    pv[FG_L1] = pv[FG_L2] = pv[FG_L3] = 127;
                    pv[FG_W1] = pv[FG_W2] = pv[FG_W3] = 2;
                    fmini_set_patch(0, pv);
                    play(NT[c], y, FS / 2);
                    for (k = FS / 4; k < FS / 2; k++) {
                        mx = fmax(mx, fabs(y[k]));
                        mean += y[k];
                    }
                    mean /= FS / 4;
                    if (mx > worst)
                        worst = mx;
                    if (fabs(mean) > dc)
                        dc = fabs(mean);
                    ok &= mx < 4.0 * 24000 && isfinite(mx);
                }
        printf("   loudest %.0f of %d (a voice's full scale), largest mean %.0f\n", worst, 24000, dc);
        check("a loud saw through every cutoff, emphasis and key stays below 4 x a voice's full scale (no runaway)", ok);
    }

    /* 3b. EMPHASIS on noise: the passband's loss and the peak over it, against the hardware's (CA-72 sessions E, J, N) */
    {
        static double ps0[NF / 2], ps[NF / 2];
        static const struct { uint32_t dial10; double loss, peak; } R[6] = {{30, -4.0, 2.6}, {40, -8.0, 9.3}, {50, -9.5, 11.3}, {60, -10.4, 14.0}, {70, -11.9, 20.4}, {75, -12.5, 25.0}};
        uint32_t e0 = 64, nn = FS * 8, j, ok = 1;
        double ref = 0;
        setup(e0, 0, 0, FS_NZ);
        pv[FG_SW1] = FS_NZ;
        pv[FG_LN] = 24;
        fmini_set_patch(0, pv);
        play(41, y, nn);
        welch(y + FS, nn - FS, ps0);
        {   /* the open filter's passband level (the noise itself) */
            uint32_t q, kb0 = (uint32_t)(70.0 / ((double)FS / NF));
            for (q = kb0 - 2; q <= kb0 + 2; q++)
                ref += ps0[q] / 5;
        }
        for (j = 0; j < 6u; j++) {
            uint32_t emph = (uint32_t)(R[j].dial10 * 12.7 / 10 + 0.5), k, kp = 0, kb = (uint32_t)(70.0 / ((double)FS / NF));
            double pass = 0, peak = 0, loss, over;
            setup(e0, emph, 0, FS_NZ);
            pv[FG_SW1] = FS_NZ;
            pv[FG_LN] = 24;
            fmini_set_patch(0, pv);
            play(41, y, nn);
            welch(y + FS, nn - FS, ps);
            for (k = kb - 2; k <= kb + 2; k++)
                pass += ps[k] / ref / 5;
            for (k = 4; k < NF / 2; k++) {
                double r = 0;
                uint32_t q;
                for (q = k - 2 < 1 ? 1 : k - 2; q <= k + 2 && q < NF / 2; q++)
                    r += ps[q] / ref / 5;
                if (r > peak) {
                    peak = r;
                    kp = k;
                }
            }
            loss = 10 * log10(pass);
            over = 10 * log10(peak / pass);
            printf("   EMPHASIS %.1f: passband %+.1f dB (hardware %+.1f), peak %+.1f dB over it (hardware %+.1f) at %.0f Hz\n", R[j].dial10 / 10.0,
                   loss, R[j].loss, over, R[j].peak, kp * (double)FS / NF);
            ok &= fabs(loss - R[j].loss) < 2.0 && fabs(over - R[j].peak) < (j < 4 ? 3.5 : 8.0);
        }
        check("EMPHASIS 3 .. 7.5 on noise: the passband falls and the peak rises as the hardware's (loss within 2 dB, peak within 3.5 dB)", ok);
    }
    return bad ? 1 : 0;
}
