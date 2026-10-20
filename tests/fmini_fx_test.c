/* SPDX-License-Identifier: GPL-3.0-only
 * FMini-1's FX buses (fx.c, FELUCCA_FMINI_ONLY): the reverb types (stereo, decay, level, stability), the delay and chorus types.
 *   build/host/fmini_fx_test [WAVDIR]       (WAVDIR: demo renders) */
#define UI_TEST_NO_MAIN 1
#define FELUCCA_FMINI_ONLY 1
#include "ui_test.c"
#include "../firmware/src/fmini_app.c"
#include <stdio.h>

static int bad;
static void fxcheck(const char *what, int ok)
{
    printf("fx: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t xs = 0x1234567u;
static int32_t noise(int32_t amp)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return (int32_t)(((int64_t)(int32_t)xs * amp) >> 31);
}
/* a send (c, d, r) -> the stereo wet: L = wet + side, R = wet - side (as mix_block does) */
#define NMAX (36u * 44100u)
static int32_t outL[NMAX], outR[NMAX];
static int32_t cin[NMAX], din[NMAX], rin[NMAX];
static void run_fx(uint32_t n)
{
    static int32_t w[CTL], c[CTL], d[CTL], r[CTL];
    uint32_t t, i;
    for (t = 0; t + CTL <= n; t += CTL) {
        for (i = 0; i < CTL; i++) {
            c[i] = cin[t + i];
            d[i] = din[t + i];
            r[i] = rin[t + i];
            w[i] = 0;
        }
        fx_buses(c, d, r, w, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t s = (hl.side ? rev_side[i] : 0) + (fx.ds_on ? dside[i] : 0);
            outL[t + i] = w[i] + s;
            outR[t + i] = w[i] - s;
        }
    }
}
static void clear_in(void)
{
    memset(cin, 0, sizeof cin);
    memset(din, 0, sizeof din);
    memset(rin, 0, sizeof rin);
}
static void fx_reset(void)
{
    uint32_t gi, k;
    host_tracks_init();
    for (gi = 0; gi < G_COUNT; gi++)
        song.g[gi] = GP[gi].def;
    memset(&fx, 0, sizeof fx);
    rev_clear();
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_REV] = trk[k].p[P_DLY] = trk[k].p[P_CHOR] = 0;
    trk[0].p[P_REV] = 100;
}
static double rms(const int32_t *x, uint32_t a, uint32_t b)
{
    double s = 0;
    uint32_t i;
    for (i = a; i < b; i++)
        s += (double)x[i] * x[i];
    return sqrt(s / (b - a));
}
static double corr(uint32_t a, uint32_t b)
{
    double sl = 0, sr = 0, sx = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        sl += (double)outL[i] * outL[i];
        sr += (double)outR[i] * outR[i];
        sx += (double)outL[i] * outR[i];
    }
    return sx / sqrt(sl * sr + 1e-9);
}
/* RT60 by the Schroeder integral of the left channel (-5 .. -35 dB, x2) */
static double rt60(uint32_t n)
{
    static double e[NMAX];
    double s = 0, t5 = -1, t35 = -1;
    uint32_t i;
    for (i = n; i-- > 0;) {
        s += (double)outL[i] * outL[i] + (double)outR[i] * outR[i];
        e[i] = s;
    }
    for (i = 0; i < n; i++) {
        double db = 10 * log10(e[i] / e[0] + 1e-30);
        if (t5 < 0 && db <= -5)
            t5 = i;
        if (t35 < 0 && db <= -35) {
            t35 = i;
            break;
        }
    }
    return t35 < 0 ? 99 : (t35 - t5) * 2.0 / 44100.0;
}
static void burst(uint32_t len, int32_t amp)         /* a noise burst on the reverb send */
{
    uint32_t i;
    for (i = 0; i < len; i++)
        rin[i] = noise(amp);
}
static void wav(const char *dir, const char *name, uint32_t n)
{
    char p[256];
    FILE *f;
    uint32_t i, dl = n * 4u, v;
    snprintf(p, sizeof p, "%s/%s.wav", dir, name);
    f = fopen(p, "wb");
    fwrite("RIFF", 1, 4, f);
    v = 36u + dl;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    {
        uint16_t h[2] = {1, 2};
        fwrite(h, 2, 2, f);
        v = 44100;
        fwrite(&v, 4, 1, f);
        v = 44100u * 4u;
        fwrite(&v, 4, 1, f);
        h[0] = 4;
        h[1] = 16;
        fwrite(h, 2, 2, f);
    }
    fwrite("data", 1, 4, f);
    fwrite(&dl, 4, 1, f);
    for (i = 0; i < n; i++) {
        int16_t s[2] = {(int16_t)clamp(outL[i] >> 2, -32768, 32767), (int16_t)clamp(outR[i] >> 2, -32768, 32767)};
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

#include "fmini_fx_cases.h"

int main(int argc, char **argv)
{
    fx_cases(argc > 1 ? argv[1] : 0);
    printf(bad ? "fmini_fx_test: FAIL\n" : "fmini_fx_test: ok\n");
    return bad != 0;
}
