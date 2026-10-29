/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * FMini-1: the whole user interface of the FMINI-only firmware (FELUCCA_FMINI_ONLY=1). Felucca's menu, layers, sequencer
 * pages and the rest of its screens are not reachable: main.c calls fa_input / fa_leds / fa_draw instead of
 * ui_input / ui_leds / ui_draw, and fa_splash instead of draw_splash. The sound engine, the FX, MIDI, USB and the
 * storage are Felucca's.
 *
 * The screen is the approved panel (a 240 x 240 version of the 320 x 240 prototype): wood sides, the tabs
 * CTRL OSC MIX FILT F.ENV A.ENV FX, four fluted knobs with a silver cap and a 0..10 scale, the sliding-box switches
 * (CTRL red, MIX blue, FX orange), the waveform boxes, the filter and contour graphs, the patch number and name
 * bottom left and "n/7" small bottom right.
 *
 * Controls: KNOB 1..4 the page's four knobs. SELECT moves a highlight over every control of every page (the knobs, the
 * switches, the waveforms; past the last control of a page it goes on to the next); ALGORITHM changes the highlighted
 * control (a switch: right = on, left = off). PRESETS: the factory patch F1..F8. Buttons: FX, ENV (F.ENV, again
 * A.ENV), LFO (CTRL; held, MONO / POLY: moogi m5), EDIT (OSC), SCL (MIX), GLO (FILT) open a page; OCT- / OCT+ the keys' octave. The keys, MIDI
 * and the MASTER pot play and sound as in Felucca. Track 1 is the instrument; the other tracks are muted.
 * Included by felucca.c before main.c. */
#if FELUCCA_FMINI_ONLY
#define FA_RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define FA_OR FA_RGB(0xE0, 0x75, 0x2B)                   /* orange: values, curves, the active tab */
#define FA_CR FA_RGB(0xEC, 0xE4, 0xD0)                   /* cream: labels */
#define FA_MU FA_RGB(0x9A, 0x8F, 0x7E)                   /* muted */
#define FA_RED FA_RGB(0xD6, 0x38, 0x2B)
#define FA_BLUE FA_RGB(0x2F, 0x7F, 0xE0)
#define FA_BOX FA_RGB(0x0F, 0x0E, 0x0D)
#define FA_LINE FA_RGB(0x3A, 0x35, 0x30)
#define FA_RULE FA_RGB(0x4A, 0x44, 0x3D)
#define FA_NPAGE 7u

static uint8_t fa_page;                                  /* 0..6 */
static uint8_t fa_env_last;                              /* ENV: the page it opened last (F.ENV or A.ENV) */
static uint32_t fa_sig_prev = 0xFFFFFFFFu;
static uint16_t fa_knob_bmp[38 * 38];                    /* the baked knob: 0 = transparent */
static uint8_t fa_knob_ok;

static const char *const FA_TAB[FA_NPAGE] = {"CTRL", "OSC", "MIX", "FILT", "F.ENV", "A.ENV", "FX"};

/* ---------------------------------------------------------- colours --- */
static uint16_t fa_mix(uint16_t a, uint16_t b, uint32_t t)   /* a + (b - a) * t / 256 per channel */
{
    uint32_t r = ((a >> 11) * (256u - t) + (b >> 11) * t) >> 8, g = (((a >> 5) & 63u) * (256u - t) + ((b >> 5) & 63u) * t) >> 8,
             bl = ((a & 31u) * (256u - t) + (b & 31u) * t) >> 8;
    return (uint16_t)(r << 11 | g << 5 | bl);
}
static uint16_t fa_bg(int32_t y)                         /* the panel's vertical gradient #24211e -> #141211 */
{
    return fa_mix(FA_RGB(0x24, 0x21, 0x1E), FA_RGB(0x14, 0x12, 0x11), (uint32_t)clamp(y, 0, 239) * 256u / 240u);
}
static const uint16_t FA_SILVER[5] = {FA_RGB(0xF4, 0xF4, 0xF2), FA_RGB(0x8D, 0x8D, 0x8B), FA_RGB(0xE2, 0xE2, 0xDF),
                                      FA_RGB(0x6E, 0x6E, 0x6C), FA_RGB(0xCF, 0xCF, 0xCC)};
static uint16_t fa_silver(uint32_t t)                    /* 0..255 along the gradient (stops .3 .55 .8) */
{
    static const uint8_t ST[5] = {0, 77, 140, 204, 255};
    uint32_t i = 0;
    while (i < 3u && t > ST[i + 1])
        i++;
    return fa_mix(FA_SILVER[i], FA_SILVER[i + 1], (t - ST[i]) * 256u / (uint32_t)(ST[i + 1] - ST[i]));
}

/* the angle of (dx, dy): 0..1023 for a turn, 0 up, clockwise (about 4 degrees at worst) */
static uint32_t fa_ang(int32_t dx, int32_t dy)
{
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, t, a;
    if (!ax && !ay)
        return 0;
    if (ay >= ax) {                                       /* up / down sectors: 0 .. 128 from the vertical */
        t = ax * 128 / ay;
        a = t;
        if (dy > 0)
            a = 512 - a;                                  /* down: 512 - */
    } else {
        t = ay * 128 / ax;
        a = 256 - t;                                      /* right: 256 - ; */
        if (dy > 0)
            a = 256 + t;
    }
    if (dx < 0)
        a = 1024 - a;
    return (uint32_t)a & 1023u;
}

/* the knob, baked once: shadow, the fluted skirt, the plate, the black ring, the brushed silver cap */
static void fa_bake_knob(void)
{
    int32_t x, y;
    for (y = 0; y < 38; y++)
        for (x = 0; x < 38; x++) {
            int32_t dx = 2 * x + 1 - 38, dy = 2 * y + 1 - 38, r2 = dx * dx + dy * dy, sh2 = dx * dx + (dy - 2) * (dy - 2);
            uint32_t ang = fa_ang(dx, dy), ph = (ang * 12u) & 1023u;
            int32_t wob = (int32_t)(ph < 512u ? ph : 1023u - ph) * 3 / 512 - 1;   /* the 12 flutes: -1 .. +2 half pixels */
            int32_t rs = 33 + wob;                                                 /* the skirt, in half pixels */
            uint16_t c = 0;
            if (sh2 <= (rs + 2) * (rs + 2))
                c = FA_RGB(0x0B, 0x0A, 0x09);                                      /* the shadow */
            if (r2 <= rs * rs) {
                int32_t hx = dx + 7, hy = dy + 13, d2 = hx * hx + hy * hy;          /* the highlight, up and left */
                uint32_t t = (uint32_t)clamp(d2 * 256 / (50 * 50), 0, 255);
                c = fa_mix(FA_RGB(0x3D, 0x39, 0x35), FA_RGB(0x0C, 0x0B, 0x0A), t);
                if (r2 > (rs - 2) * (rs - 2))
                    c = FA_RGB(0x04, 0x04, 0x04);                                   /* its black edge */
            }
            if (r2 <= 28 * 28)
                c = r2 > 26 * 26 ? FA_RGB(0x2B, 0x28, 0x25) : FA_RGB(0x0D, 0x0C, 0x0B);   /* the plate and its rim */
            if (r2 <= 20 * 20)
                c = FA_RGB(0x08, 0x08, 0x08);                                       /* the black ring */
            if (r2 <= 19 * 19) {                                                    /* the silver cap */
                uint32_t t = (uint32_t)clamp((dx + dy + 38) * 255 / 76, 0, 255), br = (ang * 28u * 4u) & 1023u;
                c = fa_silver(t);
                if (br < 40u && r2 > 3 * 3)
                    c = fa_mix(c, 0, 40);                                           /* the brushing */
                if (r2 > 17 * 17)
                    c = fa_mix(c, 0xFFFF, 60);                                      /* the lit rim */
            }
            fa_knob_bmp[y * 38 + x] = c ? (uint16_t)(c | 0x0841u) : 0u;
        }
    fa_knob_ok = 1;
}
static void fa_knob_draw(int32_t cx, int32_t cy)
{
    int32_t x, y;
    if (!fa_knob_ok)
        fa_bake_knob();
    for (y = 0; y < 38; y++)
        for (x = 0; x < 38; x++) {
            uint16_t c = fa_knob_bmp[y * 38 + x];
            if (c)
                cv_pset(cx - 19 + x, cy - 19 + y, c);
        }
}

/* ------------------------------------------------------------- text --- */
static int32_t fa_cap_y(const aafont_t *f) { return f == &AF_L ? AF_L_CAP_Y : f == &AF_N ? AF_N_CAP_Y : f == &AF_X ? AF_X_CAP_Y : f == &AF_S ? AF_S_CAP_Y : AF_M_CAP_Y; }
static int32_t fa_cap_h(const aafont_t *f) { return f == &AF_L ? AF_L_CAP_H : f == &AF_N ? AF_N_CAP_H : f == &AF_X ? AF_X_CAP_H : f == &AF_S ? AF_S_CAP_H : AF_M_CAP_H; }
/* text with its capitals starting at row ytop, centred on cx */
static void fa_ctext(int32_t cx, int32_t ytop, const aafont_t *f, const char *s, uint16_t fg)
{
    cv_text_on(cx - text_w(f, s) / 2, ytop - fa_cap_y(f), f, s, fg, fa_bg(ytop));
}
static void fa_ltext(int32_t x, int32_t ytop, const aafont_t *f, const char *s, uint16_t fg)
{
    cv_text_on(x, ytop - fa_cap_y(f), f, s, fg, fa_bg(ytop));
}
static void fa_rtext(int32_t xr, int32_t ytop, const aafont_t *f, const char *s, uint16_t fg)
{
    cv_text_r(xr, ytop - fa_cap_y(f), f, s, fg, fa_bg(ytop));
}

/* a polyline of points (x, y in 1/100 of w, h; y up) in the box x, y, w, h */
static void fa_poly(const int16_t *pt, uint32_t n, int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    uint32_t i;
    for (i = 0; i + 1u < n; i++)
        cv_line_t(x + pt[2 * i] * w / 100, y + h / 2 - pt[2 * i + 1] * h / 200, x + pt[2 * i + 2] * w / 100,
                  y + h / 2 - pt[2 * i + 3] * h / 200, c, 2);
}
static const int16_t FA_WAVE[6][12] = {                 /* TRI SHARK SAW SQR PULSE W PULSE N: x 0..100, y -100..100 */
    {0, 0, 25, 100, 75, -100, 100, 0, -1, -1, -1, -1},
    {0, -100, 40, 100, 60, -20, 100, -100, -1, -1, -1, -1},
    {0, 0, 50, 100, 50, -100, 100, 0, -1, -1, -1, -1},
    {0, -100, 0, 100, 50, 100, 50, -100, 100, -100, -1, -1},
    {0, -100, 0, 100, 33, 100, 33, -100, 100, -100, -1, -1},
    {0, -100, 0, 100, 12, 100, 12, -100, 100, -100, -1, -1}};
static const uint8_t FA_WAVE_N[6] = {4, 4, 4, 5, 5, 5};

/* ------------------------------------------------------------ scene --- */
static void fa_num(char *s, int32_t ratio)               /* 0..1000 -> "0.0" .. "10.0" */
{
    ratio = clamp(ratio, 0, 1000);
    fmt_int(s, ratio / 100);
    s += str_len(s);
    *s++ = '.';
    *s++ = (char)('0' + ratio % 100 / 10);
    *s = 0;
}

/* a knob's number: its ratio as 0.0 .. 10.0, except the FILTER's CUTOFF and EMPHASIS, which keep their dial text (param_format:
 * the cutoff -5.0 .. +5.0, 0 in the middle; the emphasis 0.0 .. 10.0) */
static void fa_val(char *s, int32_t ratio, const fmgf_t *f)
{
    if (f->kind == FK_PAR && (f->a == P_E0 || f->a == P_E1))
        return;
    fa_num(s, ratio);
}

/* moogi m6: a knob column at any height (the drum screen's smaller one: cy, the label's row ylab, the value's yval, no K1..K4
 * when kn_on is 0); fa_knob_col is the page's, as before */
static void fa_knob_col_at(uint32_t pg, uint32_t i, int32_t cx, int hot, int32_t cy, int32_t ylab, int32_t yval, int kn_on)
{
    const char *lb, *un;
    char val[16];
    int32_t ratio, k, n = 11, a;
    uint32_t ic;
    const fmgf_t *f = fmg_field(pg, i);
    uint16_t lab = hot ? FA_OR : FA_CR;
    fmg_card_at(pg, i, -1, &lb, val, &un, &ratio, &ic);
    n = f->kind == FK_NAME && f->b == FN_RANGE ? 6 : 11;
    for (k = 0; k < n; k++) {                              /* the scale: ticks, then the numerals or the ranges */
        int32_t ai = (-135 + 270 * k / (n - 1) + 360) % 360 * 1024 / 360, sn = SINE[ai & 1023], cs = SINE[(ai + 256) & 1023];
        cv_line_t(cx + sn * 22 / 32768, cy - cs * 22 / 32768, cx + sn * 25 / 32768, cy - cs * 25 / 32768, FA_CR, 1);
        if (n == 6 ? (k == 0 || k == 5) : (k == 0 || k == 5 || k == 10)) {
            char t[8];
            if (n == 6)
                str_cpy(t, FMG_RANGE[k], 8);
            else if (f->kind == FK_PAR && f->a == P_E0) {       /* the cutoff's dial: -5 0 +5 */
                t[0] = k == 10 ? '+' : 0;
                fmt_int(t + (k == 10), k - 5);
            } else
                fmt_int(t, k);
            fa_ctext(cx + sn * 31 / 32768, cy - cs * 31 / 32768 - 3, &AF_X, t, FA_MU);
        }
    }
    if (hot)                                               /* the cursor: an orange ring round the skirt */
        for (k = 0; k < 64; k++)
            cv_pset(cx + SINE[k * 16 & 1023] * 21 / 32768, cy - SINE[(k * 16 + 256) & 1023] * 21 / 32768, FA_OR);
    fa_knob_draw(cx, cy);
    a = (ratio < 0 ? 0 : ratio) * 270 / 1000 + 225;        /* the pointer: a dot between the cap and the skirt */
    {
        int32_t ai = a % 360 * 1024 / 360, px = cx + SINE[ai & 1023] * 12 / 32768, py = cy - SINE[(ai + 256) & 1023] * 12 / 32768;
        cv_rect(px - 1, py - 1, 3, 3, FA_RGB(0xF6, 0xF1, 0xE3));
    }
    fa_ctext(cx, ylab, &AF_X, f->lb, lab);
    if (f->kind == FK_NAME && f->b == FN_RANGE) {
        str_cpy(val, FMG_RANGE[clamp((int32_t)fmg_byte(f->a), 0, 5)], 16);
    } else {
        fa_val(val, ratio, f);
    }
    fa_ctext(cx, yval, &AF_S, val, FA_OR);
    if (kn_on) {
        char kn[5] = {'K', (char)('1' + i), 0, 0, 0};
        if (pg == 6u)                                      /* FX: the parameter each knob sets (the effect is the label above) */
            str_cpy(kn, i == 3u ? "GAIN" : "MIX", 5);
        fa_ctext(cx, 127, &AF_X, kn, FA_RGB(0x5D, 0x55, 0x4A));
    }
}
static void fa_knob_col(uint32_t pg, uint32_t i, int32_t cx, int hot) { fa_knob_col_at(pg, i, cx, hot, 84, 38, 112, 1); }

static void fa_switch(uint32_t pg, uint32_t ex, int32_t cx, int hot)
{
    const fmgf_t *f = fmg_field(pg, 4u + ex);
    int on = fmg_sw_on(f), x = cx - 15, y = pg == 0u ? 154 : 163;   /* CTRL: higher, no ON / OFF under it (VOICES below) */
    uint16_t col = pg == 0u ? FA_RED : pg == 2u ? FA_BLUE : on ? FA_OR : FA_RGB(0xD9, 0xD2, 0xC0);
    if (hot)
        cv_frame(x - 3, y - 3, 36, 20, FA_OR);
    cv_rect(x, y, 31, 14, FA_RGB(0x4A, 0x44, 0x3D));
    cv_rect(x + 1, y + 1, 29, 12, FA_RGB(0x0D, 0x0C, 0x0B));
    cv_rect(on ? x + 15 : x + 2, y + 2, 14, 10, col);
    fa_ctext(cx, pg == 0u ? 141 : 150, &AF_X, f->lb, hot ? FA_OR : FA_CR);
    if (pg != 0u)
        fa_ctext(cx, 182, &AF_X, on ? "ON" : "OFF", on ? (pg == 2u ? FA_BLUE : FA_OR) : FA_MU);
}

/* CTRL's VOICES: MONO | POLY, the chosen half filled */
static void fa_voices(int hot)
{
    int poly = TSEL->p[P_VOICE] == V_POLY;
    int32_t x = 60, y = 184, ty = y + 1 + (13 - AF_X_CAP_H) / 2 - AF_X_CAP_Y;   /* the cap height centred in the 13 rows */
    const uint16_t dk = FA_RGB(0x0D, 0x0C, 0x0B), ink = FA_RGB(0x1A, 0x0F, 0x06), bg = FA_RGB(0xD9, 0xD2, 0xC0);   /* beige */
    if (hot)
        cv_frame(x - 2, y - 2, 124, 19, bg);                 /* the cursor: a second beige border */
    cv_rect(x, y, 120, 15, bg);
    cv_rect(x + 1, y + 1, 118, 13, dk);
    cv_rect(poly ? x + 60 : x + 1, y + 1, 59, 13, bg);
    cv_text_on(x + 30 - text_w(&AF_X, "MONO") / 2, ty, &AF_X, "MONO", poly ? FA_MU : ink, poly ? dk : bg);
    cv_text_on(x + 90 - text_w(&AF_X, "POLY") / 2, ty, &AF_X, "POLY", poly ? ink : FA_MU, poly ? bg : dk);
}

static void fa_graph(uint32_t pg)
{
    int32_t k[4], x, i;
    uint32_t j;
    int16_t pt[12];
    const char *lb, *un;
    char val[16];
    uint32_t ic;
    int32_t ratio;
    for (j = 0; j < 4u; j++) {
        fmg_card_at(pg, j, -1, &lb, val, &un, &ratio, &ic);
        k[j] = ratio < 0 ? 0 : ratio;                       /* 0..1000 */
    }
    cv_rect(14, 146, 212, 46, FA_LINE);
    cv_rect(15, 147, 210, 44, FA_BOX);
    if (pg == 3u) {                                         /* the ladder's response: FMINI_GRAPH, the cutoff's singing pitch moving across it */
        int32_t fo = FMINI_CUT[clamp((int32_t)TSEL->p[P_E0], 0, 127)], e = clamp((int32_t)TSEL->p[P_E1], 0, 127), er, ef, px = 0, py = 0, xs;
        er = e >= 120 ? 15 : e >> 3;                        /* between two of the table's 17 emphasis rows (0, 8 .. 120, 127) */
        ef = e >= 120 ? (e - 120) * 256 / 7 : (e & 7) * 32;
        xs = 17 + (fo - 24 * 16) * 206 / (112 * 16);        /* the singing pitch: a dotted line (the screen: MIDI 24 .. 136) */
        for (i = 0; i < 44; i += 4)
            cv_pset(xs, 148 + i, FA_MU);
        for (i = 0; i <= 206; i += 2) {
            int32_t c = (((24 * 16 + i * (112 * 16) / 206) - fo) * 256 / 24) + 48 * 256, ix, fr, a, b, db, y;
            c = clamp(c, 0, 71 * 256);
            ix = c >> 8;
            fr = c & 255;
            a = FMINI_GRAPH[er][ix] + (((FMINI_GRAPH[er][ix + 1] - FMINI_GRAPH[er][ix]) * fr) >> 8);
            b = FMINI_GRAPH[er + 1][ix] + (((FMINI_GRAPH[er + 1][ix + 1] - FMINI_GRAPH[er + 1][ix]) * fr) >> 8);
            db = (a + (((b - a) * ef) >> 8)) / 8;
            y = 190 - (clamp(db, -50, 30) + 50) / 2;        /* -50 .. +30 dB over the 40 rows */
            x = 17 + i;
            if (i)
                cv_line_t(px, py, x, y, FA_OR, 2);
            px = x;
            py = y;
        }
        fa_ltext(18, 198, &AF_X, "24 DB/OCT LADDER LOW PASS", FA_MU);
    } else {                                                /* a contour: attack, decay, sustain, release */
        int32_t a = 5 + k[0] * 25 / 1000, dd = 5 + k[1] * 30 / 1000, su = k[2] * 2 / 10 - 100;
        pt[0] = 0; pt[1] = -100;
        pt[2] = (int16_t)a; pt[3] = 100;
        pt[4] = (int16_t)(a + dd); pt[5] = (int16_t)su;
        pt[6] = (int16_t)(a + dd + 20); pt[7] = (int16_t)su;
        pt[8] = (int16_t)(a + dd + 35); pt[9] = -100;
        fa_poly(pt, 5, 19, 150, 202, 38, FA_OR);
        fa_ltext(18, 198, &AF_X, "ATTACK DECAY SUSTAIN", FA_MU);
    }
}

/* a smaller knob (a scale of ticks, no numerals) for a control that has no K slot: its label above, its value below */
static void fa_knob_mini(uint32_t pg, uint32_t i, int32_t cx, int32_t cy, int hot)
{
    const char *lb, *un;
    char val[16];
    int32_t ratio, k, a;
    uint32_t ic;
    const fmgf_t *f = fmg_field(pg, i);
    fmg_card_at(pg, i, -1, &lb, val, &un, &ratio, &ic);
    for (k = 0; k < 11; k++) {
        int32_t ai = (-135 + 270 * k / 10 + 360) % 360 * 1024 / 360, sn = SINE[ai & 1023], cs = SINE[(ai + 256) & 1023];
        cv_line_t(cx + sn * 22 / 32768, cy - cs * 22 / 32768, cx + sn * 25 / 32768, cy - cs * 25 / 32768, FA_CR, 1);
    }
    if (hot)
        for (k = 0; k < 64; k++)
            cv_pset(cx + SINE[k * 16 & 1023] * 21 / 32768, cy - SINE[(k * 16 + 256) & 1023] * 21 / 32768, FA_OR);
    fa_knob_draw(cx, cy);
    a = (ratio < 0 ? 0 : ratio) * 270 / 1000 + 225;
    {
        int32_t ai = a % 360 * 1024 / 360, px = cx + SINE[ai & 1023] * 12 / 32768, py = cy - SINE[(ai + 256) & 1023] * 12 / 32768;
        cv_rect(px - 1, py - 1, 3, 3, FA_RGB(0xF6, 0xF1, 0xE3));
    }
    fa_ctext(cx, 138, &AF_X, lb, hot ? FA_OR : FA_CR);
    if (f->kind != FK_NAME && (f->kind != FK_GLB || GP[f->a].fmt == F_PCT))
        fa_val(val, ratio, f);
    fa_ctext(cx, 201, &AF_S, val, FA_OR);
}

/* a 3 x 5 pixel font for the faders' small labels (A..Z): three bits a row, the top row first */
static const uint16_t FA_TINY[26] = {
    0x2BED, 0x6BAE, 0x3923, 0x6B6E, 0x79A7, 0x79A4, 0x396B, 0x5BED, 0x7497, 0x126A, 0x5BAD, 0x4927, 0x5FED,
    0x6B6D, 0x2B6A, 0x6BA4, 0x2B73, 0x6BAD, 0x388E, 0x7492, 0x5B6F, 0x5B6A, 0x5BFD, 0x5AAD, 0x5A92, 0x72A7,
};
/* the 4..5 x 6 glyphs of the letters the faders use (a '#' is ink), top row first; other letters: the 3 x 5 font above */
static const char *const FA_TINY6[][7] = {
    {"A", ".##.", "#..#", "#..#", "####", "#..#", "#..#"},
    {"C", ".###", "#...", "#...", "#...", "#...", ".###"},
    {"D", "###.", "#..#", "#..#", "#..#", "#..#", "###."},
    {"E", "####", "#...", "###.", "#...", "#...", "####"},
    {"I", "###", ".#.", ".#.", ".#.", ".#.", "###"},
    {"M", "#...#", "##.##", "#.#.#", "#...#", "#...#", "#...#"},
    {"N", "#..#", "##.#", "##.#", "#.##", "#.##", "#..#"},
    {"O", ".##.", "#..#", "#..#", "#..#", "#..#", ".##."},
    {"R", "###.", "#..#", "###.", "#.#.", "#..#", "#..#"},
    {"T", "#####", "..#..", "..#..", "..#..", "..#..", "..#.."},
    {"Y", "#...#", "#...#", ".#.#.", "..#..", "..#..", "..#.."},
};
static int32_t fa_tiny_w(char ch)
{
    uint32_t k;
    for (k = 0; k < NELEM(FA_TINY6); k++)
        if (FA_TINY6[k][0][0] == ch)
            return (int32_t)str_len(FA_TINY6[k][1]);
    return 3;
}
static void fa_tiny_text(int32_t cx, int32_t y, const char *s, uint16_t fg)
{
    int32_t w = -1, x, r, c;
    const char *p;
    for (p = s; *p; p++)
        w += fa_tiny_w(*p) + 1;
    x = cx - w / 2;
    for (p = s; *p; p++) {
        uint32_t k;
        int32_t gw = fa_tiny_w(*p);
        for (k = 0; k < NELEM(FA_TINY6); k++)
            if (FA_TINY6[k][0][0] == *p) {
                for (r = 0; r < 6; r++)
                    for (c = 0; c < gw; c++)
                        if (FA_TINY6[k][1 + r][c] == '#')
                            cv_pset(x + c, y + r, fg);
                break;
            }
        if (k == NELEM(FA_TINY6) && *p >= 'A' && *p <= 'Z') {
            uint32_t g = FA_TINY[*p - 'A'];
            for (r = 0; r < 5; r++)
                for (c = 0; c < 3; c++)
                    if ((g >> (14 - (r * 3 + c))) & 1u)
                        cv_pset(x + c, y + r, fg);
        }
        x += gw + 1;
    }
}

/* the minimal horizontal fader of the FX page (DECAY / TIME / RATE / TONE): a small label, a thin track filled up to
 * the value, the value below */
static void fa_fader(uint32_t pg, uint32_t i, int32_t cx, int hot)
{
    const char *lb, *un;
    char val[16];
    int32_t ratio, x = cx - 22, y = 168, fw;
    uint32_t ic;
    const fmgf_t *f = fmg_field(pg, i);
    fmg_card_at(pg, i, -1, &lb, val, &un, &ratio, &ic);
    fw = (ratio < 0 ? 0 : ratio) * 42 / 1000;
    if (hot)
        cv_frame(x - 3, y - 4, 50, 15, FA_OR);
    cv_rect(x, y, 44, 7, FA_LINE);
    cv_rect(x + 1, y + 1, 42, 5, FA_BOX);
    cv_rect(x + 1, y + 1, fw, 5, FA_OR);
    cv_rect(x + 1 + fw - (fw > 1 ? 1 : 0), y - 1, 2, 9, FA_RGB(0xF6, 0xF1, 0xE3));     /* the cap */
    fa_tiny_text(cx, 152, lb, hot ? FA_OR : FA_RGB(0xA6, 0x9C, 0x88));
    if (f->kind != FK_NAME && (f->kind != FK_GLB || GP[f->a].fmt == F_PCT))
        fa_val(val, ratio, f);
    else if (f->a == G_CRATE)                              /* RATE in Hz */
        str_cpy(val + str_len(val), "Hz", 4);
    fa_ctext(cx, 186, &AF_S, val, FA_OR);
}

/* the FX page's EDIT button under each effect: a beige frame, a dark inside, EDIT; the cursor an orange frame */
static void fa_edit_btn(int32_t cx, int hot)
{
    const uint16_t dk = FA_RGB(0x0D, 0x0C, 0x0B), bg = FA_RGB(0xD9, 0xD2, 0xC0);
    int32_t x = cx - 22, y = 156, ty = y + 1 + (19 - AF_X_CAP_H) / 2 - AF_X_CAP_Y;
    if (hot)
        cv_frame(x - 3, y - 3, 50, 25, FA_OR);
    cv_rect(x, y, 44, 21, bg);
    cv_rect(x + 1, y + 1, 42, 19, dk);
    cv_text_on(cx - text_w(&AF_X, "EDIT") / 2, ty, &AF_X, "EDIT", hot ? FA_OR : FA_CR, dk);
}

/* one knob of an FX pop-up: a scale of ticks, the dial and its pointer, the label above and the value below */
static void fa_pop_knob(uint32_t pg, uint32_t i, int32_t cx, int32_t cy, int hot)
{
    const char *lb, *un;
    char val[16];
    int32_t ratio, k, a;
    uint32_t ic;
    const fmgf_t *f = fmg_field(pg, i);
    fmg_card_at(pg, i, -1, &lb, val, &un, &ratio, &ic);
    for (k = 0; k < 11; k++) {
        int32_t ai = (-135 + 270 * k / 10 + 360) % 360 * 1024 / 360, sn = SINE[ai & 1023], cs = SINE[(ai + 256) & 1023];
        cv_line_t(cx + sn * 22 / 32768, cy - cs * 22 / 32768, cx + sn * 25 / 32768, cy - cs * 25 / 32768, FA_CR, 1);
    }
    if (hot)
        for (k = 0; k < 64; k++)
            cv_pset(cx + SINE[k * 16 & 1023] * 21 / 32768, cy - SINE[(k * 16 + 256) & 1023] * 21 / 32768, FA_OR);
    fa_knob_draw(cx, cy);
    a = (ratio < 0 ? 0 : ratio) * 270 / 1000 + 225;
    {
        int32_t ai = a % 360 * 1024 / 360, px = cx + SINE[ai & 1023] * 12 / 32768, py = cy - SINE[(ai + 256) & 1023] * 12 / 32768;
        cv_rect(px - 1, py - 1, 3, 3, FA_RGB(0xF6, 0xF1, 0xE3));
    }
    fa_ctext(cx, cy - 42, &AF_X, f->lb, hot ? FA_OR : FA_CR);
    if (f->kind == FK_PAR || (f->kind == FK_GLB && GP[f->a].fmt == F_PCT))
        fa_val(val, ratio, f);
    fa_ctext(cx, cy + 32, &AF_S, val, FA_OR);
    {
        char kn[4] = {'K', (char)('1' + i), 0, 0};
        fa_ctext(cx, cy + 50, &AF_X, kn, FA_RGB(0x5D, 0x55, 0x4A));
    }
}
static void fa_fx_dialog(void)
{
    uint32_t pg = 6u + fmg_fx, i;
    const uint16_t bgd = FA_RGB(0x1B, 0x19, 0x17);
    cv_rect(12, 42, 216, 164, FA_OR);
    cv_rect(14, 44, 212, 160, bgd);
    fa_ctext(120, 50, &AF_S, FMG_PAGE[pg].name, FA_CR);
    cv_rect(20, 66, 200, 1, FA_RULE);
    for (i = 0; i < 4u; i++)
        fa_pop_knob(pg, i, 39 + (int32_t)i * 54, 112, fmg_cur[pg] == i);
    fa_ctext(120, 190, &AF_X, "K1-K4  SELECT  ALGO  FX=NEXT", FA_MU);   /* (moogi m8: no TAP: the delay follows the tempo) */
}

static void fa_wave_boxes(uint32_t pg, uint32_t cur)
{
    static const char *const WV[6] = {"TRI", "SHARK", "SAW", "SQR", "PULSE W", "PULSE N"};
    uint32_t j;
    for (j = 0; j < 3u; j++) {                             /* each box sits under its oscillator's knob (the columns of fa_scene) */
        int32_t cx = 36 + (int32_t)j * 56, x = cx - 26, w = clamp((int32_t)fmg_byte(FG_W1 + j), 0, 5);
        int hot = cur == 4u + j;
        cv_rect(x, 146, 52, 36, hot ? FA_OR : FA_LINE);
        cv_rect(x + 1, 147, 50, 34, FA_BOX);
        fa_poly(FA_WAVE[w], FA_WAVE_N[w], x + 5, 151, 42, 26, FA_OR);
        fa_ctext(cx, 188, &AF_X, WV[w], hot ? FA_OR : FA_CR);
    }
    fa_knob_mini(pg, 7, 204, 173, cur == 7u);
}

/* ------------------------------------------------------- presets --- */
static uint8_t fa_base;                                   /* the factory patch last loaded (0..99): the one PRESETS goes on from, edited or not (an edit makes SLOT OWN) */
/* the list PRESETS walks: the 8 factory patches, then the user presets of this instrument (SAVE) in slot order */
static uint32_t fa_nuser(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)(up_used(k) && up_engine(k) == ENGI_FMINI);
    return n;
}
static uint32_t fa_user_nth(uint32_t n)                   /* the slot of the n-th user preset */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && up_engine(k) == ENGI_FMINI && !n--)
            return k;
    return 0;
}
static uint32_t fa_user_rank(uint32_t slot)
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)(up_used(k) && up_engine(k) == ENGI_FMINI);
    return n;
}
static int fa_on_user(void)
{
    const track_t *t = TSEL;
    return t->user && up_used((uint32_t)t->user - 1u) && up_engine((uint32_t)t->user - 1u) == ENGI_FMINI;
}

/* SAVE: a small dialog. PRESETS picks the slot U01..U32, SELECT moves along the name and ALGORITHM changes the letter,
 * SAVE writes it, any page button cancels */
static uint8_t fa_sv;                                     /* 1: the dialog is open */
static uint8_t fa_sv_slot, fa_sv_pos;
static char fa_sv_name[13];
static uint32_t fa_msg_until;                             /* "SAVED U05" shown until fm1_ms passes this */
static char fa_msg[16];
static const char FA_CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";

static void fa_sv_fill(void)                              /* the slot's own name (overwrite) or the playing patch's */
{
    uint32_t k = fa_sv_slot;
    if (up_used(k) && up_engine(k) == ENGI_FMINI)
        up_name(k, fa_sv_name);
    else if (fa_on_user())
        up_name((uint32_t)TSEL->user - 1u, fa_sv_name);
    else
        str_cpy(fa_sv_name, ENGINES[ENGI_FMINI]->presets[TSEL->p[P_E7] >= 0 && TSEL->p[P_E7] < (int32_t)FMINI_NFACTORY ? (uint32_t)TSEL->p[P_E7] : fa_base % FMINI_NFACTORY].name, 13);
}
static void fa_sv_open(void)
{
    uint32_t k;
    fa_sv = 1;
    fa_sv_pos = 0;
    if (fa_on_user())
        fa_sv_slot = (uint8_t)(TSEL->user - 1u);
    else {                                                /* the first free slot */
        for (k = 0; k < UP_SLOTS && up_used(k); k++)
            ;
        fa_sv_slot = (uint8_t)(k < UP_SLOTS ? k : 0u);
    }
    fa_sv_fill();
}
static void fa_say(const char *a, uint32_t k)
{
    char l[4];
    up_slot_label(l, k);
    str_cpy(fa_msg, a, 9);
    str_cpy(fa_msg + str_len(fa_msg), l, 4);
    fa_msg_until = fm1_ms + 1500u;
}
static uint8_t fa_last_user;
static void fa_sv_do(void)
{
    uint32_t k = fa_sv_slot, n = str_len(fa_sv_name);
    int rc;
    while (n && fa_sv_name[n - 1u] == ' ')                /* no trailing blanks */
        fa_sv_name[--n] = 0;
    rc = up_store(k, fa_sv_name);
    fa_sv = 0;
    if (rc == 0 || rc == 3) {
        TSEL->user = (uint8_t)(k + 1u);
        fa_last_user = (uint8_t)(k + 1u);
        fa_say(rc == 3 ? "SAVED(RAM) " : "SAVED ", k);
    } else
        str_cpy(fa_msg, "SAVE ERROR", 16), fa_msg_until = fm1_ms + 1500u;
    ui.force = 1;
}
static int fa_msg_on(void) { return fa_msg_until && (int32_t)(fm1_ms - fa_msg_until) < 0; }

static void fa_dialog(void)
{
    uint32_t k = fa_sv_slot, i;
    char l[4], hdr[20];
    int used = up_used(k) && up_engine(k) == ENGI_FMINI;
    const uint16_t bgd = FA_RGB(0x1B, 0x19, 0x17);
    cv_rect(16, 62, 208, 112, FA_OR);
    cv_rect(18, 64, 204, 108, bgd);
    up_slot_label(l, k);
    fa_ctext(120, 72, &AF_S, "SAVE PRESET", FA_CR);
    str_cpy(hdr, l, 4);
    fa_ltext(28, 92, &AF_M, hdr, FA_OR);
    fa_ltext(28 + text_w(&AF_M, hdr) + 8, 95, &AF_X, used ? "OVERWRITE" : "EMPTY", used ? FA_RED : FA_MU);
    for (i = 0; i < 12u; i++) {                           /* the name, letter by letter, the cursor underlined */
        char c[2] = {fa_sv_name[i] ? fa_sv_name[i] : ' ', 0};
        int32_t x = 28 + (int32_t)i * 14;
        cv_rect(x, 124, 13, 1, i == fa_sv_pos ? FA_OR : FA_LINE);
        if (i == fa_sv_pos)
            cv_rect(x, 125, 13, 1, FA_OR);
        if (c[0] != ' ')
            fa_ctext(x + 6, 108, &AF_M, c, i == fa_sv_pos ? FA_OR : FA_CR);
    }
    fa_ctext(120, 140, &AF_X, "PRESETS SLOT   SELECT/ALGO NAME", FA_MU);
    fa_ctext(120, 154, &AF_X, "SAVE = WRITE   ANY PAGE = CANCEL", FA_MU);
}

/* ARP: a pop-up like SAVE's. KNOB 1..4 are MODE, RATE, OCT, GATE; SELECT picks a line (the fifth is the tempo) and
 * ALGORITHM changes it; ARP again, HOME or a page button closes it. The arpeggio goes on after closing */
static uint8_t fa_ar;                                     /* 1: the pop-up is open */
static uint8_t fa_ar_row;                                 /* 0 MODE, 1 RATE, 2 OCT, 3 GATE, 4 HOLD, 5 BPM */
#define FA_AR_N 6u
static const uint8_t FA_AR_ID[5] = {P_AMODE, P_ARATE, P_AOCT, P_AGATE, P_AHOLD};
static const char *const FA_AR_LB[FA_AR_N] = {"MODE", "RATE", "OCTAVES", "GATE", "HOLD", "TEMPO"};

static void fa_ar_turn(uint32_t row, int32_t s)
{
    track_t *t = TSEL;
    if (row < 5u) {
        const param_desc_t *d = track_desc(t, FA_AR_ID[row]);
        int16_t *vp = &t->p[FA_AR_ID[row]];
        *vp = (int16_t)param_turn(d, *vp, s);
        motion_capture(t, FA_AR_ID[row], *vp);
        ml_knob(t, FA_AR_ID[row]);
    } else if (!song.g[G_CLOCK]) {                        /* (moogi m5: the norns' clock: its tempo) */
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + fmg_acc(EN_ALGO, s, 200), GP[G_BPM].min, GP[G_BPM].max);
    }
}
/* a pop-up's frame: the orange border, the title (the same size for every pop-up) */
static void fa_frame(const char *title)
{
    cv_rect(10, 24, 220, 186, FA_OR);
    cv_rect(12, 26, 216, 182, FA_RGB(0x1B, 0x19, 0x17));
    fa_ctext(120, 36, &AF_M, title, FA_CR);
}
/* a line of a pop-up: the label on the left, the value on the right, the highlight under the chosen one */
static void fa_line(int32_t y, const char *lb, const char *val, int hot)
{
    if (hot)
        cv_rect(16, y - 5, 208, 21, FA_RGB(0x2E, 0x28, 0x22));
    fa_ltext(22, y, &AF_M, lb, hot ? FA_OR : FA_CR);
    fa_rtext(218, y, &AF_M, val, hot ? FA_OR : FA_CR);
}
static void fa_ar_dialog(void)
{
    const track_t *t = TSEL;
    uint32_t i;
    fa_frame("ARPEGGIATOR");
    for (i = 0; i < FA_AR_N; i++) {
        char val[16];
        const char *un;
        if (i < 5u)
            param_format(track_desc(t, FA_AR_ID[i]), t->p[FA_AR_ID[i]], val, &un);
        else if (song.g[G_CLOCK])
            str_cpy(val, "USB", 16);                          /* moogi m5: the norns' clock */
        else
            fmt_int(val, song.g[G_BPM]);
        fa_line(62 + (int32_t)i * 22, FA_AR_LB[i], val, i == fa_ar_row);
    }
    fa_ctext(120, 196, &AF_S, "SELECT  ALGO  K1-K4  ARP=CLOSE", FA_MU);
}

/* the ARP button: a tap turns it on / off, held HOLD on / off; the pop-up shows the settings for a few seconds */
static uint8_t fa_arp_down, fa_arp_done, fa_ar_last = 1;
static uint32_t fa_arp_t0, fa_ar_until;
static uint8_t fa_lfo_down;                               /* moogi m5: LFO held, MONO <-> POLY */
static uint32_t fa_lfo_t0;

/* the keys of the notes that sound (the arpeggio, the sequence, MIDI): lit like the keys held */
static uint32_t fa_sound_keys(void)
{
    const track_t *t = &trk[0];
    uint32_t m = 0, i, k;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate)
            for (k = 0; k < 27u; k++)
                if (kb_map(t, k) == t->v[i].note)
                    m |= 1u << k;
    return m;
}

/* ----------------------------------------------------------- GLO --- */
/* GLO opens the global settings pop-up: MIDI IN (USB / MIDI / ALL), CHANNEL (ALL / 1..16), MIDI CC (every
 * knob, switch and wave box of every screen and the ARP's lines, each with its own CC, 0 = none; a CC sent from the
 * controller while the list is open is taken for the highlighted line), PITCH BEND (the range) and ABOUT (the splash
 * with the version).
 * SELECT picks a line, ALGORITHM changes it (or opens MIDI CC / ABOUT); GLO goes back, then closes. The settings are
 * kept in the flash settings record (favorites.factory rows 7 and 8: no engine of this firmware uses them). */
#define FMINI_VERSION "v1.8.0-m9"
#define FA_NPG 44u                                        /* the controls of the seven screens (FMG_PAGE: 8+8+8+4+4+4+8) */
#define FA_NCC (FA_NPG + FA_AR_N)                         /* .. and the ARP pop-up's six lines */
static uint8_t fa_gl;                                     /* 0 closed, 1 the list, 2 MIDI CC, 3 ABOUT, 4 ACID (fmini_seq.c),
                                                           * 5 DRUMS (moogi m7, fmini_drum.c) */
static uint8_t fa_gl_row;                                 /* 0 MIDI IN, 1 CHANNEL, 2 MIDI CC, 3 PITCH BEND, 4 KNOB REC, 5 ACID,
                                                           * 6 DRUMS (moogi m6 DRUM KIT; m7 a page), 7 USB AUDIO (m9), 8 ABOUT */
#define FA_GL_N 9u
/* moogi m9: GLO > USB AUDIO: MIX (0, the stereo master, as before) or 10 CH (1: usb.c, fx.c fa_ul_*: the synth in stereo and the
 * drums' eight lanes, at 44.1 kHz). Settings byte 212; read before USB starts (fa_usb_boot), a change re-enumerates at once */
static uint8_t fa_usb10;
static uint8_t fa_cc_i;                                   /* the control highlighted in MIDI CC */
static uint8_t fa_cc[FA_NCC];                             /* its CC, 0 = none */
static uint8_t fa_cfg_dirty;
static const char *const FA_SRC[3] = {"USB", "MIDI", "ALL"};       /* fm_midi_src 0, 1, 3 (2 was Bluetooth: gone) */
static const char *const FA_BEND[4] = {"+/-2", "+/-12", "+2/-12", "+2/-24"};
static const char *const FA_PG_TAG[8] = {"CTRL", "OSC", "MIX", "FILT", "F.ENV", "A.ENV", "FX", "ARP"};
/* the CC each control answers to out of the box: the usual ones where there is one (5 glide time, 7 volume, 71 emphasis,
 * 73 attack, 74 cutoff, 75 decay, 91 / 93 / 94 sends), the free ones in order for the rest */
static const uint8_t FA_CC0[FA_NCC] = {
    20, 5, 21, 22, 23, 24, 25, 26,                        /* CTRL: TUNE GLIDE MOD MIX LFO RATE, MOD OSC MOD FLT GLIDE LFO SQR */
    27, 28, 29, 30, 31, 33, 34, 62,                       /* OSC: ranges, DETUNE 2, waves, DETUNE 3 */
    35, 36, 37, 39, 40, 41, 42, 43,                       /* MIX: levels, NOISE, switches */
    74, 71, 44, 45,                                       /* FILT */
    46, 47, 48, 49,                                       /* F.ENV */
    73, 75, 50, 7,                                        /* A.ENV: ATTACK DECAY SUSTAIN LEVEL */
    91, 94, 93, 51, 52, 53, 54, 55,                       /* FX: REVERB DELAY CHORUS DRIVE, switches */
    56, 57, 58, 59, 60, 61,                               /* ARP: MODE RATE OCTAVES GATE HOLD TEMPO */
};
static int fa_cc_valid(uint32_t cc) { return cc == 0u || ml_free_cc(cc); }

/* the FX pop-ups' other globals (the settings still keep their bytes; a factory patch, a saved one and the autosave set them) */
static const uint8_t FA_GID2[8] = {G_RTYPE, G_RDAMP, G_DTIME, G_DFDBK, G_ENGGO, G_CDEPTH, G_INFO, G_MIDI};
static uint8_t *fa_cfg_byte(uint32_t i) { return &favorites.factory[7u + i / 32u][i % 32u]; }
static void fa_seq_cfg_load(void);
static void fa_seq_cfg_save(void);
static void fa_dr_cfg_load(void);                         /* moogi m6: the drums (fmini_drum.c) */
static void fa_dr_cfg_save(void);
static void fa_cfg_load(void)
{
    uint32_t i;
    fa_seq_cfg_load();
    fa_dr_cfg_load();
    if (*fa_cfg_byte(0) == 0xA7u && *fa_cfg_byte(1) == 0x4Fu) {
        uint32_t src = *fa_cfg_byte(2), ch = *fa_cfg_byte(3);
        fm_midi_src = (uint8_t)(src == 0u || src == 1u ? src : 3u);   /* (2 was Bluetooth) */
        fm_midi_ch = (uint8_t)(ch > 16u ? 0u : ch);
        fm_bend_mode = (uint8_t)(*fa_cfg_byte(4u + FA_NCC) & 3u);
        /* (the FX settings are not read back: each factory patch carries its own, a saved one and the autosave theirs) */
        fm_bt = 0;
        for (i = 0; i < FA_NCC; i++) {
            uint32_t c = *fa_cfg_byte(4u + i);
            fa_cc[i] = (uint8_t)(fa_cc_valid(c) ? c : FA_CC0[i]);
        }
    } else {
        fm_midi_src = 3;
        fm_midi_ch = 0;
        fm_bend_mode = 0;
        fm_bt = 0;
        memcpy(fa_cc, FA_CC0, sizeof fa_cc);
    }
}
static void fa_cfg_save(void)
{
    uint32_t i;
    if (!fa_cfg_dirty && !fmg_gdirty)
        return;
    fa_cfg_dirty = 0;
    fmg_gdirty = 0;
    *fa_cfg_byte(0) = 0xA7;
    *fa_cfg_byte(1) = 0x4F;
    *fa_cfg_byte(2) = fm_midi_src;
    *fa_cfg_byte(3) = fm_midi_ch;
    for (i = 0; i < FA_NCC; i++)
        *fa_cfg_byte(4u + i) = fa_cc[i];
    *fa_cfg_byte(4u + FA_NCC) = fm_bend_mode;
    *fa_cfg_byte(212) = fa_usb10;                         /* moogi m9: USB AUDIO */
    {
        static const uint8_t GID[4] = {G_RSIZE, G_DRREV, G_CRATE, G_DRLVL};
        for (i = 0; i < 4u; i++)
            *fa_cfg_byte(5u + FA_NCC + i) = (uint8_t)song.g[GID[i]];
        for (i = 0; i < 8u; i++)
            *fa_cfg_byte(9u + FA_NCC + i) = (uint8_t)song.g[FA_GID2[i]];
    }
    fa_seq_cfg_save();
    fa_dr_cfg_save();                                     /* moogi m6: the drums */
    settings_save();                                      /* (deferred while anything plays) */
}
/* moogi m9: USB AUDIO. A change drops the FM-1 off the USB for about a second (usb.c usb_uac10_switch): the computer then
 * finds the other audio input. Saved with the settings (byte 212) */
static void fa_usb_set(uint32_t on)
{
    on = on != 0;
    if (on == fa_usb10)
        return;
    fa_usb10 = (uint8_t)on;
    fa_cfg_dirty = 1;
#if FELUCCA_UAC10
    usb_uac10_switch(on);
#endif
}
static __attribute__((unused)) void fa_usb_boot(void)   /* main.c: the saved USB AUDIO, before usb_start */
{
    fa_usb10 = (uint8_t)(*fa_cfg_byte(0) == 0xA7u && *fa_cfg_byte(1) == 0x4Fu && *fa_cfg_byte(212) == 1u);
#if FELUCCA_UAC10
    usb_uac10_switch(fa_usb10);
#endif
}
static void fa_gl_close(void)
{
    fa_gl = 0;
    fa_cfg_save();
}
static void fa_arp_show(void)
{
    fa_sv = 0;
    fa_gl_close();
    fa_ar = 1;
    fa_ar_until = fm1_ms + 4000u;
}

static uint8_t fa_dr;                                     /* moogi m6: the drum mode is on (fmini_drum.c) */
static void fa_dr_key(uint32_t k);
#include "fmini_seq.c"                                   /* Acid Moogi: the sequencer, the tempo, the acid line */
#include "fmini_drum.c"                                  /* moogi m6: the drum machine on track 2 */

/* control n of the list -> its page and place, its tag and name */
static void fa_ctl_loc(uint32_t n, uint32_t *pg, uint32_t *i)
{
    uint32_t p = 0;
    while (p < 7u && n >= fmg_count(p) - (p == 0u)) {   /* (CTRL's VOICES has no CC: the list keeps its numbers) */
        n -= fmg_count(p) - (p == 0u);
        p++;
    }
    *pg = p;
    *i = n;
}
static void fa_ctl_name(uint32_t n, const char **tag, char *name)
{
    uint32_t pg, i;
    const char *lb;
    if (n >= FA_NPG) {
        *tag = FA_PG_TAG[7];
        str_cpy(name, FA_AR_LB[n - FA_NPG], 16);
        return;
    }
    fa_ctl_loc(n, &pg, &i);
    lb = fmg_field(pg, i)->lb;
    *tag = FA_PG_TAG[pg];
    str_cpy(name, lb, 16);
    if (pg == 1u)
        if (i < 3u)
            str_cpy(name + str_len(name), " RANGE", 7);
        else if (i >= 4u && i < 7u)
            str_cpy(name + str_len(name), " WAVE", 7);
    else if (pg == 2u)
        str_cpy(name + str_len(name), i < 4u ? " LEVEL" : " ON", 7);
    else if (pg == 0u && i == 1u)
        str_cpy(name, "GLIDE TIME", 16);
    else if (pg == 0u && i == 6u)
        str_cpy(name, "GLIDE ON", 16);
    else if (pg == 6u && i >= 4u)
        str_cpy(name, i == 4u ? "REVERB DECAY" : i == 5u ? "DELAY TIME" : i == 6u ? "CHORUS RATE" : "DRIVE TONE", 16);
}

/* a CC value v (0..127) for control n */
static void fa_ctl_apply(uint32_t n, uint32_t v)
{
    uint32_t pg, i;
    const fmgf_t *f;
    track_t *t = TSEL;
    if (n >= FA_NPG) {
        n -= FA_NPG;
        if (n < 5u)
            midi_cc_set(t, FA_AR_ID[n], v);
        else if (!song.g[G_CLOCK])                            /* (moogi m5: not on the norns' clock) */
            song.g[G_BPM] = (int16_t)(40 + (int32_t)v * 200 / 127);
        return;
    }
    fa_ctl_loc(n, &pg, &i);
    if (pg == 6u && i >= 4u) {                            /* the FX page's slots 4..7 kept their CCs: DECAY TIME RATE TONE, now in the pop-ups */
        static const uint8_t POPI[4] = {2, 2, 3, 2};
        pg = 6u + (i - 3u);
        i = POPI[i - 4u];
    }
    f = fmg_field(pg, i);
    switch (f->kind) {
    case FK_PAR:
        midi_cc_set(t, f->a, v);
        break;
    case FK_NAME:
        fmg_write(f->a, v * 6u / 128u);
        break;
    case FK_GLIDE:
        fmg_write(f->a, v);
        {
            uint32_t sw2 = fmg_byte(FG_SW2), want = v ? (sw2 | FT_GLIDE) : (sw2 & ~(uint32_t)FT_GLIDE);
            if (want != sw2)
                fmg_write(FG_SW2, want);
        }
        fmg_glide_sync();
        break;
    case FK_BYTE:
        fmg_write(f->a, v);
        break;
    case FK_GLB: {
        const param_desc_t *d = &GP[f->a];
        int32_t n = d->max - d->min + 1;
        song.g[f->a] = (int16_t)(d->min + (d->fmt == F_ENUM ? (int32_t)v * n / 128 : ((int32_t)v * (n - 1) + 63) / 127));
        fmg_gdirty = 1;
        break;
    }
    default:                                              /* a switch: on from 64 */
        fmg_turn(pg, i, v >= 64u ? 1 : -1, EN_ALGO);
        break;
    }
}
/* the CCs the ISR has noted: applied here (the main loop), or, in the MIDI CC list, taken for the highlighted line */
static void fa_cc_poll(void)
{
    uint32_t w, b, pend[4];
    fm1_irq_off();
    for (w = 0; w < 4u; w++) {
        pend[w] = fm_cc_pend[w];
        fm_cc_pend[w] = 0;
    }
    fm1_irq_on();
    for (w = 0; w < 4u; w++)
        for (b = 0; pend[w] >> b; b++)
            if ((pend[w] >> b) & 1u) {
                uint32_t cc = w * 32u + b, n, v = fm_cc_rx[cc];
                if (fa_gl == 2u) {
                    if (ml_free_cc(cc)) {
                        fa_cc[fa_cc_i] = (uint8_t)cc;
                        fa_cfg_dirty = 1;
                    }
                    continue;
                }
                for (n = 0; n < FA_NCC; n++)
                    if (fa_cc[n] == cc) {
                        fmg_force_page = (int8_t)fa_page;
                        fa_ctl_apply(n, v);
                    }
            }
}

static void fa_gl_dialog(void)
{
    char b[24];
    if (fa_gl == 4u) {
        ac_menu_dialog();
        return;
    }
    if (fa_gl == 5u) {
        fa_dr_menu_dialog();                              /* moogi m7: GLO > DRUMS */
        return;
    }
    if (fa_gl == 1u) {
        fa_frame("GLOBAL");
        fa_line(50, "MIDI IN", FA_SRC[fm_midi_src == 3u ? 2u : fm_midi_src & 1u], fa_gl_row == 0u);
        if (fm_midi_ch)
            fmt_int(b, fm_midi_ch);
        else
            str_cpy(b, "ALL", 4);
        fa_line(66, "MIDI CHANNEL", b, fa_gl_row == 1u);   /* (moogi m9: nine lines, 16 apart) */
        fa_line(82, "MIDI CC", ">", fa_gl_row == 2u);
        fa_line(98, "PITCH BEND", FA_BEND[fm_bend_mode & 3u], fa_gl_row == 3u);
        fa_line(114, "KNOB REC", fa_knob_rec ? "ON" : "OFF", fa_gl_row == 4u);
        fa_line(130, "ACID", ">", fa_gl_row == 5u);
        fa_line(146, "DRUMS", ">", fa_gl_row == 6u);        /* moogi m7: KIT, SIDECHAIN, RELEASE */
        fa_line(162, "USB AUDIO", fa_usb10 ? "10 CH" : "MIX", fa_gl_row == 7u);   /* moogi m9 */
        fa_line(178, "ABOUT", ">", fa_gl_row == 8u);
        fa_ctext(120, 198, &AF_S, "SELECT  ALGO  GLO=BACK", FA_MU);
    } else {                                              /* MIDI CC: a window of six lines on the list */
        uint32_t top = fa_cc_i < 3u ? 0u : fa_cc_i - 3u, k;
        if (top + 6u > FA_NCC)
            top = FA_NCC - 6u;
        str_cpy(b, "MIDI CC  ", 10);
        fmt_int(b + 9, (int32_t)fa_cc_i + 1);
        str_cpy(b + str_len(b), "/", 2);
        fmt_int(b + str_len(b), FA_NCC);
        fa_frame(b);
        for (k = 0; k < 6u; k++) {
            uint32_t n = top + k;
            const char *tag;
            char nm[16], cc[8];
            int32_t y = 58 + (int32_t)k * 21;
            int hot = n == fa_cc_i;
            fa_ctl_name(n, &tag, nm);
            if (fa_cc[n])
                fmt_int(cc, fa_cc[n]);
            else
                str_cpy(cc, "OFF", 4);
            if (hot)
                cv_rect(16, y - 4, 208, 20, FA_RGB(0x2E, 0x28, 0x22));
            fa_ltext(20, y + 3, &AF_X, tag, FA_MU);
            fa_ltext(54, y, &AF_M, nm, hot ? FA_OR : FA_CR);
            fa_rtext(218, y, &AF_M, cc, hot ? FA_OR : FA_CR);
        }
        fa_ctext(120, 190, &AF_S, "ALGO = CC   OR SEND A CC", FA_MU);
    }
}

/* the patch shown: a factory one by its number 00..99 and name, or a saved one (U01..) ; the last factory patch loaded
 * stays shown while its sound is edited (SLOT OWN), so there is always a number */
static int16_t fa_last_sl = -2;                           /* SLOT as last seen: -1 = not a factory patch (edited / saved one) */
static uint8_t fa_primed;                                 /* the first frame has been seen: no pop-up at power-on */
static uint32_t fa_pp_until;                              /* the preset pop-up shows until fm1_ms passes this */
static void fa_preset_label(char *num, char *name)
{
    if (fa_on_user()) {
        up_slot_label(num, (uint32_t)TSEL->user - 1u);
        up_name((uint32_t)TSEL->user - 1u, name);
    } else {
        uint32_t k = fa_base % FMINI_NFACTORY;
        num[0] = (char)('0' + k / 10u);
        num[1] = (char)('0' + k % 10u);
        num[2] = 0;
        str_cpy(name, ENGINES[ENGI_FMINI]->presets[k].name, 16);
    }
}
static int fa_pp_on(void) { return fa_pp_until && (int32_t)(fm1_ms - fa_pp_until) < 0; }
/* the pop-up of a preset change: number and name large, 1.5 s */
static void fa_pp_draw(void)
{
    char num[8], nm[16];
    const aafont_t *f = &AF_L;
    fa_preset_label(num, nm);
    if (text_w(&AF_L, nm) > 204)
        f = &AF_M;
    cv_rect(12, 70, 216, 100, FA_OR);
    cv_rect(14, 72, 212, 96, FA_RGB(0x1B, 0x19, 0x17));
    fa_ctext(120, 84, &AF_L, num, FA_OR);
    fa_ctext(120, 126, f, nm, FA_CR);
}
/* every frame: a new preset (PRESETS, a Program Change, a saved one loaded) is shown in the pop-up; 1 = it changed now
 * (the caller goes HOME) */
static uint32_t fa_track_preset(void)
{
    const track_t *t = TSEL;
    int32_t sl = t->p[P_E7];
    uint32_t u = fa_on_user() ? t->user : 0u, ch = 0;
    if (sl >= 0 && sl < (int32_t)FMINI_NFACTORY) {
        if (sl != fa_last_sl) {
            fa_base = (uint8_t)sl;
            fa_last_sl = (int16_t)sl;
            if (fa_primed) {
                fa_pp_until = fm1_ms + 1500u;
                ch = 1;
            }
        }
    } else
        fa_last_sl = -1;
    if (u != fa_last_user) {
        fa_last_user = (uint8_t)u;
        if (u && fa_primed) {
            fa_pp_until = fm1_ms + 1500u;
            ch = 1;
        }
    }
    return ch;
}

/* The battery, bottom right (1.8.0): a horizontal icon of four bars. The level comes from the ADC thresholds the firmware always had
 * (531 / 561 / 591: ui_draw.c batt_level), so 4 bars from 591, 3 from 561, 2 from 531 and 1 below; that last one is red (FA_BATT_LOW,
 * the stock "empty": calibrate it against song.batt_raw, which the console prints as batt_raw). USB power has no charger line to
 * read (ui_draw.c batt_shown): a host that has configured us and is not suspended counts as charging, and a bolt crosses the icon.
 * Not yet read (raw 0: the emulator, the first blocks): full. Bits 0..2 the bars, bit 3 low, bit 4 charging. */
#define FA_BATT_LOW 531
static uint32_t fa_batt(void)
{
    int32_t r = song.batt_raw;
    uint32_t bars = r == 0 || r >= 591 ? 4u : r >= 561 ? 3u : r >= FA_BATT_LOW ? 2u : 1u;
    return bars | (uint32_t)(bars == 1u) << 3 | (uint32_t)(usb.config && !usb.suspended) << 4;
}
/* xr: the right edge of the nub, y: the top of the 10 px body */
static void fa_battery(int32_t xr, int32_t y)
{
    uint32_t st = fa_batt(), bars = st & 7u, i;
    uint16_t bg = fa_bg(y + 4), ink = st & 8u ? FA_RED : FA_RGB(0xD9, 0xD2, 0xC0), lit = st & 8u ? FA_RED : FA_CR;
    int32_t x = xr - 25;
    cv_rect(x, y, 23, 10, ink);                              /* the outline: a 23 x 10 body and a 2 x 4 nub */
    cv_rect(x + 1, y + 1, 21, 8, bg);
    cv_rect(x + 23, y + 3, 2, 4, ink);
    for (i = 0; i < bars; i++)
        cv_rect(x + 2 + (int32_t)i * 5, y + 2, 4, 6, lit);
    if (st & 16u) {                                          /* the bolt over the bars: rows of {y, x0, x1} in the body, a dark edge, then an orange core */
        static const uint8_t R[7][3] = {{1, 12, 13}, {2, 11, 12}, {3, 10, 11}, {4, 9, 12}, {5, 10, 11}, {6, 9, 10}, {7, 8, 9}};
        for (i = 0; i < 7u; i++)
            cv_rect(x + (int32_t)R[i][1] - 1, y + (int32_t)R[i][0], (int32_t)(R[i][2] - R[i][1]) + 3, 1, FA_RGB(0x0E, 0x0D, 0x0C));
        for (i = 0; i < 7u; i++)
            cv_rect(x + (int32_t)R[i][1], y + (int32_t)R[i][0], (int32_t)(R[i][2] - R[i][1]) + 1, 1, FA_OR);
    }
}

static void fa_scene(void)
{
    uint32_t pg = fa_page, i, cur = fmg_curi(pg), ex = fmg_count(pg) > 4u ? fmg_count(pg) - 4u : 0u;
    char b[24];
    if (pg == 0u && ex > 4u)                                 /* CTRL: the fifth is VOICES, drawn apart */
        ex = 4u;
    int32_t x;
    cv_rect(8, 0, 224, 240, fa_bg(0));                       /* the panel: the gradient in 20 bands */
    for (i = 0; i < 20u; i++)
        cv_rect(8, (int32_t)i * 12, 224, 12, fa_bg((int32_t)i * 12 + 6));
    for (x = 0; x < 8; x++) {                                /* the wood sides: a horizontal gradient and the grain */
        uint16_t w = x < 4 ? fa_mix(FA_RGB(0x4E, 0x2B, 0x14), FA_RGB(0x8A, 0x54, 0x30), (uint32_t)x * 64u)
                           : fa_mix(FA_RGB(0x8A, 0x54, 0x30), FA_RGB(0x5C, 0x33, 0x19), (uint32_t)(x - 4) * 64u);
        cv_rect(x, 0, 1, 240, w);
        cv_rect(232 + x, 0, 1, 240, w);
    }
    for (i = 1; i < 240u; i += 7u) {
        cv_rect(0, (int32_t)i, 8, 1, FA_RGB(0x3A, 0x1C, 0x0B));
        cv_rect(232, (int32_t)i, 8, 1, FA_RGB(0x3A, 0x1C, 0x0B));
        cv_rect(0, (int32_t)i + 3, 8, 1, FA_RGB(0x42, 0x20, 0x0D));
        cv_rect(232, (int32_t)i + 3, 8, 1, FA_RGB(0x42, 0x20, 0x0D));
    }
    if (fa_dr) {
        fa_dr_draw();                                        /* moogi m6: the drum screen (fmini_drum.c) */
    } else {
        cv_rect(8, 0, 224, 22, FA_RGB(0x0E, 0x0D, 0x0C));        /* the header: the name and the seven tabs */
        {                                                    /* the logo: Acid orange, Moogi cream (Acid Moogi; was FMini-1) */
            int32_t lx = 12, ly = 11 - AF_X_CAP_Y - AF_X_CAP_H / 2 + 1;
            cv_text_on(lx, ly, &AF_X, "Acid", FA_OR, FA_RGB(0x0E, 0x0D, 0x0C));
            lx += text_w(&AF_X, "Acid") + 3;
            cv_text_on(lx, ly, &AF_X, "Moogi", FA_CR, FA_RGB(0x0E, 0x0D, 0x0C));
        }
        {
            static const uint8_t TW[FA_NPAGE] = {24, 22, 20, 22, 30, 30, 18};
            int32_t tx = 64;                                 /* (after the Acid Moogi logo) */
            for (i = 0; i < FA_NPAGE; i++) {
                uint16_t bgc = i == pg ? FA_OR : FA_RGB(0x1E, 0x1B, 0x19);
                cv_rect(tx, 2, TW[i] - 1, 18, bgc);
                cv_text_on(tx + (TW[i] - 1) / 2 - text_w(&AF_X, FA_TAB[i]) / 2, 11 - AF_X_CAP_Y - AF_X_CAP_H / 2 + 1, &AF_X,
                           FA_TAB[i], i == pg ? FA_RGB(0x1A, 0x0F, 0x06) : FA_MU, bgc);
                tx += TW[i];
            }
        }
        cv_rect(14, 30, 212, 1, FA_RULE);                         /* the section title on its rule */
        {
            int32_t w = text_w(&AF_S, FMG_PAGE[pg].name) + 14;
            cv_rect(120 - w / 2, 24, w, 13, FA_RGB(0x1B, 0x19, 0x17));
            cv_text_on(120 - text_w(&AF_S, FMG_PAGE[pg].name) / 2, 24 - AF_S_CAP_Y + 2, &AF_S, FMG_PAGE[pg].name, FA_CR,
                       FA_RGB(0x1B, 0x19, 0x17));
        }
        for (i = 0; i < 4u; i++)
            fa_knob_col(pg, i, 36 + (int32_t)i * 56, cur == i);
        if (pg == 1u) {
            fa_wave_boxes(pg, cur);
        } else if (pg == 3u || pg == 4u || pg == 5u) {
            fa_graph(pg);
        } else if (pg == 6u) {                                   /* FX: DECAY TEMPO RATE TONE under their sends, a thin bar between the four sections */
            for (i = 0; i < 4u; i++)
                fa_edit_btn(36 + (int32_t)i * 56, cur == 4u + i);
            for (i = 1; i < 4u; i++) {                         /* the bars between the sections: a bright core with a faint glow */
                cv_rect(7 + (int32_t)i * 56, 38, 3, 168, FA_RGB(0x4A, 0x40, 0x32));
                cv_rect(8 + (int32_t)i * 56, 38, 1, 168, FA_RGB(0xD2, 0xC2, 0xA2));
            }
        } else {
            for (i = 0; i < ex; i++)
                fa_switch(pg, i, 36 + (int32_t)i * 56, cur == 4u + i);
            if (pg == 0u)
                fa_voices(cur == 8u);
        }
        {   /* the foot: the patch's number (00..99, or U01.. of a saved one) and name, the battery at the right */
            int32_t w;
            char nm[16];
            fa_preset_label(b, nm);
            w = text_w(&AF_N, b);
            fa_ltext(14, 214, &AF_N, b, FA_OR);
            fa_ltext(14 + w + 6, 214, &AF_N, nm, FA_CR);
            fa_battery(226, 227);
        }
    }
    if (fa_gl == 1u || fa_gl == 2u || fa_gl == 4u || fa_gl == 5u)
        fa_gl_dialog();
    else if (fa_pl_show())
        fa_tempo_dialog();
    else if (fa_sq)
        fa_seq_dialog();
    else if (fa_ac_show())
        fa_ac_dialog();
    else if (fa_ar)
        fa_ar_dialog();
    else if (fa_sv)
        fa_dialog();
    else if (fmg_fx)
        fa_fx_dialog();
    else if (fa_msg_on()) {
        int32_t mw = text_w(&AF_M, fa_msg) + 20;
        cv_rect(120 - mw / 2, 96, mw, 26, FA_OR);
        cv_rect(120 - mw / 2 + 2, 98, mw - 4, 22, FA_RGB(0x1B, 0x19, 0x17));
        fa_ctext(120, 102, &AF_M, fa_msg, FA_OR);
    }
    if (fa_pp_on())
        fa_pp_draw();
}

static uint32_t fa_signature(void)
{
    const track_t *t = TSEL;
    uint32_t h = 2166136261u, i;
    h = (h ^ fa_page) * 16777619u;
    for (i = 0; i < FMG_NPG; i++)
        h = (h ^ fmg_cur[i]) * 16777619u;
    for (i = 0; i < FG_SIZE; i++)
        h = (h ^ fmini_patch[0][i]) * 16777619u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    h = (h ^ fa_base ^ (uint32_t)fa_pp_on() << 8) * 16777619u;
    h = (h ^ fa_gl ^ (uint32_t)fa_gl_row << 3 ^ (uint32_t)fa_cc_i << 6 ^ (uint32_t)fm_midi_src << 12 ^ (uint32_t)fm_bt << 17 ^ (uint32_t)fm_midi_ch << 14 ^ (uint32_t)fm_bend_mode << 19 ^ (uint32_t)fa_usb10 << 23) * 16777619u;   /* (m9: USB AUDIO) */
    for (i = 0; i < FA_NCC; i++)
        h = (h ^ fa_cc[i]) * 16777619u;
    h = (h ^ fa_sv ^ (uint32_t)fa_ar << 4 ^ (uint32_t)fa_ar_row << 5 ^ (uint32_t)song.g[G_BPM] << 8 ^ (uint32_t)song.g[G_CLOCK] << 24) * 16777619u;
    h = (h ^ (uint32_t)song.g[G_RSIZE] ^ (uint32_t)song.g[G_DRREV] << 7 ^ (uint32_t)song.g[G_CRATE] << 14 ^ (uint32_t)song.g[G_DRLVL] << 21) * 16777619u;   /* FX: DECAY TIME RATE TONE */
    for (i = 0; i < 8u; i++)                                 /* the pop-ups' others, and which pop-up is open */
        h = (h ^ (uint32_t)song.g[FA_GID2[i]]) * 16777619u;
    h = (h ^ fmg_fx) * 16777619u;
    h = (h ^ fa_sv_slot ^ (uint32_t)fa_sv_pos << 8 ^ (uint32_t)t->user << 16 ^ (uint32_t)fa_msg_on() << 24) * 16777619u;
    for (i = 0; i < 12u; i++)
        h = (h ^ (uint8_t)fa_sv_name[i]) * 16777619u;
    h = fa_seq_sig(h);                                       /* the sequence, the tempo, the acid line (fmini_seq.c) */
    h = fa_dr_sig(h);                                        /* moogi m6: the drums (fmini_drum.c) */
    h = (h ^ fa_batt()) * 16777619u;                         /* the battery icon */
    return (h ^ t->preset) * 16777619u;
}

static void fa_splash(void);
static void fa_draw(void)
{
    uint32_t sig = fa_signature(), band;
    if (sig == fa_sig_prev && !ui.force)
        return;
    fa_sig_prev = sig;
    ui.force = 0;
    fmg_force_page = (int8_t)fa_page;
    if (fa_gl == 3u) {                                    /* ABOUT: the splash, the version bottom right */
        fa_splash();
        return;
    }
    for (band = 0; band < 2u; band++) {
        cv_begin(240, 120, FA_RGB(0x17, 0x15, 0x13));
        cv_oy = -(int32_t)band * 120;
        cv_cy0 = 0;
        fa_scene();
        cv_oy = 0;
        cv_blit(0, band * 120u);
    }
}

#include "fmini_splash.h"
static void fa_splash(void)                              /* the picture (assets/fmini-splash.png), the version bottom right: power-on (3 s, main.c) and ABOUT */
{
    uint32_t band, x, y;
    for (band = 0; band < 2u; band++) {
        cv_begin(240, 120, 0);
        cv_oy = -(int32_t)band * 120;
        for (y = band * 120u; y < band * 120u + 120u; y++)
            for (x = 0; x < 240u; x++)
                cv_pset((int32_t)x, (int32_t)y, FMINI_SPLASH_PAL[FMINI_SPLASH[y * 240u + x]]);
        cv_rect(228 - text_w(&AF_S, FMINI_VERSION), 217, text_w(&AF_S, FMINI_VERSION) + 8, 18, FA_RGB(0x14, 0x10, 0x1E));   /* (on the busy art: a dark plate) */
        cv_text_on(232 - text_w(&AF_S, FMINI_VERSION), 220, &AF_S, FMINI_VERSION, FA_CR, 0);
        cv_oy = 0;
        cv_blit(0, band * 120u);
    }
}

/* ---------------------------------------------------------- control --- */
static void fa_goto(uint32_t pg)
{
    fa_page = (uint8_t)(pg % FA_NPAGE);
    fa_cfg_save();                                        /* (the FX page's globals) */
}
/* HOME: the first screen, CTRL; the pop-ups close. A preset change does the same (fa_input) */
#define FA_HOME_PAGE 3u                    /* moogi: HOME and power-on show FILT (was CTRL, page 0) */

static void fa_dr_leave(void)                             /* moogi m6: out of the drum mode (the drums play on) */
{
    fa_dr = 0;
    if (fa_dr_dirty)
        fa_dr_save_now();
}
static void fa_home(void)
{
    fa_sv = 0;
    fa_ar = 0;
    fa_gl_close();
    fa_dr_leave();
    fa_goto(FA_HOME_PAGE);
}
/* moogi m6: SEQ twice: the drum mode on (the FILT page under it: KNOB 1..4 the filter; the dialogs close, REC disarms) or off
 * (HOME's screen) */
static void fa_dr_toggle(void)
{
    if (fa_dr) {
        fa_home();
        return;
    }
    fmg_fx = 0;
    fa_sv = 0;
    fa_ar = 0;
    fa_gl_close();
    if (fa_sq_rec)
        fa_seq_arm();                                     /* (REC off: the keys record nothing in the drum mode) */
    fa_goto(FA_HOME_PAGE);
    fa_dr = 1;
}
static void fa_go_filt(void)                              /* EDIT with the acid line off: FILT, as before */
{
    fmg_fx = 0;
    fa_sv = 0;
    fa_ar = 0;
    fa_gl_close();
    fa_goto(3u);
}

/* SELECT: one control on; past a page's last control the next page's first (the other way: the last) */
static void fa_select(int32_t s)
{
    uint32_t pg = fa_page, n = fmg_count(pg);
    int32_t c = (int32_t)fmg_curi(pg) + (s > 0 ? 1 : -1);
    if (c < 0) {
        fa_page = (uint8_t)((pg + FA_NPAGE - 1u) % FA_NPAGE);
        fmg_cur[fa_page] = (uint8_t)(fmg_count(fa_page) - 1u);
    } else if (c >= (int32_t)n) {
        fa_page = (uint8_t)((pg + 1u) % FA_NPAGE);
        fmg_cur[fa_page] = 0;
    } else {
        fmg_cur[pg] = (uint8_t)c;
    }
}

/* a factory patch n was just loaded into track t: what its preset_t does not carry (eng_fmini.c fmini_x_t): the arpeggiator
 * (on or off, with the MODE the ARP button turns on), the LFO and the common modulation, the FX settings of the pop-ups and the keys' octave.
 * The tempo stays the player's. Also the Program Change's way in (ui.c midi_pc_load) */
static const uint8_t FA_XG[12] = {G_RTYPE, G_RSIZE, G_RDAMP, G_DTIME, G_DRREV, G_DFDBK, G_ENGGO, G_CDEPTH, G_CRATE, G_INFO, G_DRLVL, G_MIDI};
static void fa_factory_extras(track_t *t, uint32_t n)
{
    const fmini_x_t *x;
    uint32_t i;
    if (n >= FMINI_NFACTORY)
        return;
    x = &FMINI_X[n];
    t->p[P_AMODE] = x->arp[0] ? x->arp[1] : 0;
    t->p[P_ARATE] = x->arp[2];
    t->p[P_AOCT] = x->arp[3];
    t->p[P_AGATE] = x->arp[4];
    t->p[P_ASWING] = x->arp[5];
    t->p[P_APROB] = x->arp[6];
    t->p[P_AHOLD] = 0;
    t->p[P_AORDER] = 0;
    t->p[P_LEVEL] = x->level;
    t->p[P_LRATE] = x->lfo[0];
    t->p[P_LWAVE] = x->lfo[1];
    t->p[P_LFADE] = x->lfo[2];
    t->p[P_LSYNC] = x->lfo[3];
    t->p[P_LD_PIT] = x->mod[0];
    t->p[P_LD_AMP] = x->mod[1];
    t->p[P_ED_PIT] = x->mod[2];
    if (t != &trk[0])
        return;
    for (i = 0; i < 12u; i++)
        song.g[FA_XG[i]] = (int16_t)clamp(x->fx[i], GP[FA_XG[i]].min, GP[FA_XG[i]].max);
    song.octave = x->oct;
    fa_ar_last = x->arp[1] ? x->arp[1] : 1u;
}

/* PRESETS: the previous / next patch of the list (factory F1..F8, then the saved ones), round; in the SAVE dialog the slot */
static void fa_preset(int32_t s)
{
    track_t *t = TSEL;
    uint32_t nu = fa_nuser(), n = FMINI_NFACTORY + nu, cur, nx;
    if (fa_sv) {
        fa_sv_slot = (uint8_t)((fa_sv_slot + UP_SLOTS + (s > 0 ? 1u : UP_SLOTS - 1u)) % UP_SLOTS);
        fa_sv_fill();
        return;
    }
    cur = fa_on_user() ? FMINI_NFACTORY + fa_user_rank((uint32_t)t->user - 1u)
                       : (t->p[P_E7] >= 0 && t->p[P_E7] < (int32_t)FMINI_NFACTORY ? (uint32_t)t->p[P_E7] : fa_base % FMINI_NFACTORY);   /* (OWN: an edit of CTRL / OSC / MIX: on from the patch it came from) */
    nx = (cur + (s > 0 ? 1u : n - 1u)) % n;
    if (nx < FMINI_NFACTORY) {
        t->user = 0;
        apply_preset_to(t, nx);
        t->p[P_E7] = (int16_t)nx;
        fa_factory_extras(t, nx);                         /* its arpeggiator, LFO, FX settings and octave */
    } else {
        up_load(fa_user_nth(nx - FMINI_NFACTORY));
    }
    fmini_poll();
}

/* a page button: the tap opens it */
static void fa_buttons(uint32_t pressed)
{
    static const uint8_t PG_BTN[5] = {B_LFO, B_HOME, B_SCL, B_FX, B_ENV};   /* (GLO: global, EDIT, ARP, SEQ, REC, PLAY:
                                                                             * their own; B_HOME never comes here) */
    uint32_t i, seq_was = fa_sq;
    fa_sq = (uint8_t)((fm1_in.buttons >> panel.btn[B_SEQ]) & 1u);   /* SEQ held: the keys' layer, its screen */
    if ((pressed >> panel.btn[B_SEQ]) & 1u) {            /* moogi m6: SEQ twice within 0.4 s: the drum mode on / off */
        if (fa_dr_sq_t && (uint32_t)(fm1_ms - fa_dr_sq_t) < 400u) {
            fa_dr_sq_t = 0;
            fa_dr_toggle();
        } else {
            fa_dr_sq_t = fm1_ms ? fm1_ms : 1u;
        }
    }
    if (seq_was && !fa_sq && fa_sq_dirty) {               /* (the parts saved when it is let go) */
        fa_sq_dirty = 0;
        fa_cfg_dirty = 1;
        fa_cfg_save();
    }
    fa_play_btn(pressed);                                 /* PLAY: a tap; held, the tempo */
    fa_edit_btn_poll(pressed);                            /* EDIT: the acid line */
    pressed = fa_rec_btn(pressed);                        /* REC: arm on its release; REC + ARP undoes the knobs; SEQ + REC clears */
    if (fa_seq_stepping() && ((pressed >> panel.btn[B_ARP]) & 1u)) {   /* step record: ARP is a rest */
        fa_seq_rest();
        pressed &= ~(1u << panel.btn[B_ARP]);
    }
    if ((pressed >> panel.btn[B_HOME]) & 1u) {            /* HOME: the first screen, CTRL; the pop-ups close */
        fa_home();
        return;
    }
    if ((pressed >> panel.btn[B_GLO]) & 1u) {             /* GLO: the global pop-up; inside a list, one level back */
        fa_sv = 0;
        fa_ar = 0;
        if (!fa_gl)
            fa_gl = 1;
        else if (fa_gl > 1u)
            fa_gl = 1;
        else
            fa_gl_close();
        return;
    }
    {   /* ARP: a tap turns the arpeggiator on / off (the button lights); held, HOLD on / off. The pop-up shows it */
        uint32_t down = (fm1_in.buttons >> panel.btn[B_ARP]) & 1u;
        if ((pressed >> panel.btn[B_ARP]) & 1u) {
            fa_arp_down = 1;
            fa_arp_done = 0;
            fa_arp_t0 = fm1_ms;
        }
        if (fa_arp_down) {
            if (!down) {
                fa_arp_down = 0;
                if (!fa_arp_done) {                           /* the tap */
                    track_t *t = TSEL;
                    if (t->p[P_AMODE]) {
                        fa_ar_last = (uint8_t)t->p[P_AMODE];
                        t->p[P_AMODE] = 0;
                    } else {
                        t->p[P_AMODE] = fa_ar_last ? fa_ar_last : 1;
                    }
                    fa_arp_show();
                }
            } else if (!fa_arp_done && (uint32_t)(fm1_ms - fa_arp_t0) >= 450u) {   /* the hold */
                fa_arp_done = 1;
                TSEL->p[P_AHOLD] = (int16_t)!TSEL->p[P_AHOLD];
                fa_arp_show();
            }
        }
    }
    {   /* moogi m5: LFO held half a second: MONO <-> POLY (CTRL's VOICES, kept in the preset), on CTRL with VOICES lit.
         * The tap still opens CTRL at once (below) */
        uint32_t down = (fm1_in.buttons >> panel.btn[B_LFO]) & 1u;
        if ((pressed >> panel.btn[B_LFO]) & 1u) {
            fa_lfo_down = 1;
            fa_lfo_t0 = fm1_ms;
        }
        if (fa_lfo_down && !down) {
            fa_lfo_down = 0;
        } else if (fa_lfo_down && (uint32_t)(fm1_ms - fa_lfo_t0) >= 450u) {
            fa_lfo_down = 0;
            fmg_fx = 0;
            fa_sv = 0;
            fa_ar = 0;
            fa_gl_close();
            fa_goto(0);
            fmg_turn(0, 8u, TSEL->p[P_VOICE] == V_POLY ? -1 : 1, EN_ALGO);
            fmg_cur[0] = 8u;
        }
    }
    if ((pressed >> panel.btn[B_SAVE]) & 1u) {            /* SAVE: open the dialog; again: write */
        if (fa_gl == 4u) {                                /* (GLO > ACID: the line as a favorite) */
            ac_fav_save();
            return;
        }
        fa_ar = 0;
        fa_gl_close();
        if (fa_sv)
            fa_sv_do();
        else
            fa_sv_open();
        return;
    }
    /* (moogi m8: SCL no longer taps the delay's time in its pop-up: the time follows the tempo) */
    for (i = 0; i < 5u; i++)
        if ((pressed >> panel.btn[PG_BTN[i]]) & 1u) {
            if (PG_BTN[i] == B_FX && fa_page == 6u) {     /* FX again: round the four pop-ups, then back to the page */
                fa_sv = 0;
                fa_ar = 0;
                fa_gl_close();
                fmg_fx = (uint8_t)((fmg_fx + 1u) % 5u);
                if (!fmg_fx)
                    fa_cfg_save();
                continue;
            }
            fmg_fx = 0;
            fa_sv = 0;                                    /* a page button cancels the dialog */
            fa_ar = 0;
            fa_gl_close();
            fa_dr_leave();                                /* moogi m6: and leaves the drum mode */
            if (PG_BTN[i] == B_ENV) {
                fa_env_last = (uint8_t)(fa_page == 4u ? 5u : 4u);
                fa_goto(fa_env_last);
            } else {
                static const uint8_t TO[5] = {0, 3, 1, 6, 4};      /* LFO CTRL, (HOME FILT), SCL OSC (again: MIX), FX, ENV */
                fa_goto(PG_BTN[i] == B_SCL && fa_page == 1u ? 2u : TO[i]);
            }
        }
    if ((pressed >> panel.btn[B_OCTDN]) & 1u)
        song.octave = song.octave > -3 ? song.octave - 1 : song.octave;
    if ((pressed >> panel.btn[B_OCTUP]) & 1u)
        song.octave = song.octave < 3 ? song.octave + 1 : song.octave;
}

/* the instrument is MONO, like the one it follows (one voice: a new key retriggers the contours and glides when GLIDE
 * is on, the lowest key wins), or POLY (CTRL's VOICES: 12 voices); it is part of the preset (P_VOICE is in its record).
 * Whatever else a patch or a record says (LEGATO, UNISON, a factory patch's own) plays MONO */
static void fa_mono(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        if (trk[i].p[P_VOICE] != V_POLY)
            trk[i].p[P_VOICE] = V_MONO;
        trk[i].p[P_PRIO] = 0;                         /* moogi: LAST (was 1, LOW: the Minimoog's): the newest key wins, a
                                                       * released one falls back to the key still held, as the Volca Bass */
    }
}

static void fa_input(void)
{
    uint32_t pressed = fm1_input_edges(0), k, notes = fm1_input_note_edges();
    int32_t s;
    midi_take();                                            /* MIDI IN notes */
    midi_pc_poll();                                         /* Program Change: a patch */
    fmini_poll();                                            /* SLOT turned: its patch */
    settings_later_poll();
    if (TSEL->eng_req != ENGI_FMINI) {                       /* an old autosave: the instrument is FMINI */
        set_engine_of(TSEL, ENGI_FMINI);
        apply_preset_to(TSEL, 0);
        fmini_poll();
    }
    song.sel = 0;
    fa_mono();
    fmg_glide_sync();                                       /* the patch's GLIDE switch and time -> the track's */
    fmg_force_page = (int8_t)fa_page;
    fa_buttons(pressed);
    if (notes)
        fa_layer_keys(notes);                               /* keys pressed with SEQ (or EDIT, the acid line on) held */
    fa_seq_poll();                                          /* the sequence's length, REC, the keys' layer */
    fa_dr_poll();                                           /* moogi m6: the drums' track, the keys' lock, saving */
    fa_acid_poll();                                         /* the acid line into the sequence */
    if (fa_pl_down && (s = panel_enc(EN_SELECT)) != 0)
        fa_tempo_turn(s);                                   /* PLAY held: SELECT is the tempo */
    if ((s = panel_enc(EN_PRESET)) != 0 && !fa_gl) {
        if (fa_acid && fa_ed_down && !fa_sq && !fa_ar && !fa_sv && !fmg_fx) {
            ac_turn(AV_TENS, s, EN_PRESET);                 /* the acid line: PRESETS is TENSION (moogi m5: EDIT held) */
            fa_ed_used = 1;
        } else if (fa_dr) {
            if (!fa_sq && !fa_ar && !fa_sv)
                fa_dr_turn_preset(s);                       /* moogi m6: VOLUME, or a black key held: its CHANCE */
        } else
            fa_preset(s);
    }
    if (fa_track_preset() && !fa_dr)                        /* a new preset (the knob, a Program Change, a saved one): HOME */
        fa_home();                                          /* (moogi m6: the drum mode stays) */
    fa_cc_poll();                                         /* the CCs the MIDI inputs brought */
    if (fa_gl || fa_sq || fa_ar || fa_sv || fa_page != 6u)
        fmg_fx = 0;                                       /* the FX pop-up lives on the FX page alone, under no other dialog */
    if (fmg_fx) {                                         /* an FX pop-up: K1..K4 its four knobs, SELECT / ALGO as on a page */
        uint32_t pg = 6u + fmg_fx;
        fmg_force_page = (int8_t)pg;
        if ((s = panel_enc(EN_SELECT)) != 0)
            fmg_cur[pg] = (uint8_t)((fmg_cur[pg] + 4u + (s > 0 ? 1u : 3u)) % 4u);
        if ((s = panel_enc(EN_ALGO)) != 0)
            fmg_turn(pg, fmg_cur[pg] % 4u, s, EN_ALGO);
        for (k = 0; k < 4u; k++)
            if ((s = panel_enc(EN_K1 + k)) != 0) {
                fmg_turn(pg, k, s, EN_K1 + k);
                fmg_cur[pg] = (uint8_t)k;
            }
        return;
    }
    if (fa_gl) {                                          /* the GLO pop-up (the page's knobs wait) */
        for (k = 0; k < 4u; k++)
            (void)panel_enc(EN_K1 + k);
        (void)panel_enc(EN_PRESET);
        if (fa_gl == 1u) {
            if ((s = panel_enc(EN_SELECT)) != 0)
                fa_gl_row = (uint8_t)((fa_gl_row + FA_GL_N + (s > 0 ? 1u : FA_GL_N - 1u)) % FA_GL_N);
            if ((s = panel_enc(EN_ALGO)) != 0) {
                if (fa_gl_row == 0u) {
                    static const uint8_t SRC[3] = {0, 1, 3};
                    int32_t d = (fm_midi_src == 3u ? 2 : (int32_t)(fm_midi_src & 1u)) + (s > 0 ? 1 : -1);
                    fm_midi_src = SRC[clamp(d, 0, 2)];
                    panic_req |= 1u;                       /* a note of the source left must not hang */
                    fa_cfg_dirty = 1;
                } else if (fa_gl_row == 1u) {
                    fm_midi_ch = (uint8_t)clamp((int32_t)fm_midi_ch + (s > 0 ? 1 : -1), 0, 16);
                    panic_req |= 1u;
                    fa_cfg_dirty = 1;
                } else if (fa_gl_row == 3u) {
                    uint32_t ch;
                    fm_bend_mode = (uint8_t)clamp((int32_t)fm_bend_mode + (s > 0 ? 1 : -1), 0, 3);
                    for (ch = 0; ch < 16u; ch++)           /* the bend held now follows the new range */
                        midi_expression_channel(ch);
                    fa_cfg_dirty = 1;
                } else if (fa_gl_row == 4u) {
                    fa_knob_rec = (uint8_t)(s > 0);         /* KNOB REC: the knobs recorded with the notes, or not */
                    fa_cfg_dirty = 1;
                } else if (fa_gl_row == 6u) {
                    fa_gl = 5;                              /* moogi m7: GLO > DRUMS */
                } else if (fa_gl_row == 7u) {
                    fa_usb_set((uint32_t)(s > 0));          /* moogi m9: USB AUDIO: MIX / 10 CH */
                } else
                    fa_gl = fa_gl_row == 2u ? 2u : fa_gl_row == 5u ? 4u : 3u;
            }
        } else if (fa_gl == 2u) {
            if ((s = panel_enc(EN_SELECT)) != 0)
                fa_cc_i = (uint8_t)((fa_cc_i + FA_NCC + (s > 0 ? 1u : FA_NCC - 1u)) % FA_NCC);
            if ((s = panel_enc(EN_ALGO)) != 0) {
                int32_t c = fa_cc[fa_cc_i], n;
                for (n = 0; n < 121; n++) {                /* the next valid CC, round through 0 = none */
                    c = (c + 121 + (s > 0 ? 1 : 120)) % 121;
                    if (fa_cc_valid((uint32_t)c))
                        break;
                }
                fa_cc[fa_cc_i] = (uint8_t)c;
                fa_cfg_dirty = 1;
            }
        } else if (fa_gl == 5u) {                           /* moogi m7: DRUMS: KIT, SIDECHAIN, RELEASE */
            if ((s = panel_enc(EN_SELECT)) != 0)
                fa_dr_row = (uint8_t)((fa_dr_row + FA_DR_NROW + (s > 0 ? 1u : FA_DR_NROW - 1u)) % FA_DR_NROW);
            if ((s = panel_enc(EN_ALGO)) != 0)
                fa_dr_menu_turn(s);
        } else if (fa_gl == 4u) {                           /* ACID: the line's settings */
            if ((s = panel_enc(EN_SELECT)) != 0)
                ac_row = (uint8_t)((ac_row + AC_NROW + (s > 0 ? 1u : AC_NROW - 1u)) % AC_NROW);
            if ((s = panel_enc(EN_ALGO)) != 0)
                ac_menu_turn(s);
        } else {                                            /* ABOUT: either encoder goes back */
            if (panel_enc(EN_SELECT) != 0 || panel_enc(EN_ALGO) != 0)
                fa_gl = 1;
        }
        return;
    }
    if (fa_sq) {                                          /* SEQ held: the wheels and knobs wait */
        (void)panel_enc(EN_ALGO);
        (void)panel_enc(EN_SELECT);
        for (k = 0; k < 4u; k++)
            (void)panel_enc(EN_K1 + k);
        return;
    }
    if (fa_ar) {                                          /* the ARP pop-up */
        if (fa_ar_until && (int32_t)(fm1_ms - fa_ar_until) >= 0 && !fa_arp_down)
            fa_ar = 0;                                    /* a few seconds without a touch: it closes */
        if ((s = panel_enc(EN_SELECT)) != 0) {
            fa_ar_until = fm1_ms + 4000u;
        }
        if (!fa_ar)
            return;
        if (s)
            fa_ar_row = (uint8_t)((fa_ar_row + FA_AR_N + (s > 0 ? 1u : FA_AR_N - 1u)) % FA_AR_N);
        if ((s = panel_enc(EN_ALGO)) != 0) {
            fa_ar_turn(fa_ar_row, s);
            fa_ar_until = fm1_ms + 4000u;
        }
        for (k = 0; k < 4u; k++)
            if ((s = panel_enc(EN_K1 + k)) != 0) {
                fa_ar_until = fm1_ms + 4000u;
                fa_ar_turn(k, s);
                fa_ar_row = (uint8_t)k;
            }
        return;
    }
    if (fa_sv) {                                          /* the SAVE dialog: name editing; the knobs wait */
        if ((s = panel_enc(EN_SELECT)) != 0)
            fa_sv_pos = (uint8_t)((fa_sv_pos + 12u + (s > 0 ? 1u : 11u)) % 12u);
        if ((s = panel_enc(EN_ALGO)) != 0) {
            char *c = &fa_sv_name[fa_sv_pos];
            uint32_t i, n = (uint32_t)sizeof FA_CH - 1u, at = 0;
            for (i = 0; i < fa_sv_pos; i++)               /* a short name: pad with blanks up to the cursor */
                if (!fa_sv_name[i])
                    fa_sv_name[i] = ' ';
            if (!*c)
                *c = ' ';
            for (i = 0; i < n; i++)
                if (FA_CH[i] == *c)
                    at = i;
            *c = FA_CH[(at + n + (s > 0 ? 1u : n - 1u)) % n];
            if (!fa_sv_name[fa_sv_pos + 1u] && fa_sv_pos < 11u)
                fa_sv_name[fa_sv_pos + 1u] = 0;
        }
        for (k = 0; k < 4u; k++)
            (void)panel_enc(EN_K1 + k);
        return;
    }
    if (fa_acid && fa_ed_down) {                          /* the acid line, EDIT held: SELECT DENSITY, ALGORITHM MOVEMENT (and
                                                           * PRESETS TENSION, above), the knobs ACCENTS SLIDES NOTES RANGE.
                                                           * moogi m5: without EDIT the wheels are the page's, as with acid off */
        static const uint8_t AK[4] = {AV_ACC, AV_SLD, AV_NOTES, AV_OCT};
        if ((s = panel_enc(EN_SELECT)) != 0) {
            ac_turn(AV_DENS, s, EN_SELECT);
            fa_ed_used = 1;
        }
        if ((s = panel_enc(EN_ALGO)) != 0) {
            ac_turn(AV_MOVE, s, EN_ALGO);
            fa_ed_used = 1;
        }
        {
            for (k = 0; k < 4u; k++)
                if ((s = panel_enc(EN_K1 + k)) != 0) {
                    ac_turn(AK[k], s, EN_K1 + k);
                    fa_ed_used = 1;
                }
            return;
        }
    }
    if (fa_dr) {                                          /* moogi m6: the drum mode: SELECT HITS, ALGORITHM ROTATE (a white key
                                                           * held: its quarter's); KNOB 1..4 the filter, below */
        if ((s = panel_enc(EN_SELECT)) != 0)
            fa_dr_turn_hits(s);
        if ((s = panel_enc(EN_ALGO)) != 0)
            fa_dr_turn_rot(s);
    } else {
        if ((s = panel_enc(EN_SELECT)) != 0)
            fa_select(s);
        fmg_force_page = (int8_t)fa_page;
        if ((s = panel_enc(EN_ALGO)) != 0)
            fmg_algo(s);
    }
    fmg_force_page = (int8_t)fa_page;
    for (k = 0; k < 4u; k++)
        if ((s = panel_enc(EN_K1 + k)) != 0) {
            fmg_force_page = (int8_t)fa_page;
            fmg_knob(k, s);
            fmg_cur[fa_page] = (uint8_t)k;                  /* the highlight follows the knob last turned */
        }
}

static void fa_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0}, nm[FM1_NCOL] = {0};
    uint32_t k, c;
    static uint8_t ready;
    if (idle_leds() || fm1_led_anim_on())
        return;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);                       /* every button's glow */
    {
        static const uint8_t PG_LED[FA_NPAGE] = {B_LFO, B_SCL, B_SCL, B_HOME, B_ENV, B_ENV, B_FX};
        if (!fa_dr)                                          /* (moogi m6: none in the drum mode, SEQ lights) */
            led_put(nl, panel.btn[PG_LED[fa_page]], 1);      /* the page's button (FILT: HOME's) */
    }
    led_put(nl, panel.btn[B_EDIT], fa_acid);                /* Acid Moogi: EDIT lights while the acid line is on */
    led_put(nl, panel.btn[B_ARP], TSEL->p[P_AMODE] != 0);
    led_put(nl, panel.btn[B_SEQ], fa_sq != 0u || fa_dr);
    led_put(nl, panel.btn[B_REC], fa_sq_rec != 0u);
    led_put(nl, panel.btn[B_PLAY], fa_pl_down ? fa_pl_led() : ck_wait ? (int)((fm1_ms / 125u) & 1u) : song.playing != 0u);
                                                          /* held: it blinks in time; moogi m5: waiting for the norns' bar,
                                                           * it blinks fast */
    led_put(nl, panel.btn[B_SAVE], fa_sv);
    led_put(nl, panel.btn[B_GLO], fa_gl != 0u);
    led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
    led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    if (fa_dr && !fa_sq) {
        fa_dr_leds(nl, nm);                                 /* moogi m6: the drums on the keys */
    } else if (fa_seq_leds(nl, nm)) {                       /* the sequence on the keys (SEQ held, playing, recording) */
        if (!fa_sq)
            for (k = 0; k < 27u; k++)
                if ((fm1_in.notes >> k) & 1u)
                    led_put(nl, 14u + k, 1);
    } else {
        c = fm1_in.notes | fa_sound_keys();                 /* the keys held, and the notes sounding: arpeggio, MIDI */
        for (k = 0; k < 27u; k++)
            led_put(nl, 14u + k, (int)((c >> k) & 1u));
    }
    for (k = 0; k < FM1_NCOL; k++) {
        fm1_led_dim[k] = nd[k];
        fm1_led_breath[k] = nb[k];
        fm1_led_mid[k] = nm[k];
        fm1_led[k] = nl[k];
    }
}

/* power-on: track 1 is the instrument (the first factory patch), the others are muted FMINI parts */
static void fa_init(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (i == FA_DR_TRK)
            continue;                                     /* moogi m6: track 2 is the drums (fa_dr_setup, below) */
        if (t->eng_req != ENGI_FMINI || i) {              /* track 1 keeps its autosaved sound; the rest start fresh and muted */
            set_engine_of(t, ENGI_FMINI);
            apply_preset_to(t, 0);
            t->eng_req = ENGI_FMINI;
            t->engine = t->eng_req;
            fmini_load_slot(i, 0);
            fmini_adopt(i);
            if (!i)
                fa_factory_extras(t, 0);                 /* the first patch's arpeggiator, LFO, FX settings and octave */
        }
        else if (t->p[P_E7] >= 0 && t->p[P_E7] < (int32_t)FMINI_NFACTORY && !fa_on_user())
            song.octave = FMINI_X[t->p[P_E7]].oct;        /* the autosaved factory patch (the octave is not autosaved): its own octave, as a PRESETS turn gives */
        t->p[P_MUTE] = i ? 1 : 0;
    }
    song.sel = 0;
    fa_cfg_load();
    fa_dr = 0;
    fa_dr_setup();                                        /* moogi m6: the drums, from the saved beat and kit */
    fa_primed = 0;
    fa_last_sl = -2;
    fa_last_user = 0;
    fa_pp_until = 0;
    fa_mono();
    fa_page = FA_HOME_PAGE;                                 /* moogi: power-on on FILT */
    fa_track_preset();                                      /* the patch loaded at power-on: shown, no pop-up */
    fa_primed = 1;
    ui.force = 1;
}
#endif /* FELUCCA_FMINI_ONLY */
