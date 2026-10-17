/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * FMini-1: the FMUG engine's panel (EDIT family, an FMUG track only, after EDIT 2). Seven pages, in the order of a
 * synth panel's sections:  CTRL  OSC  MIX  FILT  F.ENV  A.ENV  FX
 * Each page has four knobs (KNOB 1..4 edit them directly) and, on CTRL, OSC and MIX, extra controls (switches,
 * waveforms) that no knob reaches. SELECT moves a cursor over every control of the page (the four knobs, then the
 * extras; a knob picked lights its card), ALGORITHM changes the control under the cursor (a switch: up = on,
 * down = off). The graph area draws the page as a panel: the side wood, the four knobs as fluted dials, the
 * switches as horizontal rockers (CTRL red, MIX blue, off or on), the waveform boxes.
 * Most controls are bytes of the track's FMUG patch (eng_fmug.c: FG_*), written with fmug_set_patch and made the
 * track's own (SLOT OWN) with one undo per gesture, as ui_fm6op.c does for FM6. The rest are the track's parameters:
 * the engine's E0..E5 (CUTOFF EMPHASIS CONTOUR, the contour's ATTACK DECAY SUSTAIN), the amplitude envelope, the common
 * LFO RATE and the FX sends. Included by ui_graph.c after ui_fm6op.c. */
static void ml_knob(const track_t *t, uint32_t id);              /* (midi_learn.c) */
static uint8_t fmg_cur[7];                                        /* the cursor of each page: 0..3 the knobs, 4.. extras */

enum { FK_BYTE, FK_PAR, FK_SW, FK_NAME, FK_GLIDE, FK_BYP, FK_INS };   /* FK_BYP: a send's on / off (its value kept aside); FK_INS: the INSERT */
typedef struct {
    const char *lb;                                               /* card label (<= 6) */
    uint8_t kind, a, b;                                           /* a: byte index or param id; b: switch bit / names */
    uint8_t icon;
} fmgf_t;
enum { FN_RANGE, FN_WAVE };
#define FMG_NF 9u
static const struct { const char *name; uint8_t n; fmgf_t f[FMG_NF]; } FMG_PAGE[7] = {
    {"CONTROLLERS", 8, {{"TUNE", FK_BYTE, FG_TUNE, 0, ICON_TUNE}, {"GLIDE", FK_GLIDE, FG_GLID, 0, ICON_GLIDE},
                        {"MOD MIX", FK_BYTE, FG_MMIX, 0, ICON_MOD}, {"LFO RATE", FK_PAR, P_LRATE, 0, ICON_RATE},
                        {"MOD OSC", FK_SW, FG_SW1, FS_MOSC, ICON_MOD}, {"MOD FLT", FK_SW, FG_SW1, FS_MFLT, ICON_MOD},
                        {"GLIDE", FK_SW, FG_SW2, FT_GLIDE, ICON_GLIDE}, {"LFO SQR", FK_SW, FG_SW2, FT_SQR, ICON_LFO_WAVE}}},
    {"OSCILLATOR BANK", 7, {{"OSC 1", FK_NAME, FG_R1, FN_RANGE, ICON_OCTAVE}, {"OSC 2", FK_NAME, FG_R2, FN_RANGE, ICON_OCTAVE},
                        {"OSC 3", FK_NAME, FG_R3, FN_RANGE, ICON_OCTAVE}, {"DETUNE", FK_BYTE, FG_F2, 0, ICON_DETUNE},
                        {"OSC 1", FK_NAME, FG_W1, FN_WAVE, ICON_WAVE}, {"OSC 2", FK_NAME, FG_W2, FN_WAVE, ICON_WAVE},
                        {"OSC 3", FK_NAME, FG_W3, FN_WAVE, ICON_WAVE}}},
    {"MIXER", 8, {{"OSC 1", FK_BYTE, FG_L1, 0, ICON_LEVEL}, {"OSC 2", FK_BYTE, FG_L2, 0, ICON_LEVEL},
                  {"OSC 3", FK_BYTE, FG_L3, 0, ICON_LEVEL}, {"NOISE", FK_BYTE, FG_LN, 0, ICON_NOISE},
                  {"OSC 1", FK_SW, FG_SW1, FS_O1, ICON_LEVEL}, {"OSC 2", FK_SW, FG_SW1, FS_O2, ICON_LEVEL},
                  {"OSC 3", FK_SW, FG_SW1, FS_O3, ICON_LEVEL}, {"NOISE", FK_SW, FG_SW1, FS_NZ, ICON_NOISE}}},
    {"FILTER", 4, {{"CUTOFF", FK_PAR, P_E0, 0, ICON_CUTOFF}, {"EMPHASIS", FK_PAR, P_E1, 0, ICON_RESO},
                   {"CONTOUR", FK_PAR, P_E2, 0, ICON_SWEEP}, {"KBD CTRL", FK_BYTE, FG_KBD, 0, ICON_KEYTRACK}}},
    {"FILTER CONTOUR", 4, {{"ATTACK", FK_PAR, P_E3, 0, ICON_ATTACK}, {"DECAY", FK_PAR, P_E4, 0, ICON_DECAY},
                       {"SUSTAIN", FK_PAR, P_E5, 0, ICON_SUSTAIN}, {"AMOUNT", FK_PAR, P_E2, 0, ICON_SWEEP}}},
    {"LOUDNESS CONTOUR", 4, {{"ATTACK", FK_PAR, P_ATK, 0, ICON_ATTACK}, {"DECAY", FK_PAR, P_DEC, 0, ICON_DECAY},
                         {"SUSTAIN", FK_PAR, P_SUS, 0, ICON_SUSTAIN}, {"LEVEL", FK_PAR, P_LEVEL, 0, ICON_LEVEL}}},
    {"EFFECTS", 8, {{"REVERB", FK_PAR, P_REV, 0, ICON_REVERB}, {"DELAY", FK_PAR, P_DLY, 0, ICON_DELAY},
                    {"CHORUS", FK_PAR, P_CHOR, 0, ICON_CHORUS}, {"DRIVE", FK_PAR, P_DIST, 0, ICON_DRIVE},
                    {"REV", FK_BYP, P_REV, 0, ICON_REVERB}, {"DLY", FK_BYP, P_DLY, 1, ICON_DELAY},
                    {"CHO", FK_BYP, P_CHOR, 2, ICON_CHORUS}, {"INSERT", FK_INS, P_ITYPE, 0, ICON_DRIVE}}},
};
static const char *const FMG_RANGE[6] = {"LO", "32'", "16'", "8'", "4'", "2'"};
static const char *const FMG_WAVE[6] = {"TRI", "SHARK", "SAW", "SQR", "PLS W", "PLS N"};

static int fmg_page(uint32_t g) { return g >= GR_FMG0 && g <= GR_FMG6; }
static int fmg_ok(void) { return TSEL->eng_req % NENGINES == ENGI_FMUG; }
static int8_t fmg_force_page = -1;                               /* FELUCCA_FMUG_ONLY: fmug_app.c's page */
static uint32_t fmg_pg(void) { return fmg_force_page >= 0 ? (uint32_t)fmg_force_page % 7u : (cur_page()->graph - GR_FMG0) % 7u; }
static uint32_t fmg_count(uint32_t pg) { return FMG_PAGE[pg % 7u].n; }
static const fmgf_t *fmg_field(uint32_t pg, uint32_t i) { return &FMG_PAGE[pg % 7u].f[i % FMG_NF]; }
/* the control under the page's cursor */
static uint32_t fmg_curi(uint32_t pg) { return fmg_cur[pg % 7u] % fmg_count(pg); }

/* byte b of the selected track's patch = v: the track's own patch now (SLOT OWN), one undo per gesture */
static void fmg_write(uint32_t b, uint32_t v)
{
    uint32_t tr = song.sel % NTRK;
    track_t *t = TSEL;
    uint8_t p[FG_SIZE];
    if (b >= FG_NUM || fmug_patch[tr][b] == v)
        return;
    edit_undo_take(t, 0x2000u | b, UNDO_SOUND);
    memcpy(p, fmug_patch[tr], FG_SIZE);
    p[b] = (uint8_t)v;
    fmug_set_patch(tr, p);
    fmug_own_ok &= (uint8_t)~(1u << tr);
    fmug_slot[tr] = FMUG_OWN;
    t->p[P_E7] = FMUG_OWN;
    motion_undo_done(t);
}
static uint32_t fmg_byte(uint32_t b) { return fmug_patch[song.sel % NTRK][b % FG_SIZE]; }
static uint8_t fmg_shadow[3];                                    /* a send's value while its switch is off (REV DLY CHO) */
static int fmg_sw_on(const fmgf_t *f)
{
    if (f->kind == FK_BYP)
        return TSEL->p[f->a] > 0;
    if (f->kind == FK_INS)
        return TSEL->p[P_ITYPE] != 0;
    return (fmg_byte(f->a) & f->b) != 0u;
}
/* the GLIDE switch and knob drive the part's common GLIDE time (voice.c): on = the kept time, off = none */
static void fmg_glide_sync(void)
{
    track_t *t = TSEL;
    int16_t v = (int16_t)((fmg_byte(FG_SW2) & FT_GLIDE) ? fmg_byte(FG_GLID) : 0u);
    if (t->p[P_GLIDE] != v) {
        t->p[P_GLIDE] = v;
        motion_capture(t, P_GLIDE, v);
    }
}

/* FMini-1 (the app's own screen): speed-dependent steps. A slow turn is 2 values a detent (a 0..127 knob crosses its
 * range in ~64 detents), a faster one 3, 5, 8, 12, 18 by the time per detent, always in the turn's direction;
 * a reversal or a pause starts again slow. Names and switches are one step a detent. */
static uint32_t fmg_tl[16];
static int32_t fmg_acc(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ms, dt, a = (uint32_t)(s < 0 ? -s : s), last = fmg_tl[role & 15u];
    int32_t m = 2;
    if (fmg_force_page < 0 || range <= 32 || !s)
        return s;
    dt = (now - (last & 0x7FFFFFFFu)) & 0x7FFFFFFFu;
    fmg_tl[role & 15u] = (now & 0x7FFFFFFFu) | (s > 0 ? 0x80000000u : 0u);
    if (!last || ((last >> 31) != (s > 0 ? 1u : 0u)) || dt > 150u)
        return s * 2;
    dt /= a;
    m = dt >= 80u ? 2 : dt >= 50u ? 3 : dt >= 30u ? 5 : dt >= 18u ? 8 : dt >= 9u ? 12 : 18;
    return s * m;
}

/* control i of page pg moved s steps (a knob of the page, or ALGORITHM on the cursor's control) */
static void fmg_turn(uint32_t pg, uint32_t i, int32_t s, uint32_t role)
{
    const fmgf_t *f = fmg_field(pg, i);
    track_t *t = TSEL;
    if (!fmg_ok() || !s)
        return;
    switch (f->kind) {
    case FK_SW: {
        uint32_t v = fmg_byte(f->a);
        fmg_write(f->a, s > 0 ? v | f->b : v & ~(uint32_t)f->b);
        if (f->a == FG_SW2)
            fmg_glide_sync();
        break;
    }
    case FK_BYP: {                                    /* a send on (its value back, 40 first time) / off (kept aside) */
        int16_t *vp = &t->p[f->a];
        if (s > 0 && !*vp)
            *vp = (int16_t)(fmg_shadow[f->b % 3u] ? fmg_shadow[f->b % 3u] : 40);
        else if (s < 0 && *vp) {
            fmg_shadow[f->b % 3u] = (uint8_t)*vp;
            *vp = 0;
        } else
            break;
        motion_capture(t, f->a, *vp);
        ml_knob(t, f->a);
        break;
    }
    case FK_INS:                                      /* the track's INSERT: a phaser on / off */
        t->p[P_ITYPE] = (int16_t)(s > 0 ? IT_PHASER : 0);
        t->p[P_IMIX] = 127;
        motion_capture(t, P_ITYPE, t->p[P_ITYPE]);
        break;
    case FK_NAME:
        fmg_write(f->a, (uint32_t)clamp((int32_t)fmg_byte(f->a) + s, 0, 5));   /* a detent each */
        break;
    case FK_PAR: {
        const param_desc_t *d = track_desc(TSEL, f->a);
        int16_t *vp = &t->p[f->a];
        *vp = (int16_t)param_turn(d, *vp, fmg_acc(role, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
        motion_capture(t, f->a, *vp);
        ml_knob(t, f->a);
        break;
    }
    default:                                                      /* FK_BYTE, FK_GLIDE */
        fmg_write(f->a, (uint32_t)clamp((int32_t)fmg_byte(f->a) + fmg_acc(role, s, 127), 0, 127));
        if (f->kind == FK_GLIDE)
            fmg_glide_sync();
        break;
    }
}
/* KNOB slot (0..3) turned s steps on an FMUG page */
static void fmg_knob(uint32_t slot, int32_t s) { fmg_turn(fmg_pg(), slot & 3u, s, EN_K1 + slot); }
/* SELECT: the cursor goes s steps round the page's controls; ALGORITHM: the control under it */
static void fmg_select(int32_t s)
{
    uint32_t pg = fmg_pg(), n = fmg_count(pg);
    int32_t c = (int32_t)fmg_curi(pg) + (s > 0 ? 1 : -1);
    fmg_cur[pg] = (uint8_t)(c < 0 ? n - 1u : c >= (int32_t)n ? 0u : (uint32_t)c);
    if (fmg_cur[pg] < 4u) {                                       /* a knob: its card lights */
        ui.hot_col = fmg_cur[pg];
        ui.hot_t = 40;
    }
}
static void fmg_algo(int32_t s)
{
    uint32_t pg = fmg_pg(), c = fmg_curi(pg);
    fmg_turn(pg, c, s, EN_ALGO);
    if (c < 4u) {
        ui.hot_col = (uint8_t)c;
        ui.hot_t = 40;
    }
}

/* control (pg, i)'s range */
static void fmg_range(uint32_t pg, uint32_t i, int32_t *lo, int32_t *hi)
{
    const fmgf_t *f = fmg_field(pg, i);
    *lo = 0;
    *hi = f->kind == FK_SW || f->kind == FK_BYP || f->kind == FK_INS ? 1 : f->kind == FK_NAME ? 5 : 127;
    if (f->kind == FK_PAR) {
        *lo = track_desc(TSEL, f->a)->min;
        *hi = track_desc(TSEL, f->a)->max;
    }
}

/* tenths with a sign: "+1.5" */
static void fmg_tenths(char *s, int32_t x)
{
    if (x < 0) {
        *s++ = '-';
        x = -x;
    } else {
        *s++ = '+';
    }
    fmt_int(s, x / 10);
    s += str_len(s);
    *s++ = '.';
    *s++ = (char)('0' + x % 10);
    *s = 0;
}

/* control (pg, i) as a card: label, value (12), unit, gauge 0..1000 / -1, icon; vo >= 0: as if its byte held vo
 * (MENU > LARGE's widest value, ui_draw.c span_fmg) */
static void fmg_card_at(uint32_t pg, uint32_t i, int32_t vo, const char **label, char *val, const char **unit, int32_t *ratio,
                        uint32_t *icon)
{
    const fmgf_t *f = fmg_field(pg, i);
    const track_t *t = TSEL;
    int32_t v;
    *label = f->lb;
    *unit = "";
    *ratio = -1;
    *icon = f->icon;
    switch (f->kind) {
    case FK_PAR: {
        const param_desc_t *d = track_desc(TSEL, f->a);
        v = vo >= 0 ? vo : t->p[f->a];
        param_format(d, v, val, unit);
        *ratio = RATIO(d, v);
        return;
    }
    case FK_SW:
    case FK_BYP:
    case FK_INS:
        v = vo >= 0 ? vo : (int32_t)fmg_sw_on(f);
        str_cpy(val, v ? "ON" : "OFF", 12);
        return;
    case FK_NAME:
        v = clamp(vo >= 0 ? vo : (int32_t)fmg_byte(f->a), 0, 5);
        str_cpy(val, f->b == FN_RANGE ? FMG_RANGE[v] : FMG_WAVE[v], 12);
        *ratio = v * 200;
        return;
    default:
        break;
    }
    v = vo >= 0 ? vo : (int32_t)fmg_byte(f->a);
    *ratio = v * 1000 / 127;
    if (f->a == FG_F2 || f->a == FG_F3) {
        fmg_tenths(val, (v - 64) * 70 / 63);
        *unit = "ST";
    } else if (f->a == FG_TUNE) {
        v = (v - 64) * 100 / 64;                     /* cents */
        val[0] = v > 0 ? '+' : 0;
        fmt_int(val + (v > 0), v);
        *unit = "CT";
        return;
    } else if (f->a == FG_MMIX) {
        str_cpy(val, v < 3 ? "LFO" : v > 124 ? "NOISE" : "", 12);
        if (!val[0])
            fmt_int(val, v);
    } else {
        fmt_int(val, v);
    }
}
static void fmg_card(uint32_t slot, const char **label, char *val, const char **unit, int32_t *ratio, uint32_t *icon)
{
    fmg_card_at(fmg_pg(), slot & 3u, -1, label, val, unit, ratio, icon);
}

/* ------------------------------------------------------------- drawing --- */
/* the wood and the hardware are fixed colours; GREY and MONO keep to their greys through the theme's own */
#define FMG_PLAIN (ux.mono || ux.bw)
#define FMG_WOOD (FMG_PLAIN ? T_RAISE : (uint16_t)0x7A44)
#define FMG_WOOD2 (FMG_PLAIN ? T_LINE : (uint16_t)0x9306)
#define FMG_BLUE (FMG_PLAIN ? T_THEME : (uint16_t)0x2BFC)

/* a fluted dial (the vintage knob): centre cx, cy; r 0..1000 the pointer's place along its 270 degrees */
static void fmg_dial(int32_t cx, int32_t cy, int32_t r, int32_t pos, int hot)
{
    int32_t a = (pos * 270 / 1000 + 225) % 360, i = a * 1024 / 360, px, py, k;
    int32_t sn = SINE[i & 1023], cs = SINE[(i + 256) & 1023];
    if (hot)
        cv_rrect(cx - r - 3, cy - r - 3, 2 * r + 7, 2 * r + 7, r + 3, T_ACCENT, T_SURF);
    cv_rrect(cx - r - 1, cy - r - 1, 2 * r + 3, 2 * r + 3, r + 1, FMG_WOOD2, T_SURF);       /* the skirt */
    cv_rrect(cx - r, cy - r, 2 * r + 1, 2 * r + 1, r, T_BG, FMG_WOOD2);                       /* the black body */
    for (k = 0; k < 12; k++) {                                                                 /* the flutes */
        int32_t b = k * 1024 / 12;
        cv_pset(cx + (SINE[b & 1023] * (r - 1)) / 32768, cy - (SINE[(b + 256) & 1023] * (r - 1)) / 32768, T_DIM);
    }
    cv_rrect(cx - r / 2, cy - r / 2, r + 1, r + 1, r / 2, T_TEXT, T_BG);                       /* the silver cap */
    px = cx + (sn * (r - 2)) / 32768;
    py = cy - (cs * (r - 2)) / 32768;
    cv_line_t(cx, cy, px, py, T_REC, 2);                                                       /* the pointer */
}
/* a horizontal rocker: w x 14 at x, y; colour hue always (on or off), the lever to the right when on */
static void fmg_rocker(int32_t x, int32_t y, int32_t w, uint16_t hue, int on, int hot)
{
    if (hot)
        cv_rrect(x - 3, y - 3, w + 6, 20, 5, T_ACCENT, T_SURF);
    cv_rrect(x, y, w, 14, 3, hue, hot ? T_ACCENT : T_SURF);
    cv_rrect(on ? x + w - 17 : x + 3, y + 2, 14, 10, 2, on ? T_TEXT : T_BG, hue);              /* the lever */
    cv_rect(on ? x + w - 12 : x + 8, y + 4, 4, 6, on ? hue : T_DIM);
}
/* a text centred in [x, x + w) */
static void fmg_ctext(int32_t x, int32_t w, int32_t y, const char *s, uint16_t c)
{
    int32_t tw = text_w(&AF_S, s);
    cv_text(x + (w - tw) / 2, y, &AF_S, s, c);
}

static void graph_fmug(const track_t *t, uint16_t c)
{
    uint32_t pg = fmg_pg(), n = fmg_count(pg), cur = fmg_curi(pg), i, ex = n > 4u ? n - 4u : 0u;
    int32_t x, w;
    char val[16], pn[20];
    const char *lb, *un;
    int32_t ratio;
    uint32_t ic;
    (void)c;
    cv_oy = 0;
    cv_rect(3, 0, 8, 122, FMG_WOOD);                                 /* the wood of the sides */
    cv_rect(229, 0, 8, 122, FMG_WOOD);
    cv_rect(8, 0, 1, 122, FMG_WOOD2);
    cv_rect(231, 0, 1, 122, FMG_WOOD2);
    fmg_ctext(14, 212, 1, FMG_PAGE[pg].name, T_MID);
    for (i = 0; i < 4u; i++) {                                       /* the four knobs */
        x = 14 + (int32_t)i * 53 + 26;
        fmg_card_at(pg, i, -1, &lb, val, &un, &ratio, &ic);
        fmg_dial(x, 31, 13, ratio < 0 ? 0 : ratio, cur == i);
        fmg_ctext(x - 26, 53, 46, FMG_PAGE[pg].f[i].lb, cur == i ? T_ACCENT : T_TEXT);
        fmg_ctext(x - 26, 53, 58, val, T_MID);
    }
    if (pg == 1u) {                                                  /* the waveform boxes and OSC 3's pitch */
        for (i = 0; i < 3u; i++) {
            x = 16 + (int32_t)i * 70;
            fmg_card_at(pg, 4u + i, -1, &lb, val, &un, &ratio, &ic);
            cv_rrect(x, 74, 64, 18, 4, cur == 4u + i ? T_ACCENT : T_RAISE, T_SURF);
            cv_rrect(x + 1, 75, 62, 16, 3, T_BG, cur == 4u + i ? T_ACCENT : T_RAISE);
            fmg_ctext(x, 64, 77, val, cur == 4u + i ? T_ACCENT : T_TEXT);
        }
        fmg_card_at(pg, 7, -1, &lb, val, &un, &ratio, &ic);
        pn[0] = 'O'; pn[1] = 'S'; pn[2] = 'C'; pn[3] = '3'; pn[4] = ' ';
        str_cpy(pn + 5, val, 8);
        fmg_ctext(14, 212, 92, pn, cur == 7u ? T_ACCENT : T_MID);
    } else if (ex) {                                                 /* the rockers: CTRL red, MIX blue */
        uint16_t hue = pg == 0u ? T_REC : FMG_BLUE;
        w = ex > 4u ? 36 : 44;
        for (i = 0; i < ex; i++) {
            const fmgf_t *f = fmg_field(pg, 4u + i);
            x = 14 + (int32_t)i * (212 / (int32_t)ex) + (212 / (int32_t)ex - w) / 2;
            fmg_rocker(x, 76, w, hue, fmg_sw_on(f), cur == 4u + i);
            fmg_ctext(x - 6, w + 12, 91, f->lb, cur == 4u + i ? T_ACCENT : T_MID);
        }
    }
    {   /* the foot: the patch's place and name bottom left, the page "n/7" small bottom right */
        const preset_t *ps = &ENGINES[ENGI_FMUG]->presets[t->preset % ENGINES[ENGI_FMUG]->npresets];
        int32_t sl = t->p[P_E7];
        if (sl >= 0 && sl < (int32_t)FMUG_NFACTORY) {
            pn[0] = 'F';
            fmt_int(pn + 1, sl + 1);
            str_cpy(pn + str_len(pn), " ", 2);
            str_cpy(pn + str_len(pn), ps->name, 12);
        } else {
            str_cpy(pn, "OWN", 4);
        }
        cv_text(16, 106, &AF_S, pn, T_TEXT);
        pn[0] = (char)('1' + pg);
        pn[1] = '/';
        pn[2] = '7';
        pn[3] = 0;
        cv_text_r(224, 106, &AF_S, pn, T_DIM, T_SURF);
    }
}
