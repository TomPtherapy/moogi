/* SPDX-License-Identifier: GPL-3.0-only
 * Acid Moogi m6: the drum machine (fmini_app.c includes this file after fmini_seq.c; FELUCCA_FMINI_ONLY). The ideas of the
 * author's euclidean drum script for norns (the project notes keep it as drum_machine.lua), on track 2: Felucca's DRUM engine,
 * its 8 lanes KICK SNARE CLAP HAT CL HAT OP TOM RIM BELL. The sequencer plays track 2's 16 steps; seq.c does the mutes on the
 * quarter, the chance and the audition (fa_dr_mute, fa_dr_pend, fa_dr_chance, fa_dr_aud). This file is the beat and the mode.
 *
 * THE BEAT: each lane has HITS (0..16) and ROTATE (0..15) spread over 16 steps (euclidean, as the script's euclid_hit), and
 *   each quarter of a lane (steps 1-4, 5-8, 9-12, 13-16) its own HITS (0..4) and ROTATE (0..3), or "=" (the lane's). Always 16
 *   steps of 1/16, whatever the bass's length and STEP. No hits by hand: the steps are computed (fa_dr_write).
 * THE DRUM MODE: SEQ twice (within 0.4 s) turns it on and off; HOME or a page button leaves it too. The drums play in it or
 *   out of it. In it the keys play no notes (seq.c kb_lock):
 *   - black keys 1..8 the lanes: a press selects (stopped, it sounds); twice: mute / unmute, on the next quarter while playing.
 *   - SELECT HITS, ALGORITHM ROTATE of the selected lane, PRESETS its VOLUME (the lane's LEVEL, P_LN0..7).
 *   - a white key held: SELECT and ALGORITHM are its quarter's (HITS below 0: "=").
 *   - a black key held: PRESETS is its lane's CHANCE (0..100).
 *   - KNOB 1..4 the filter (FILT's), as on HOME. SEQ held: the bass's layer. EDIT held, the acid line on: the acid line.
 *   The screen: the filter's four knobs above, the 8 x 16 beat below, the selected lane's values at the foot (no step
 *   marker: the step playing shows on the white keys). The LEDs: the white keys the selected lane's hits (dim) and the step
 *   playing (lit); the black keys 1..8 the lanes heard (blinking: a mute waiting for its quarter).
 * GLO > DRUM KIT: the kit (STD, 80 10 66 55 77, DEEP TIGHT PERC).
 * SAVED at power-off (fa_dr_byte: rows 0..6 of favorites.factory, the favourites of engines 0..6, which FMINI never shows):
 *   0 0xD7, 1 the kit, 2..9 VOLUME, 10..17 CHANCE, 18.. 5 bytes a lane: HITS, ROTATE, the quarters (5 bits each: HITS + 1
 *   in bits 0..2, 0 = "=", ROTATE in bits 3..4), 58..217 kept for the fills (stage 2). Each value is checked on load: a wrong
 *   one is its default. The mutes are not saved (power-on: every lane heard). */

/* fa_dr (the drum mode is on): fmini_app.c, above the include of fmini_seq.c */
static uint8_t fa_dr_sel;                                 /* the lane selected, 0..7 */
static uint8_t fa_dr_n[NLANE], fa_dr_r[NLANE];            /* HITS 0..16, ROTATE 0..15 */
static int8_t fa_dr_qn[NLANE][4];                         /* a quarter's HITS 0..4, -1 "=" */
static uint8_t fa_dr_qr[NLANE][4];                        /* .. its ROTATE 0..3 */
static uint8_t fa_dr_lv[NLANE];                           /* VOLUME: the lane's LEVEL, 0..127 (127: the kit as designed) */
static uint8_t fa_dr_kit;                                 /* the kit, eng_drum.c's stored KIT value */
static uint8_t fa_dr_dirty;                               /* changed: saved a moment after the last change */
static uint32_t fa_dr_t_ch;
static uint8_t fa_dr_bk = 0xFFu;                          /* the last black key pressed, and when (twice: the mute) */
static uint32_t fa_dr_bk_t;
static uint32_t fa_dr_sq_t;                               /* SEQ's last press (twice: the mode) */

/* the script's starting beat: KICK 4, SNARE 2 on beats 2 and 4, HAT CL 8, its PERC (3) on RIM */
static const uint8_t FA_DR_DEF[NLANE][2] = {{4, 0}, {2, 4}, {0, 0}, {8, 0}, {0, 0}, {0, 0}, {3, 0}, {0, 0}};
static const char *const FA_DR_NAME[NLANE] = {"KICK", "SNARE", "CLAP", "HAT CL", "HAT OP", "TOM", "RIM", "BELL"};
static const char *const FA_DR_SHORT[NLANE] = {"KK", "SN", "CP", "CH", "OH", "TM", "RM", "BL"};
#define FA_DR_NKIT 9u
static const uint8_t FA_DR_KITS[FA_DR_NKIT] = {DK_STD, DK_80, DK_10, DK_66, DK_55, DK_77, DK_DEEP, DK_TIGHT, DK_PERC};

static uint8_t *fa_dr_byte(uint32_t i) { return &favorites.factory[(i / 32u) % 7u][i % 32u]; }

/* euclidean: hits spread over n steps, the first on step rot; step i (0..n-1) of it (the script's euclid_hit) */
static int fa_dr_eu(uint32_t hits, uint32_t rot, uint32_t n, uint32_t i)
{
    uint32_t j;
    if (!hits)
        return 0;
    j = (i + n - rot % n) % n;
    return (j * hits) % n < hits;
}
/* lane l on step i (0..15): its quarter's own, or the lane's */
static int fa_dr_hit(uint32_t l, uint32_t i)
{
    uint32_t q = (i >> 2) & 3u;
    if (fa_dr_qn[l][q] >= 0)
        return fa_dr_eu((uint32_t)fa_dr_qn[l][q], fa_dr_qr[l][q], 4, i & 3u);
    return fa_dr_eu(fa_dr_n[l], fa_dr_r[l], 16, i);
}
/* the beat into track 2's 16 steps (the main loop; the audio ISR reads one byte a step) */
static void fa_dr_write(void)
{
    step_t *st = trk[FA_DR_TRK].step;
    uint32_t i, l, m;
    for (i = 0; i < 16u; i++) {
        for (m = 0, l = 0; l < NLANE; l++)
            m |= (uint32_t)fa_dr_hit(l, i) << l;
        st[i].hit = (uint8_t)m;
    }
}
static int fa_dr_kit_ok(uint32_t v)
{
    uint32_t i;
    for (i = 0; i < FA_DR_NKIT; i++)
        if (FA_DR_KITS[i] == v)
            return 1;
    return 0;
}

/* track 2 as the drums: DRUM, the kit, the lanes' levels, no sends, no ARP, 16 steps of 1/16, heard (power-on, and
 * whenever something else set another engine there) */
static void fa_dr_setup(void)
{
    track_t *t = &trk[FA_DR_TRK];
    uint32_t i;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    fm1_irq_off();
    for (i = 0; i < NSTEP; i++) {
        memset(&t->step[i], 0, sizeof t->step[i]);
        t->step[i].time = i < 16u ? ST_NOTE : ST_REST;
    }
    fm1_irq_on();
    t->p[P_E0] = fa_dr_kit;
    for (i = 0; i < NLANE; i++)
        t->p[P_LN0 + i] = fa_dr_lv[i];
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;   /* (dry: the delay and the reverb are the bass's) */
    t->p[P_AMODE] = 0;
    t->p[P_AHOLD] = 0;
    t->p[P_SLEN] = 16;
    t->p[P_SDIV] = 2;                                     /* 1/16 */
    t->p[P_SGATE] = 64;                                   /* (a hit rings whatever the gate: a oneshot) */
    t->p[P_SSWING] = 0;
    t->p[P_SQNT] = 1;
    t->p[P_MUTE] = 0;
    t->p[P_LEVEL] = TP[P_LEVEL].def;                      /* (a load keeps LEVEL and PAN: track 2's old ones could be 0) */
    t->p[P_PAN] = TP[P_PAN].def;
    fa_dr_mute = 0;
    fa_dr_pend = 0;
    fa_dr_write();
}

static void fa_dr_cfg_load(void)
{
    uint32_t l, q, ok = *fa_dr_byte(0) == 0xD7u;
    fa_dr_kit = DK_STD;
    if (ok && fa_dr_kit_ok(*fa_dr_byte(1)))
        fa_dr_kit = *fa_dr_byte(1);
    for (l = 0; l < NLANE; l++) {
        uint32_t lv = *fa_dr_byte(2u + l), ch = *fa_dr_byte(10u + l), n = *fa_dr_byte(18u + 5u * l),
                 r = *fa_dr_byte(19u + 5u * l), pk = (uint32_t)*fa_dr_byte(20u + 5u * l) |
                 (uint32_t)*fa_dr_byte(21u + 5u * l) << 8 | (uint32_t)*fa_dr_byte(22u + 5u * l) << 16;
        fa_dr_lv[l] = (uint8_t)(ok && lv <= 127u ? lv : 127u);
        fa_dr_chance[l] = (uint8_t)(ok && ch <= 100u ? ch : 100u);
        fa_dr_n[l] = (uint8_t)(ok && n <= 16u ? n : FA_DR_DEF[l][0]);
        fa_dr_r[l] = (uint8_t)(ok && r <= 15u ? r : FA_DR_DEF[l][1]);
        for (q = 0; q < 4u; q++) {
            uint32_t c = (pk >> (5u * q)) & 31u, h = c & 7u;
            fa_dr_qn[l][q] = (int8_t)(ok && h >= 1u && h <= 5u ? (int32_t)h - 1 : -1);
            fa_dr_qr[l][q] = (uint8_t)(fa_dr_qn[l][q] >= 0 ? (c >> 3) & 3u : 0u);
        }
    }
}
static void fa_dr_cfg_save(void)
{
    uint32_t l, q;
    *fa_dr_byte(0) = 0xD7;
    *fa_dr_byte(1) = fa_dr_kit;
    for (l = 0; l < NLANE; l++) {
        uint32_t pk = 0;
        *fa_dr_byte(2u + l) = fa_dr_lv[l];
        *fa_dr_byte(10u + l) = fa_dr_chance[l];
        *fa_dr_byte(18u + 5u * l) = fa_dr_n[l];
        *fa_dr_byte(19u + 5u * l) = fa_dr_r[l];
        for (q = 0; q < 4u; q++)
            if (fa_dr_qn[l][q] >= 0)
                pk |= (((uint32_t)fa_dr_qn[l][q] + 1u) | (uint32_t)fa_dr_qr[l][q] << 3) << (5u * q);
        *fa_dr_byte(20u + 5u * l) = (uint8_t)pk;
        *fa_dr_byte(21u + 5u * l) = (uint8_t)(pk >> 8);
        *fa_dr_byte(22u + 5u * l) = (uint8_t)(pk >> 16);
    }
}
static void fa_dr_save_now(void)
{
    fa_dr_dirty = 0;
    fa_cfg_dirty = 1;
    fa_cfg_save();                                        /* (fa_cfg_save writes fa_dr_cfg_save's bytes too) */
}
static void fa_dr_changed(void)
{
    fa_dr_write();
    fa_dr_dirty = 1;
    fa_dr_t_ch = fm1_ms;
}

/* ------------------------------------------------------------ the controls --- */
static int32_t fa_dr_sgn(int32_t s) { return s > 0 ? 1 : -1; }
static int32_t fa_dr_held_q(void)                         /* the quarter of the (first) white key held, -1 none */
{
    uint32_t p;
    for (p = 0; p < 16u; p++)
        if ((fm1_in.notes >> fa_white_key(p)) & 1u)
            return (int32_t)(p >> 2);
    return -1;
}
static int32_t fa_dr_held_lane(void)                      /* the lane of the (first) black key 1..8 held, -1 none */
{
    uint32_t l;
    for (l = 0; l < NLANE; l++)
        if ((fm1_in.notes >> fa_black_key(l)) & 1u)
            return (int32_t)l;
    return -1;
}
/* what quarter q of lane l plays as "=" (the lane's): its hits and the rotation that gives them (the script's
 * quarter_now), so turning HITS on a "=" quarter starts from what it plays */
static void fa_dr_q_now(uint32_t l, uint32_t q, int32_t *hits, uint32_t *rot)
{
    uint32_t i, n = 0, pat = 0, r, m;
    for (i = 0; i < 4u; i++)
        if (fa_dr_eu(fa_dr_n[l], fa_dr_r[l], 16, q * 4u + i)) {
            pat |= 1u << i;
            n++;
        }
    *hits = (int32_t)n;
    *rot = 0;
    for (r = 0; r < 4u; r++) {
        for (m = 0, i = 0; i < 4u; i++)
            m |= (uint32_t)fa_dr_eu(n, r, 4, i) << i;
        if (m == pat) {
            *rot = r;
            return;
        }
    }
}
static void fa_dr_turn_hits(int32_t s)                    /* SELECT */
{
    uint32_t l = fa_dr_sel;
    int32_t q = fa_dr_held_q(), h;
    if (q < 0) {
        fa_dr_n[l] = (uint8_t)clamp((int32_t)fa_dr_n[l] + s, 0, 16);
    } else if (fa_dr_qn[l][q] < 0) {                      /* "=": locked from what it plays now */
        uint32_t r;
        fa_dr_q_now(l, (uint32_t)q, &h, &r);
        h += fa_dr_sgn(s);
        if (h >= 0) {
            fa_dr_qn[l][q] = (int8_t)clamp(h, 0, 4);
            fa_dr_qr[l][q] = (uint8_t)r;
        }
    } else {
        h = fa_dr_qn[l][q] + fa_dr_sgn(s);
        fa_dr_qn[l][q] = (int8_t)(h < 0 ? -1 : clamp(h, 0, 4));   /* below 0: back to the lane's */
        if (h < 0)
            fa_dr_qr[l][q] = 0;
    }
    fa_dr_changed();
}
static void fa_dr_turn_rot(int32_t s)                     /* ALGORITHM */
{
    uint32_t l = fa_dr_sel;
    int32_t q = fa_dr_held_q();
    if (q < 0)
        fa_dr_r[l] = (uint8_t)(((int32_t)fa_dr_r[l] + s % 16 + 16) % 16);
    else if (fa_dr_qn[l][q] >= 0)
        fa_dr_qr[l][q] = (uint8_t)((fa_dr_qr[l][q] + 4u + (s > 0 ? 1u : 3u)) % 4u);
    else
        return;                                           /* (a "=" quarter has no rotation of its own) */
    fa_dr_changed();
}
static void fa_dr_turn_preset(int32_t s)                  /* PRESETS: VOLUME, or a black key held: its CHANCE */
{
    int32_t l = fa_dr_held_lane();
    if (l >= 0) {
        fa_dr_chance[l] = (uint8_t)clamp((int32_t)fa_dr_chance[l] + 5 * s, 0, 100);
    } else {
        fa_dr_lv[fa_dr_sel] = (uint8_t)clamp((int32_t)fa_dr_lv[fa_dr_sel] + 4 * s, 0, 127);
        trk[FA_DR_TRK].p[P_LN0 + fa_dr_sel] = fa_dr_lv[fa_dr_sel];
    }
    fa_dr_changed();
}
static void fa_dr_kit_turn(int32_t s)                     /* GLO > DRUM KIT */
{
    uint32_t i, at = 0;
    for (i = 0; i < FA_DR_NKIT; i++)
        if (FA_DR_KITS[i] == fa_dr_kit)
            at = i;
    at = (uint32_t)clamp((int32_t)at + fa_dr_sgn(s), 0, FA_DR_NKIT - 1);
    fa_dr_kit = FA_DR_KITS[at];
    trk[FA_DR_TRK].p[P_E0] = fa_dr_kit;
    fa_dr_changed();
}
static void fa_dr_mute_toggle(uint32_t l)
{
    fm1_irq_off();
    if (song.playing)
        fa_dr_pend ^= (uint8_t)(1u << l);                 /* on the next quarter (seq.c fa_dr_quarter) */
    else
        fa_dr_mute ^= (uint8_t)(1u << l);
    fm1_irq_on();
}
/* key k pressed in the drum mode: a black key 1..8 selects its lane (stopped: it sounds), twice mutes it. The white keys
 * act while held (fa_dr_held_q); black 9..11 are free */
static void fa_dr_key(uint32_t k)
{
    uint32_t p = key_place(k);
    if (!key_black(k) || p >= NLANE)
        return;
    if (fa_dr_bk == p && (uint32_t)(fm1_ms - fa_dr_bk_t) < 400u) {
        fa_dr_mute_toggle(p);
        fa_dr_bk = 0xFFu;
    } else {
        fa_dr_bk = (uint8_t)p;
        fa_dr_bk_t = fm1_ms;
    }
    fa_dr_sel = (uint8_t)p;
    if (!song.playing)
        fa_dr_aud = (uint8_t)(p + 1u);
}
/* each frame: track 2 stays the drums, the keys are the mode's while it is on, the beat saved a moment after a change */
static void fa_dr_poll(void)
{
    if (trk[FA_DR_TRK].eng_req != ENGI_DRUM)
        fa_dr_setup();
    kb_lock = (uint8_t)(fa_dr ? 1u : 0u);
    if (fa_dr_dirty && (uint32_t)(fm1_ms - fa_dr_t_ch) >= 2000u)
        fa_dr_save_now();
}

/* -------------------------------------------------------------- the screen --- */
/* the drum screen: the header, the filter's four knobs (KNOB 1..4) above, the beat below (a lane a row, 16 steps in four
 * quarters), the selected lane's values at the foot */
static void fa_dr_draw(void)
{
    uint32_t l, i, q;
    int32_t hq = fa_dr_held_q(), hl = fa_dr_held_lane();
    uint8_t heard = (uint8_t)~fa_dr_mute;
    char b[40];
    cv_rect(8, 0, 224, 22, FA_RGB(0x0E, 0x0D, 0x0C));     /* the header: the logo, DRUMS and the kit */
    {
        int32_t lx = 12, ly = 11 - AF_X_CAP_Y - AF_X_CAP_H / 2 + 1, w;
        cv_text_on(lx, ly, &AF_X, "Acid", FA_OR, FA_RGB(0x0E, 0x0D, 0x0C));
        lx += text_w(&AF_X, "Acid") + 3;
        cv_text_on(lx, ly, &AF_X, "Moogi", FA_CR, FA_RGB(0x0E, 0x0D, 0x0C));
        str_cpy(b, "KIT ", 8);
        str_cpy(b + 4, N_DRUM_KIT[fa_dr_kit % DK_COUNT], 8);
        w = text_w(&AF_X, b);
        cv_text_on(226 - w, ly, &AF_X, b, FA_MU, FA_RGB(0x0E, 0x0D, 0x0C));
        w = text_w(&AF_X, "DRUMS") + 10;
        cv_rect(150 - w / 2, 2, w, 18, FA_OR);
        cv_text_on(150 - text_w(&AF_X, "DRUMS") / 2, ly, &AF_X, "DRUMS", FA_RGB(0x1A, 0x0F, 0x06), FA_OR);
    }
    for (i = 0; i < 4u; i++)                              /* the filter, smaller: KNOB 1..4 */
        fa_knob_col_at(3u, i, 36 + (int32_t)i * 56, 0, 66, 26, 98, 0);
    cv_rect(14, 112, 212, 1, FA_RULE);
    for (l = 0; l < NLANE; l++) {                         /* the beat */
        int32_t y = 117 + (int32_t)l * 11, on = (int32_t)((heard >> l) & 1u), sel = l == fa_dr_sel;
        if (sel)
            cv_rect(10, y - 1, 220, 11, FA_RGB(0x2A, 0x25, 0x20));
        if (sel)
            cv_rect(10, y - 1, 2, 11, FA_CR);
        fa_ltext(14, y + 1, &AF_X, FA_DR_SHORT[l], sel ? FA_CR : on ? FA_MU : FA_RGB(0x4A, 0x44, 0x3D));
        if ((fa_dr_pend >> l) & 1u)                       /* a mute waiting for its quarter */
            cv_rect(30, y + 3, 2, 2, FA_RED);
        for (i = 0; i < 16u; i++) {
            int32_t x = 35 + (int32_t)i * 11 + (int32_t)(i >> 2) * 4;
            if (fa_dr_hit(l, i))
                cv_rect(x, y, 9, 8, on ? FA_OR : FA_RGB(0x5A, 0x3A, 0x22));
            else
                cv_rect(x + 3, y + 3, 3, 2, FA_LINE);
        }
        for (q = 0; q < 4u; q++)                          /* a quarter of its own: a line under it */
            if (fa_dr_qn[l][q] >= 0)
                cv_rect(35 + (int32_t)q * 48, y + 9, 42, 1, sel && (int32_t)q == hq ? FA_OR : FA_CR);
        if (sel && hq >= 0 && fa_dr_qn[l][hq] < 0)        /* the quarter held, "=": its ends marked */
            for (i = 0; i < 2u; i++)
                cv_rect(35 + hq * 48 + (int32_t)i * 41, y + 9, 1, 1, FA_OR);
    }
    {   /* the foot: the lane, then what the wheels set now */
        uint32_t s = fa_dr_sel;
        str_cpy(b, FA_DR_NAME[s], 8);
        fa_ltext(14, 210, &AF_S, b, FA_CR);
        if (hl >= 0) {
            str_cpy(b, "CHANCE ", 8);
            fmt_int(b + 7, fa_dr_chance[hl]);
            str_cpy(b + str_len(b), "%", 2);
        } else if (hq >= 0) {
            b[0] = 'Q';
            b[1] = (char)('1' + hq);
            b[2] = 0;
            if (fa_dr_qn[s][hq] < 0) {
                str_cpy(b + 2, "  =", 4);
            } else {
                str_cpy(b + 2, "  HITS ", 8);
                fmt_int(b + str_len(b), fa_dr_qn[s][hq]);
                str_cpy(b + str_len(b), "  ROT ", 7);
                fmt_int(b + str_len(b), fa_dr_qr[s][hq]);
            }
        } else {
            str_cpy(b, "HITS ", 6);
            fmt_int(b + 5, fa_dr_n[s]);
            str_cpy(b + str_len(b), "  ROT ", 7);
            fmt_int(b + str_len(b), fa_dr_r[s]);
            str_cpy(b + str_len(b), "  VOL ", 7);
            fmt_int(b + str_len(b), (int32_t)fa_dr_lv[s] * 100 / 127);
        }
        fa_rtext(226, 210, &AF_S, b, FA_OR);
    }
}

/* the LEDs in the drum mode: the white keys the selected lane's hits (dim) and the step playing (lit), the black keys
 * 1..8 the lanes heard (lit), a mute waiting for its quarter blinking */
static void fa_dr_leds(uint8_t *nl, uint8_t *nm)
{
    const track_t *t = &trk[FA_DR_TRK];
    uint32_t i, blink = (fm1_ms >> 7) & 1u;
    for (i = 0; i < 16u; i++) {
        uint32_t k = fa_white_key(i);
        led_put(nl, 14u + k, song.playing && t->seq_idx % 16u == i);
        led_put(nm, 14u + k, fa_dr_hit(fa_dr_sel, i));
    }
    for (i = 0; i < NLANE; i++) {
        uint32_t k = fa_black_key(i), on = !((fa_dr_mute >> i) & 1u);
        led_put(nl, 14u + k, (int)((fa_dr_pend >> i) & 1u ? blink : on));
        led_put(nm, 14u + k, (int)(i == fa_dr_sel));
    }
}

/* the screen's signature of the drums (not the step playing: the screen does not show it) */
static uint32_t fa_dr_sig(uint32_t h)
{
    uint32_t l, q;
    h = (h ^ fa_dr ^ (uint32_t)fa_dr_sel << 1 ^ (uint32_t)fa_dr_kit << 4 ^ (uint32_t)fa_dr_mute << 8 ^ (uint32_t)fa_dr_pend << 16 ^
         (uint32_t)(fa_dr_held_q() + 1) << 24 ^ (uint32_t)(fa_dr_held_lane() + 1) << 27) * 16777619u;
    for (l = 0; l < NLANE; l++) {
        h = (h ^ fa_dr_n[l] ^ (uint32_t)fa_dr_r[l] << 5 ^ (uint32_t)fa_dr_lv[l] << 9 ^ (uint32_t)fa_dr_chance[l] << 16) * 16777619u;
        for (q = 0; q < 4u; q++)
            h = (h ^ (uint32_t)(fa_dr_qn[l][q] + 1) ^ (uint32_t)fa_dr_qr[l][q] << 3) * 16777619u;
    }
    return h;
}
