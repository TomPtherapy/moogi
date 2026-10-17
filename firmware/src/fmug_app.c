/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * FMini-1: the whole user interface of the FMUG-only firmware (FELUCCA_FMUG_ONLY=1). Felucca's menu, layers, sequencer
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
 * A.ENV), LFO (CTRL), EDIT (OSC), SCL (MIX), GLO (FILT) open a page; OCT- / OCT+ the keys' octave. The keys, MIDI
 * and the MASTER pot play and sound as in Felucca. Track 1 is the instrument; the other tracks are muted.
 * Included by felucca.c before main.c. */
#if FELUCCA_FMUG_ONLY
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
static int32_t fa_cap_y(const aafont_t *f) { return f == &AF_N ? AF_N_CAP_Y : f == &AF_X ? AF_X_CAP_Y : f == &AF_S ? AF_S_CAP_Y : AF_M_CAP_Y; }
static int32_t fa_cap_h(const aafont_t *f) { return f == &AF_N ? AF_N_CAP_H : f == &AF_X ? AF_X_CAP_H : f == &AF_S ? AF_S_CAP_H : AF_M_CAP_H; }
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

static void fa_knob_col(uint32_t pg, uint32_t i, int32_t cx, int hot)
{
    const char *lb, *un;
    char val[16];
    int32_t ratio, k, cy = 84, n = 11, a;
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
            else
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
    fa_ctext(cx, 38, &AF_X, f->lb, lab);
    if (f->kind == FK_NAME && f->b == FN_RANGE) {
        str_cpy(val, FMG_RANGE[clamp((int32_t)fmg_byte(f->a), 0, 5)], 16);
    } else {
        fa_num(val, ratio);
    }
    fa_ctext(cx, 112, &AF_S, val, FA_OR);
    {
        char kn[4] = {'K', (char)('1' + i), 0, 0};
        fa_ctext(cx, 127, &AF_X, kn, FA_RGB(0x5D, 0x55, 0x4A));
    }
}

static void fa_switch(uint32_t pg, uint32_t ex, int32_t cx, int hot)
{
    const fmgf_t *f = fmg_field(pg, 4u + ex);
    int on = fmg_sw_on(f), x = cx - 15, y = 163;
    uint16_t col = pg == 0u ? FA_RED : pg == 2u ? FA_BLUE : on ? FA_OR : FA_RGB(0xD9, 0xD2, 0xC0);
    if (hot)
        cv_frame(x - 3, y - 3, 36, 20, FA_OR);
    cv_rect(x, y, 31, 14, FA_RGB(0x4A, 0x44, 0x3D));
    cv_rect(x + 1, y + 1, 29, 12, FA_RGB(0x0D, 0x0C, 0x0B));
    cv_rect(on ? x + 15 : x + 2, y + 2, 14, 10, col);
    fa_ctext(cx, 150, &AF_X, f->lb, hot ? FA_OR : FA_CR);
    fa_ctext(cx, 182, &AF_X, on ? "ON" : "OFF", on ? (pg == 0u ? FA_RED : pg == 2u ? FA_BLUE : FA_OR) : FA_MU);
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
    if (pg == 3u) {                                         /* the ladder's low-pass: cutoff, emphasis */
        int32_t fc = 12 * 256 + 36 * 256 * k[0] / 1000, pk = k[1] * 18 / 1000, py = 0, px = 0;
        for (i = 0; i <= 60; i++) {
            int32_t d = (i * 256 - fc), u = d * 256 / (3 * 256), g = 65536 / (1 + (int32_t)((int64_t)u * u / 65536)), db;
            db = (d > 0 ? -d * 220 / (60 * 256) : 0) + pk * g / 65536;
            db = clamp(db, -40, 20);
            x = 17 + i * 206 / 60;
            {
                int32_t y = 190 - (db + 40 + 0) * 40 / 60;
                if (i)
                    cv_line_t(px, py, x, y, FA_OR, 2);
                px = x;
                py = y;
            }
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

static void fa_wave_boxes(uint32_t pg, uint32_t cur)
{
    static const char *const WV[6] = {"TRI", "SHARK", "SAW", "SQR", "PULSE W", "PULSE N"};
    uint32_t j;
    for (j = 0; j < 3u; j++) {
        int32_t x = 14 + (int32_t)j * 74, w = clamp((int32_t)fmg_byte(FG_W1 + j), 0, 5);
        char t[24] = "OSC ";
        int hot = cur == 4u + j;
        cv_rect(x, 146, 66, 36, hot ? FA_OR : FA_LINE);
        cv_rect(x + 1, 147, 64, 34, FA_BOX);
        fa_poly(FA_WAVE[w], FA_WAVE_N[w], x + 6, 151, 54, 26, FA_OR);
        t[4] = (char)('1' + j);
        t[5] = ' ';
        t[6] = 0;
        str_cpy(t + 6, WV[w], 12);
        fa_ctext(x + 33, 188, &AF_X, t, hot ? FA_OR : FA_CR);
    }
}

/* ------------------------------------------------------- presets --- */
/* the list PRESETS walks: the 8 factory patches, then the user presets of this instrument (SAVE) in slot order */
static uint32_t fa_nuser(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)(up_used(k) && up_engine(k) == ENGI_FMUG);
    return n;
}
static uint32_t fa_user_nth(uint32_t n)                   /* the slot of the n-th user preset */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && up_engine(k) == ENGI_FMUG && !n--)
            return k;
    return 0;
}
static uint32_t fa_user_rank(uint32_t slot)
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)(up_used(k) && up_engine(k) == ENGI_FMUG);
    return n;
}
static int fa_on_user(void)
{
    const track_t *t = TSEL;
    return t->user && up_used((uint32_t)t->user - 1u) && up_engine((uint32_t)t->user - 1u) == ENGI_FMUG;
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
    if (up_used(k) && up_engine(k) == ENGI_FMUG)
        up_name(k, fa_sv_name);
    else if (fa_on_user())
        up_name((uint32_t)TSEL->user - 1u, fa_sv_name);
    else
        str_cpy(fa_sv_name, ENGINES[ENGI_FMUG]->presets[TSEL->p[P_E7] >= 0 && TSEL->p[P_E7] < (int32_t)FMUG_NFACTORY ? TSEL->p[P_E7] : 0].name, 13);
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
    int used = up_used(k) && up_engine(k) == ENGI_FMUG;
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

static void fa_scene(void)
{
    uint32_t pg = fa_page, i, cur = fmg_curi(pg), ex = fmg_count(pg) > 4u ? fmg_count(pg) - 4u : 0u;
    const track_t *t = TSEL;
    char b[24];
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
    cv_rect(8, 0, 224, 22, FA_RGB(0x0E, 0x0D, 0x0C));        /* the header: the name and the seven tabs */
    cv_text_on(12, 3, &AF_S, "FMini-1", FA_OR, FA_RGB(0x0E, 0x0D, 0x0C));
    {
        static const uint8_t TW[FA_NPAGE] = {24, 22, 20, 22, 30, 30, 18};
        int32_t tx = 60;
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
    } else {
        for (i = 0; i < ex; i++)
            fa_switch(pg, i, 36 + (int32_t)i * 56, cur == 4u + i);
    }
    {   /* the foot: the patch's number and name, the page "n/7" */
        int32_t sl = t->p[P_E7], w;
        char nm[16];
        if (fa_on_user()) {
            up_slot_label(b, (uint32_t)t->user - 1u);
            up_name((uint32_t)t->user - 1u, nm);
        } else if (sl >= 0 && sl < (int32_t)FMUG_NFACTORY) {
            fmt_int(b, sl + 1);                       /* 1..100 */
            if (sl + 1 < 10) {
                b[1] = b[0];
                b[0] = '0';
                b[2] = 0;
            }
            str_cpy(nm, ENGINES[ENGI_FMUG]->presets[sl].name, 16);
        } else {
            str_cpy(b, "--", 3);
            str_cpy(nm, "OWN", 4);
        }
        w = text_w(&AF_N, b);
        fa_ltext(14, 214, &AF_N, b, FA_OR);
        fa_ltext(14 + w + 6, 214, &AF_N, nm, FA_CR);
        b[0] = (char)('1' + pg);
        b[1] = '/';
        b[2] = '7';
        b[3] = 0;
        fa_rtext(226, 232, &AF_X, b, FA_RGB(0x7A, 0x71, 0x64));
    }
    if (fa_sv)
        fa_dialog();
    else if (fa_msg_on()) {
        int32_t mw = text_w(&AF_M, fa_msg) + 20;
        cv_rect(120 - mw / 2, 96, mw, 26, FA_OR);
        cv_rect(120 - mw / 2 + 2, 98, mw - 4, 22, FA_RGB(0x1B, 0x19, 0x17));
        fa_ctext(120, 102, &AF_M, fa_msg, FA_OR);
    }
}

static uint32_t fa_signature(void)
{
    const track_t *t = TSEL;
    uint32_t h = 2166136261u, i;
    h = (h ^ fa_page) * 16777619u;
    for (i = 0; i < 7u; i++)
        h = (h ^ fmg_cur[i]) * 16777619u;
    for (i = 0; i < FG_SIZE; i++)
        h = (h ^ fmug_patch[0][i]) * 16777619u;
    for (i = 0; i < P_COUNT; i++)
        h = (h ^ (uint32_t)t->p[i]) * 16777619u;
    h = (h ^ fa_sv) * 16777619u;
    h = (h ^ fa_sv_slot ^ (uint32_t)fa_sv_pos << 8 ^ (uint32_t)t->user << 16 ^ (uint32_t)fa_msg_on() << 24) * 16777619u;
    for (i = 0; i < 12u; i++)
        h = (h ^ (uint8_t)fa_sv_name[i]) * 16777619u;
    return (h ^ t->preset) * 16777619u;
}

static void fa_draw(void)
{
    uint32_t sig = fa_signature(), band;
    if (sig == fa_sig_prev && !ui.force)
        return;
    fa_sig_prev = sig;
    ui.force = 0;
    fmg_force_page = (int8_t)fa_page;
    for (band = 0; band < 2u; band++) {
        cv_begin(240, 120, FA_RGB(0x17, 0x15, 0x13));
        cv_oy = -(int32_t)band * 120;
        cv_cy0 = 0;
        fa_scene();
        cv_oy = 0;
        cv_blit(0, band * 120u);
    }
}

static void fa_splash(void)                              /* power-on: the name, large, and "by Ruppert" under it (3 s: main.c) */
{
    const uint16_t bg = FA_RGB(0x14, 0x12, 0x11);
    lcd_fill(0, 0, 240, 240, bg);
    cv_begin(240, 80, bg);
    cv_oy = 0;
    cv_text_on(120 - text_w(&AF_H, "FMini-1") / 2, 0, &AF_H, "FMini-1", FA_OR, bg);
    cv_blit(0, 60);
    cv_begin(240, 24, bg);
    cv_text_on(120 - text_w(&AF_M, "by Ruppert") / 2, 2, &AF_M, "by Ruppert", FA_CR, bg);
    cv_blit(0, 150);
}

/* ---------------------------------------------------------- control --- */
static void fa_goto(uint32_t pg) { fa_page = (uint8_t)(pg % FA_NPAGE); }

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

/* PRESETS: the previous / next patch of the list (factory F1..F8, then the saved ones), round; in the SAVE dialog the slot */
static void fa_preset(int32_t s)
{
    track_t *t = TSEL;
    uint32_t nu = fa_nuser(), n = FMUG_NFACTORY + nu, cur, nx;
    if (fa_sv) {
        fa_sv_slot = (uint8_t)((fa_sv_slot + UP_SLOTS + (s > 0 ? 1u : UP_SLOTS - 1u)) % UP_SLOTS);
        fa_sv_fill();
        return;
    }
    cur = fa_on_user() ? FMUG_NFACTORY + fa_user_rank((uint32_t)t->user - 1u)
                       : (t->p[P_E7] >= 0 && t->p[P_E7] < (int32_t)FMUG_NFACTORY ? (uint32_t)t->p[P_E7] : 0u);
    nx = (cur + (s > 0 ? 1u : n - 1u)) % n;
    if (nx < FMUG_NFACTORY) {
        t->user = 0;
        apply_preset_to(t, nx);
        t->p[P_E7] = (int16_t)nx;
    } else {
        up_load(fa_user_nth(nx - FMUG_NFACTORY));
    }
    fmug_poll();
}

/* a page button: the tap opens it */
static void fa_buttons(uint32_t pressed)
{
    static const uint8_t PG_BTN[6] = {B_LFO, B_EDIT, B_SCL, B_GLO, B_FX, B_ENV};
    uint32_t i;
    if ((pressed >> panel.btn[B_SAVE]) & 1u) {            /* SAVE: open the dialog; again: write */
        if (fa_sv)
            fa_sv_do();
        else
            fa_sv_open();
        return;
    }
    for (i = 0; i < 6u; i++)
        if ((pressed >> panel.btn[PG_BTN[i]]) & 1u) {
            fa_sv = 0;                                    /* a page button cancels the dialog */
            if (PG_BTN[i] == B_ENV) {
                fa_env_last = (uint8_t)(fa_page == 4u ? 5u : 4u);
                fa_goto(fa_env_last);
            } else {
                static const uint8_t TO[6] = {0, 1, 2, 3, 6, 4};
                fa_goto(TO[i]);
            }
        }
    if ((pressed >> panel.btn[B_OCTDN]) & 1u)
        song.octave = song.octave > -3 ? song.octave - 1 : song.octave;
    if ((pressed >> panel.btn[B_OCTUP]) & 1u)
        song.octave = song.octave < 3 ? song.octave + 1 : song.octave;
}

/* the instrument is monophonic, like the one it follows: one voice, legato (no retrigger while a key is held), the
 * lowest key wins; every patch and every saved preset plays this way whatever its record says */
static void fa_mono(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_VOICE] = V_LEGATO;
        trk[i].p[P_PRIO] = 1;
    }
}

static void fa_input(void)
{
    uint32_t pressed = fm1_input_edges(0), k;
    int32_t s;
    (void)fm1_input_note_edges();
    midi_take();                                            /* MIDI IN notes */
    midi_pc_poll();                                         /* Program Change: a patch */
    fmug_poll();                                            /* SLOT turned: its patch */
    settings_later_poll();
    if (TSEL->eng_req != ENGI_FMUG) {                       /* an old autosave: the instrument is FMUG */
        set_engine_of(TSEL, ENGI_FMUG);
        apply_preset_to(TSEL, 0);
        fmug_poll();
    }
    song.sel = 0;
    fa_mono();
    fmg_force_page = (int8_t)fa_page;
    fa_buttons(pressed);
    if ((s = panel_enc(EN_PRESET)) != 0)
        fa_preset(s);
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
    if ((s = panel_enc(EN_SELECT)) != 0)
        fa_select(s);
    fmg_force_page = (int8_t)fa_page;
    if ((s = panel_enc(EN_ALGO)) != 0)
        fmg_algo(s);
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
        static const uint8_t PG_LED[FA_NPAGE] = {B_LFO, B_EDIT, B_SCL, B_GLO, B_ENV, B_ENV, B_FX};
        led_put(nl, panel.btn[PG_LED[fa_page]], 1);          /* the page's button */
    }
    led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
    led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    c = fm1_in.notes;
    for (k = 0; k < 27u; k++)
        led_put(nl, 14u + k, (int)((c >> k) & 1u));
    for (k = 0; k < FM1_NCOL; k++) {
        fm1_led_dim[k] = nd[k];
        fm1_led_breath[k] = nb[k];
        fm1_led_mid[k] = nm[k];
        fm1_led[k] = nl[k];
    }
}

/* power-on: track 1 is the instrument (the first factory patch), the others are muted FMUG parts */
static void fa_init(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if (t->eng_req != ENGI_FMUG || i) {              /* track 1 keeps its autosaved sound; the rest start fresh and muted */
            set_engine_of(t, ENGI_FMUG);
            apply_preset_to(t, 0);
            t->eng_req = ENGI_FMUG;
            t->engine = t->eng_req;
            fmug_load_slot(i, 0);
            fmug_adopt(i);
        }
        t->p[P_MUTE] = i ? 1 : 0;
    }
    song.sel = 0;
    fa_mono();
    fa_page = 0;
    ui.force = 1;
}
#endif /* FELUCCA_FMUG_ONLY */
