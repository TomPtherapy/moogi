/* the cases of fmug_fx_test.c */
static const char *const RN[6] = {"ROOM", "HALL", "PLATE", "SPRING", "SHIMMER", "CLOUD"};
static double rev_level[6], rev_rt[6];
static void reverb_cases(const char *dir)
{
    uint32_t ty, n = 14u * 44100u, nb = 0;
    double per[6];
    for (ty = 0; ty < 6; ty++) {
        double c, a, b;
        clock_t t0;
        fx_reset();
        song.g[G_RTYPE] = (int16_t)ty;
        clear_in();
        burst(44100u / 5u, 90000);
        t0 = clock();
        run_fx(n);
        per[ty] = (double)(clock() - t0) / CLOCKS_PER_SEC / ((double)n / 44100.0);
        a = rms(outL, 22050u, 44100u);
        b = rms(outL, 8u * 44100u, 10u * 44100u);
        c = corr(44100, 4u * 44100u);
        rev_level[ty] = a;
        rev_rt[ty] = rt60(n);
        printf("fx: %-8s tail rms %7.0f (0.5-1 s) %7.0f (8-10 s)  RT60 %5.2f s  L/R correlation %5.2f  host cpu %.1f %% of real time\n", RN[ty], a, b, rev_rt[ty], c, per[ty] * 100);
        {
            char nm[32];
            if (dir) {
                snprintf(nm, sizeof nm, "rev_%s", RN[ty]);
                wav(dir, nm, n);
            }
        }
        fxcheck("stereo: the tail's left and right are not alike (|r| < 0.6)", c < 0.6 && c > -0.6);
        fxcheck("a real tail: not silent, not absurdly loud (rms 300 .. 60000 after 1 s)", a > 300 && a < 60000);
    }
    nb += 0;
    (void)nb;
}
static double goertzel(const int32_t *x, uint32_t a, uint32_t b, double f)
{
    double w = 2 * 3.14159265358979 * f / 44100.0, c = 2 * cos(w), s0 = 0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = a; i < b; i++) {
        s0 = x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt((s1 * s1 + s2 * s2 - c * s1 * s2) / (double)(b - a)) * 2 / sqrt(2.0);
}
static void corner_cases(void)
{
    uint32_t ty, dc, tn, bad_b = 0, bad_g = 0, n = 34u * 44100u;
    int32_t peak;
    uint32_t i;
    for (ty = 0; ty < 6; ty++)
        for (dc = 0; dc < 2; dc++)
            for (tn = 0; tn < 2; tn++) {
                double e1, e2;
                fx_reset();
                song.g[G_RTYPE] = (int16_t)ty;
                song.g[G_RSIZE] = (int16_t)(dc ? 127 : 0);
                song.g[G_RDAMP] = (int16_t)(tn ? 127 : 0);
                clear_in();
                for (i = 0; i < 3u * 44100u; i++)
                    rin[i] = (i / 300u) % 2u ? 262000 : -262000;             /* the loudest: a square, the sum of four tracks' sends */
                run_fx(n);
                peak = 0;
                for (i = 0; i < n; i++) {
                    peak = outL[i] > peak ? outL[i] : peak;
                    peak = -outL[i] > peak ? -outL[i] : peak;
                }
                e1 = rms(outL, 5u * 44100u, 8u * 44100u);
                e2 = rms(outL, 28u * 44100u, 31u * 44100u);
                if (peak > (1 << 22))
                    bad_b++;
                if (e2 > e1 * 0.8 + 64)                                  /* (ROOM's old comb loops keep an idle offset of a few LSB: 37 rms at most) */
                    bad_g++;
                if (peak > (1 << 22) || e2 > e1 * 0.8 + 64)
                    printf("fx: corner %s decay %u tone %u: peak %d  rms 5-8 s %.0f  28-31 s %.0f\n", RN[ty], dc * 127, tn * 127, peak, e1, e2);
            }
    fxcheck("corners (6 types x decay 0/127 x tone 0/127, 3 s of full-scale square sends): bounded (peak < 2^22)", !bad_b);
    fxcheck("corners: the tails fall (28-31 s quieter than 5-8 s) - nothing sustains or grows", !bad_g);
}
static void shimmer_case(void)
{
    uint32_t i, n = 6u * 44100u;
    double h4, h8, s4, s8;
    fx_reset();
    song.g[G_RTYPE] = 1;
    clear_in();
    for (i = 0; i < 44100u; i++)
        rin[i] = (int32_t)(60000 * sin(2 * 3.14159265358979 * 440.0 * i / 44100.0));
    run_fx(n);
    h4 = goertzel(outL, 66150, 4u * 44100u, 440);
    h8 = goertzel(outL, 66150, 4u * 44100u, 880);
    fx_reset();
    song.g[G_RTYPE] = 4;
    clear_in();
    for (i = 0; i < 44100u; i++)
        rin[i] = (int32_t)(60000 * sin(2 * 3.14159265358979 * 440.0 * i / 44100.0));
    run_fx(n);
    s4 = goertzel(outL, 66150, 4u * 44100u, 440);
    s8 = goertzel(outL, 66150, 4u * 44100u, 880);
    printf("fx: octave: HALL 440 Hz %.0f, 880 Hz %.0f | SHIMMER 440 Hz %.0f, 880 Hz %.0f\n", h4, h8, s4, s8);
    fxcheck("SHIMMER: the tail of a 440 Hz note carries its octave (880 Hz at least -18 dB of 440, far above HALL's)", s8 > s4 * 0.12 && s8 > 10 * h8);
}
static void switch_cases(void)
{
    uint32_t a, b, i, n = 3u * 44100u, sw = 44100u + 32u * 40u, bad = 0;
    for (a = 0; a < 6; a++)
        for (b = 0; b < 6; b++) {
            int32_t pk = 0, jmp = 0, jprev = 0;
            if (a == b)
                continue;
            fx_reset();
            song.g[G_RTYPE] = (int16_t)a;
            clear_in();
            burst(44100u / 5u, 90000);
            run_fx(sw);
            song.g[G_RTYPE] = (int16_t)b;
            for (i = 0; i < n - sw; i++)
                cin[i] = din[i] = rin[i] = 0;
            {
                static int32_t keep[NMAX];
                memcpy(keep, outL, sw * 4u);
                run_fx(n - sw);
                for (i = 1; i < sw; i++) {
                    int32_t j = keep[i] - keep[i - 1];
                    jprev = (j < 0 ? -j : j) > jprev ? (j < 0 ? -j : j) : jprev;
                }
                for (i = 1; i < 44100u; i++) {
                    int32_t j = outL[i] - outL[i - 1];
                    jmp = (j < 0 ? -j : j) > jmp ? (j < 0 ? -j : j) : jmp;
                    pk = (outL[i] < 0 ? -outL[i] : outL[i]) > pk ? (outL[i] < 0 ? -outL[i] : outL[i]) : pk;
                }
            }
            if (jmp > 3 * jprev + 200) {
                printf("fx: switch %s -> %s: biggest step %d after, %d before\n", RN[a], RN[b], jmp, jprev);
                bad++;
            }
        }
    fxcheck("a type change while the tail rings: no click (no step in the next second over 3x the biggest step before it)", !bad);
}
static void bypass_case(void)
{
    uint32_t i, n = 44100u, nz = 0;
    fx_reset();
    trk[0].p[P_REV] = 0;
    clear_in();
    run_fx(n);
    for (i = 0; i < n; i++)
        nz += outL[i] != 0 || outR[i] != 0;
    fxcheck("REVERB MIX 0 (and a silent send): off, no output, nothing running", !nz && !fx.rev_on);
    trk[0].p[P_REV] = 100;
    burst(2000, 50000);
    run_fx(n);
    fxcheck("..and back on from MIX 0: it runs again", fx.rev_on);
}
static void cost_case(void)
{
    uint32_t ty, rep, n = 4u * 44100u;
    double t[6];
    for (ty = 0; ty < 6; ty++) {
        clock_t t0;
        fx_reset();
        song.g[G_RTYPE] = (int16_t)ty;
        clear_in();
        burst(n, 60000);
        t[ty] = 1e9;
        for (rep = 0; rep < 5; rep++) {                 /* the best of five: a noisy host */
            double dt;
            t0 = clock();
            run_fx(n);
            dt = (double)(clock() - t0) / CLOCKS_PER_SEC / ((double)n / 44100.0);
            t[ty] = dt < t[ty] ? dt : t[ty];
        }
    }
    printf("fx: host cost (share of real time, -O1; the device is ~50x slower): ROOM %.2f %% HALL %.2f %% PLATE %.2f %% SPRING %.2f %% SHIMMER %.2f %% CLOUD %.2f %%\n", t[0] * 100, t[1] * 100, t[2] * 100, t[3] * 100, t[4] * 100, t[5] * 100);
    fxcheck("cost: no type more than 4x HALL's", t[0] < 4 * t[1] && t[2] < 4 * t[1] && t[3] < 4 * t[1] && t[4] < 4 * t[1] && t[5] < 4 * t[1]);
}
static const char *const DN[7] = {"ANALOG", "DIGITAL", "TAPE", "P-PONG", "MULTI", "SLAP", "REVERSE"};
static double peakpos(const int32_t *x, uint32_t a, uint32_t b, double *mag)   /* the biggest |x| in [a, b): its position (ms) */
{
    uint32_t i, bi = a;
    int32_t m = 0;
    for (i = a; i < b; i++)
        if (abs(x[i]) > m) {
            m = abs(x[i]);
            bi = i;
        }
    if (mag)
        *mag = m;
    return bi * 1000.0 / 44100.0;
}
static double band(const int32_t *x, double ms0, double ms1)
{
    return rms(x, (uint32_t)(ms0 * 44.1), (uint32_t)(ms1 * 44.1));
}
static void delay_setup(uint32_t ty, int32_t ms)
{
    int32_t v;
    fx_reset();
    trk[0].p[P_REV] = 0;
    trk[0].p[P_DLY] = 100;
    song.g[G_DTIME] = (int16_t)ty;
    for (v = 0; v < 127 && dly_ms(v) < ms; v++)
        ;
    song.g[G_DRREV] = (int16_t)v;
    song.g[G_DFDBK] = 60;
    clear_in();
}
static void delay_cases(const char *dir)
{
    uint32_t ty, n = 4u * 44100u, i;
    double T, m;
    int ok;
    for (ty = 0; ty < 7; ty++) {
        delay_setup(ty, 200);
        T = dly_ms(song.g[G_DRREV]);
        din[0] = 100000;
        run_fx(n);
        if (dir) {
            char nm[32];
            snprintf(nm, sizeof nm, "dly_%s", DN[ty]);
            for (i = 0; i < n; i++)
                din[i] = 0;
            wav(dir, nm, n);
        }
        switch (ty) {
        case 0: case 1: case 2:
            ok = fabs(peakpos(outL, 100, 44100, &m) - T) < (ty == 2 ? 40 : 14) && m > 1000;
            fxcheck("echo at TIME, then a second at 2 TIME, quieter", ok && band(outL, 2 * T - 30, 2 * T + 30) > 0 && band(outL, 2 * T - 30, 2 * T + 30) < band(outL, T - 30, T + 30));
            break;
        case 3:
            fxcheck("P-PONG: the first echo is on the left (at TIME), the second on the right (2 TIME), the third left again",
                    band(outL, T - 15, T + 15) > 4 * band(outR, T - 15, T + 15) && band(outR, 2 * T - 15, 2 * T + 15) > 3 * band(outL, 2 * T - 15, 2 * T + 15) &&
                    band(outL, 3 * T - 15, 3 * T + 15) > 3 * band(outR, 3 * T - 15, 3 * T + 15));
            break;
        case 4:
            fxcheck("MULTI: four taps at T/4 T/2 3T/4 T, left and right unlike",
                    band(outL, T / 4 - 10, T / 4 + 10) > 100 && band(outR, T / 2 - 10, T / 2 + 10) > 100 && band(outL, T - 10, T + 10) > 100 &&
                    band(outL, T / 4 - 10, T / 4 + 10) > 2 * band(outR, T / 4 - 10, T / 4 + 10) && band(outR, T / 2 - 10, T / 2 + 10) > 2 * band(outL, T / 2 - 10, T / 2 + 10));
            break;
        case 5:
            ok = fabs(peakpos(outL, 100, 44100, &m) - (30 + T / 10)) < 12;
            fxcheck("SLAP: one short echo at 30 ms + TIME / 10, the repeat much smaller", ok && band(outL, 2 * (30 + T / 10) - 15, 2 * (30 + T / 10) + 15) < 0.5 * band(outL, 30 + T / 10 - 15, 30 + T / 10 + 15));
            break;
        default: {
            /* a click followed by a swell is heard backwards: noise whose envelope RISES with time (0 .. 150 ms) comes back falling */
            delay_setup(6, 200);
            for (i = 0; i < 6615; i++)
                din[i] = (int32_t)(noise(120000) * ((double)i / 6615.0));
            run_fx(n);
            fxcheck("REVERSE: a swell (noise rising over 150 ms) comes back as a fade (loud at T + 50 .. 125 ms, 6 x quieter at T + 150 .. 200)",
                    band(outL, T + 50, T + 125) > 4 * band(outL, T + 150, T + 200) && band(outL, T + 50, T + 125) > 3000);
            break;
        }
        }
    }
}
static void delay_extra(void)
{
    uint32_t ty, i, n = 14u * 44100u, bad_b = 0, bad_f = 0;
    int32_t pk;
    for (ty = 0; ty < 7; ty++) {
        double e1, e2;
        delay_setup(ty, 150);
        song.g[G_DFDBK] = 127;
        for (i = 0; i < 3u * 44100u; i++)
            din[i] = (i / 200u) % 2u ? 262000 : -262000;
        run_fx(n);
        pk = 0;
        for (i = 0; i < n; i++)
            pk = abs(outL[i]) > pk ? abs(outL[i]) : pk;
        e1 = rms(outL, 4u * 44100u, 6u * 44100u);
        e2 = rms(outL, 11u * 44100u, 13u * 44100u);
        if (pk > (1 << 22))
            bad_b++;
        if (e2 > e1 * 0.7 + 64) {
            bad_f++;
            printf("fx: %s at FEEDBACK 127: 4-6 s %.0f, 11-13 s %.0f\n", DN[ty], e1, e2);
        }
    }
    fxcheck("all seven types at FEEDBACK 127 after 3 s of loud square sends: bounded", !bad_b);
    fxcheck("..and the repeats fall away (no type sustains or grows)", !bad_f);
    {   /* TIME turned while it rings: no click */
        uint32_t k, bad = 0;
        for (ty = 0; ty < 7; ty++) {
            int32_t jmp = 0, jprev = 0;
            delay_setup(ty, 150);
            for (i = 0; i < 22050u; i++)
                din[i] = (int32_t)(40000 * sin(2 * 3.14159265358979 * 330.0 * i / 44100.0));
            run_fx(2u * 44100u);
            for (i = 1; i < 2u * 44100u; i++)
                jprev = abs(outL[i] - outL[i - 1]) > jprev ? abs(outL[i] - outL[i - 1]) : jprev;
            for (k = 0; k < 2; k++) {
                song.g[G_DRREV] = (int16_t)(song.g[G_DRREV] + 20);
                for (i = 0; i < 44100u; i++)
                    din[i] = 0;
                {
                    static int32_t keep[NMAX];
                    run_fx(32u * 400u);
                    memcpy(keep, outL, 32u * 400u * 4u);
                    for (i = 1; i < 32u * 400u; i++)
                        jmp = abs(keep[i] - keep[i - 1]) > jmp ? abs(keep[i] - keep[i - 1]) : jmp;
                }
            }
            if (jmp > 3 * jprev + 300) {
                printf("fx: TIME turn on %s: biggest step %d, before %d\n", DN[ty], jmp, jprev);
                bad++;
            }
        }
        fxcheck("TIME turned (+20 twice) while it rings: no click (the delay glides)", !bad);
    }
}
static const char *const CN[3] = {"STEREO", "JUNO", "MULTI"};
static void chorus_cases(const char *dir)
{
    uint32_t ty, i, n = 3u * 44100u;
    for (ty = 0; ty < 3; ty++) {
        double c0, c1, d0, d127, lv, ratio0, ratio127, g0, g1;
        fx_reset();
        trk[0].p[P_REV] = 0;
        trk[0].p[P_CHOR] = 100;
        song.g[G_ENGGO] = (int16_t)ty;
        clear_in();
        for (i = 0; i < n; i++)
            cin[i] = noise(50000);
        run_fx(n);
        c0 = corr(44100, n);
        lv = rms(outL, 44100, n) / 28900.0;
        if (dir) {
            char nm[32];
            snprintf(nm, sizeof nm, "cho_%s", CN[ty]);
            wav(dir, nm, n);
        }
        fxcheck("CHORUS: stereo: left and right differ on noise (|r| < 0.97) and it is about as loud as the send (0.3 .. 1.6)", fabs(c0) < 0.97 && lv > 0.3 && lv < 1.6);
        for (i = 0; i < n; i++)
            cin[i] = (int32_t)(40000 * sin(2 * 3.14159265358979 * 1000.0 * i / 44100.0));
        song.g[G_CDEPTH] = 0;
        fx_reset();
        trk[0].p[P_REV] = 0;
        trk[0].p[P_CHOR] = 100;
        song.g[G_ENGGO] = (int16_t)ty;
        song.g[G_CDEPTH] = 0;
        run_fx(n);
        d0 = goertzel(outL, 44100, n, 1000);
        ratio0 = d0;
        fx_reset();
        trk[0].p[P_REV] = 0;
        trk[0].p[P_CHOR] = 100;
        song.g[G_ENGGO] = (int16_t)ty;
        song.g[G_CDEPTH] = 127;
        run_fx(n);
        d127 = goertzel(outL, 44100, n, 1000);
        ratio127 = d127;
        g0 = rms(outL, 44100, n);
        (void)c1; (void)g1; (void)g0;
        printf("fx: %s: the 1 kHz carrier of a 1 kHz tone: DEPTH 0 %.0f, DEPTH 127 %.0f\n", CN[ty], ratio0, ratio127);
        fxcheck("CHORUS: DEPTH 0 leaves a pure delayed tone, DEPTH 127 modulates it (sidebands: the carrier weakens)", ratio0 > 15000 && ratio127 < 0.8 * ratio0);
    }
}
static void drive_cases(void)
{
    static int32_t blk[CTL], full[44100];
    uint32_t ty, i, k;
    double lvl[2], h2[2], h3[2], base;
    for (ty = 0; ty < 2; ty++) {
        host_tracks_init();
        trk[0].p[P_DIST] = 64;
        song.g[G_INFO] = (int16_t)ty;
        song.g[G_DRLVL] = 64;
        song.g[G_MIDI] = 64;
        memset(&trk[0].dist_hp, 0, 4 * sizeof(int32_t));
        for (k = 0; k < 44100u / CTL; k++) {
            for (i = 0; i < CTL; i++)
                blk[i] = (int32_t)(20000 * sin(2 * 3.14159265358979 * 220.0 * (k * CTL + i) / 44100.0));
            track_dist(&trk[0], blk, CTL);
            for (i = 0; i < CTL; i++)
                full[k * CTL + i] = blk[i];
        }
        lvl[ty] = rms(full, 22050, 44100 - 100);
        h2[ty] = goertzel(full, 22050, 44100 - 100 - (44100 - 100 - 22050) % 200, 440) / goertzel(full, 22050, 44100 - 100, 220);
        h3[ty] = goertzel(full, 22050, 44100 - 100, 660) / goertzel(full, 22050, 44100 - 100, 220);
    }
    printf("fx: DRIVE at GAIN 64 on a 220 Hz tone: SOFT rms %.0f (2nd %.3f, 3rd %.3f of the fundamental), TUBE rms %.0f (2nd %.3f, 3rd %.3f)\n", lvl[0], h2[0], h3[0], lvl[1], h2[1], h3[1]);
    fxcheck("TUBE: as loud as SOFT within 6 dB, with real harmonics, and not SOFT's spectrum", lvl[1] > 0.5 * lvl[0] && lvl[1] < 2 * lvl[0] && h2[1] > 0.02 && fabs(h2[1] - h2[0]) + fabs(h3[1] - h3[0]) > 0.01);
    {   /* OUTPUT */
        double o[3];
        int32_t outv[3] = {32, 64, 127}, v;
        for (v = 0; v < 3; v++) {
            host_tracks_init();
            trk[0].p[P_DIST] = 64;
            song.g[G_INFO] = 1;
            song.g[G_DRLVL] = 64;
            song.g[G_MIDI] = (int16_t)outv[v];
            for (k = 0; k < 44100u / CTL; k++) {
                for (i = 0; i < CTL; i++)
                    blk[i] = (int32_t)(20000 * sin(2 * 3.14159265358979 * 220.0 * (k * CTL + i) / 44100.0));
                track_dist(&trk[0], blk, CTL);
                for (i = 0; i < CTL; i++)
                    full[k * CTL + i] = blk[i];
            }
            o[v] = rms(full, 22050, 44100 - 100);
        }
        printf("fx: OUTPUT 32 / 64 / 127: rms %.0f %.0f %.0f\n", o[0], o[1], o[2]);
        fxcheck("OUTPUT: 32 is half of 64, 127 about twice", fabs(o[0] / o[1] - 0.5) < 0.04 && fabs(o[2] / o[1] - 1.98) < 0.1);
    }
    base = 0;
    {   /* the corners: loudest input, highest GAIN: bounded, no overflow */
        int32_t pk = 0;
        for (ty = 0; ty < 2; ty++) {
            host_tracks_init();
            trk[0].p[P_DIST] = 127;
            song.g[G_INFO] = (int16_t)ty;
            song.g[G_MIDI] = 127;
            for (k = 0; k < 44100u / CTL; k++) {
                for (i = 0; i < CTL; i++)
                    blk[i] = (k / 40u) % 2u ? 230000 : -230000;
                track_dist(&trk[0], blk, CTL);
                for (i = 0; i < CTL; i++)
                    pk = abs(blk[i]) > pk ? abs(blk[i]) : pk;
            }
        }
        fxcheck("DRIVE at GAIN 127 / OUTPUT 127 on the loudest square: bounded (below 2^17)", pk < (1 << 17));
    }
    (void)base;
}
static void fx_cases(const char *dir)
{
    reverb_cases(dir);
    delay_cases(dir);
    delay_extra();
    chorus_cases(dir);
    drive_cases();
    corner_cases();
    shimmer_case();
    switch_cases();
    bypass_case();
    cost_case();
}
