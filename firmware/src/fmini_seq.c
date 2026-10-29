/* SPDX-License-Identifier: GPL-3.0-only
 * Acid Moogi (fmini_app.c includes this file; FELUCCA_FMINI_ONLY): the sequencer on the keys, the tempo on PLAY +
 * SELECT, and the acid line, a port of the author's acid mod for norns (the project notes keep it as acid-mod.lua).
 *
 * THE SEQUENCE is track 1's: four parts of 16 steps (64), the parts that play in order (seq.c fa_parts, seq_nx), HALF
 * plays the last of them for 8 steps: 8 .. 64 steps in eights. The 16 white keys are the 16 steps of the part shown:
 * the step playing lights, a step holding a note glows.
 *  - REC while playing: the notes played go into the step playing (seq.c rec_note: a held note ties on, a new note
 *    while the last one is held slides into it, as on the Volca Bass). REC while stopped: step record, the cursor on the
 *    first step (its key blinks); a note goes into the cursor's step (it replaces what was there) and the cursor goes on;
 *    ARP there is a rest; a note pressed while the last one is still held slides into it. REC again: done.
 *  - SEQ held (the keys play nothing then, seq.c keyboard_block: kb_mask): a white key moves the cursor to its step;
 *    black keys 1..4 are the parts: a press shows that part on the white keys, a double press turns it on / off (one
 *    stays on); black key 5 is HALF. SEQ + REC clears the sequence (notes and knobs). The screen shows the part while
 *    SEQ is held.
 *  - The knobs are recorded with the notes (Felucca's automation, motion.c) only with GLO > KNOB REC ON (off by default).
 *    REC held + ARP takes the knobs' motion back to how it was when REC was last armed. REC arms when it is let go.
 * THE TEMPO: PLAY held, SELECT turns it (the screen shows it, PLAY blinks in time); a tap of PLAY starts and stops.
 * THE DRUMS (moogi m6): SEQ twice is the drum mode, fmini_drum.c (its header is their manual).
 * THE NORNS' CLOCK (moogi m5): clock pulses on USB set the tempo by themselves (the tempo screen says USB) and half a second
 *   without them gives the tempo back. PLAY stays the FM-1's: from stop it waits for the norns' next bar (PLAY blinks fast),
 *   the norns' START only marks the bar's first beat, its STOP stops nothing (seq.c ck_realtime).
 * THE ACID LINE: EDIT twice turns it on (EDIT lights) and off; a tap of EDIT: a new line from the next loop. On, it
 * writes the line into the sequence and keeps it there. EDIT held: SELECT, ALGORITHM, PRESETS are DENSITY, MOVEMENT,
 * TENSION, KNOB 1..4 ACCENTS, SLIDES, NOTES (in the pool), RANGE (the octave), a key the root (moogi m5: the wheels too only
 * with EDIT held; without it they are the page's and PRESETS changes the preset, as with the acid line off).
 * GLO > ACID: the scale, note length, step, high range, the velocities, breathe, the four LFOs, the history and the
 * favorites. The line is built as acid-mod.lua builds it (the same seed gives the same 16 steps). */

static uint8_t *fa_cfg_byte(uint32_t i);
static void fa_cfg_save(void);
static uint32_t fa_ac_rnd(void);

/* ----------------------------------------------------------- SEQ --- */
static uint8_t fa_sq;                                     /* SEQ is held: the keys are the sequence's, its screen shows */
static uint8_t fa_sq_rec;                                 /* REC armed */
static uint8_t fa_sq_cur;                                 /* step record's cursor, 0..63 */
static uint8_t fa_sq_view;                                /* the part the white keys show, 0..3 */
static uint8_t fa_sq_dirty;                               /* the parts changed with SEQ held: saved when it is let go */
static volatile uint8_t fa_sr_n, fa_sr_note[4];           /* step record: the recorded keys still down */
static volatile uint8_t fa_sr_prev = 0xFFu;               /* .. and the step the last one went into */
static uint8_t fa_bk_last = 0xFFu;                        /* SEQ + a black key: the last part pressed, and when (a double press) */
static uint32_t fa_bk_t;
static uint8_t fa_acid;                                   /* the acid line is on */
static motion_store_t fa_mo_snap;                         /* the knobs' motion as it was when REC was last armed (REC + ARP) */
static uint8_t fa_mo_ok;
static uint8_t fa_rec_down, fa_rec_used;                  /* REC held; a combination used it (no arm on its release) */
static void fa_ac_changed(void);

static uint32_t fa_seq_last_part(void)
{
    uint32_t m = fa_parts & 15u;
    return (m & 8u) ? 3u : (m & 4u) ? 2u : (m & 2u) ? 1u : 0u;
}
static uint32_t fa_seq_len(void) { return 16u * (fa_seq_last_part() + 1u); }     /* P_SLEN */
static uint32_t fa_seq_count(void)                        /* the steps that play: 8 .. 64 */
{
    uint32_t len = fa_seq_len(), i, n = 0;
    for (i = 0; i < len; i++)
        n += (uint32_t)seq_on_step(i, len);
    return n ? n : 1u;
}
static uint32_t fa_seq_first(void)
{
    uint32_t len = fa_seq_len(), i;
    for (i = 0; i < len; i++)
        if (seq_on_step(i, len))
            return i;
    return 0;
}
static uint32_t fa_seq_pos(uint32_t idx)                  /* step idx as the n-th that plays (0..) */
{
    uint32_t len = fa_seq_len(), i, n = 0;
    for (i = 0; i < idx && i < len; i++)
        n += (uint32_t)seq_on_step(i, len);
    return n;
}
static uint32_t fa_seq_at(uint32_t pos)                   /* the n-th step that plays */
{
    uint32_t len = fa_seq_len(), i;
    for (i = 0; i < len; i++)
        if (seq_on_step(i, len) && !pos--)
            return i;
    return fa_seq_first();
}
static int fa_seq_stepping(void) { return fa_sq_rec && !song.playing && !fa_acid; }   /* step record */

/* step record: the cursor goes on: in the steps that play to the next that plays, in a part that does not play to the
 * part's next step */
static void fa_seq_advance(void)
{
    uint32_t len = fa_seq_len(), c = fa_sq_cur;
    if (c < len && seq_on_step(c, len))
        fa_sq_cur = (uint8_t)seq_nx(&trk[0], c, len);
    else
        fa_sq_cur = (uint8_t)((c & ~15u) | ((c + 1u) & 15u));
}

/* seq.c input_on (the audio ISR, every note played: keys, MIDI): step record */
static void fa_seq_note(track_t *t, uint32_t note)
{
    step_t *s;
    uint32_t k;
    if (!fa_seq_stepping() || t != &trk[0] || note > 127u)
        return;
    if (fa_sr_n && fa_sr_prev < NSTEP)                    /* the last note still held: it slides into this one */
        t->step[fa_sr_prev].flags |= SF_SLIDE;
    s = &t->step[fa_sq_cur & (NSTEP - 1u)];
    memset(s, 0, sizeof *s);
    s->note[0] = (uint8_t)note;
    s->n = 1;
    s->time = ST_NOTE;
    s->vel = 100;
    fa_sr_prev = (uint8_t)(fa_sq_cur & (NSTEP - 1u));
    for (k = 0; k < fa_sr_n && fa_sr_note[k] != note; k++)
        ;
    if (k == fa_sr_n && k < 4u)
        fa_sr_note[fa_sr_n++] = (uint8_t)note;
    fa_seq_advance();
}
/* seq.c input_off */
static void fa_seq_off(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    if (t != &trk[0])
        return;
    for (i = 0; i < fa_sr_n; i++)
        if (fa_sr_note[i] != note)
            fa_sr_note[k++] = fa_sr_note[i];
    fa_sr_n = (uint8_t)k;
}
/* step record, ARP: a rest at the cursor */
static void fa_seq_rest(void)
{
    step_t *s = &trk[0].step[fa_sq_cur & (NSTEP - 1u)];
    step_t r;
    memset(&r, 0, sizeof r);
    r.time = ST_REST;
    *s = r;
    fa_sr_prev = 0xFFu;
    fa_seq_advance();
    fa_sq_view = (uint8_t)(fa_sq_cur >> 4);
}
static void fa_seq_clear(void)                            /* SEQ + REC: the notes and the knobs' motion */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        memset(&trk[0].step[i], 0, sizeof trk[0].step[i]);
    motion_clear(&trk[0]);
    fa_mo_snap = motion;
    fa_mo_ok = 1;
    fa_sq_cur = (uint8_t)fa_seq_first();
    fa_sr_prev = 0xFFu;
    fa_ac_changed();                                      /* (the acid line on: it writes itself back) */
}
static void fa_seq_arm(void)                              /* REC: on (the cursor on the first step) / off */
{
    fa_sq_rec = (uint8_t)!fa_sq_rec;
    if (fa_sq_rec) {
        fa_mo_snap = motion;                              /* (REC + ARP takes the knobs back to this) */
        fa_mo_ok = 1;
        fa_sq_cur = (uint8_t)fa_seq_first();
        fa_sq_view = (uint8_t)(fa_sq_cur >> 4);
        fa_sr_n = 0;
        fa_sr_prev = 0xFFu;
    }
}

/* REC + ARP: the knobs' motion back to how it was when REC was last armed (none armed yet: all of it goes) */
static void fa_motion_undo(void)
{
    track_t *t = &trk[0];
    uint32_t f;
    if (!fa_mo_ok) {
        motion_clear(t);
    } else {
        motion_restore(t);
        f = motion_guard();
        motion = fa_mo_snap;
        RING_PUBLISH();
        motion_unguard(f);
        motion_rebase(t);
        motion_full = 0;
    }
    str_cpy(fa_msg, "KNOBS UNDONE", 16);
    fa_msg_until = fm1_ms + 1500u;
}
/* REC: armed (or not) when it is let go; held, ARP undoes the knobs' motion; SEQ + REC clears. Returns the presses
 * left for the other buttons (an ARP REC used is not the arpeggiator's, nor step record's rest) */
static uint32_t fa_rec_btn(uint32_t pressed)
{
    uint32_t down = (fm1_in.buttons >> panel.btn[B_REC]) & 1u;
    if ((pressed >> panel.btn[B_REC]) & 1u) {
        if (fa_sq) {
            fa_seq_clear();
            fa_sq_dirty = 1;
            str_cpy(fa_msg, "SEQ CLEARED", 16);
            fa_msg_until = fm1_ms + 1500u;
        } else {
            fa_rec_down = 1;
            fa_rec_used = 0;
        }
    }
    if (fa_rec_down && ((pressed >> panel.btn[B_ARP]) & 1u)) {
        fa_motion_undo();
        fa_rec_used = 1;
        pressed &= ~(1u << panel.btn[B_ARP]);
    }
    if (fa_rec_down && !down) {
        fa_rec_down = 0;
        if (!fa_rec_used && !fa_acid && !fa_dr)           /* (moogi m6: REC does nothing in the drum mode) */
            fa_seq_arm();
    }
    return pressed;
}

/* key k pressed with SEQ held */
static void fa_seq_key(uint32_t k)
{
    uint32_t p = key_place(k);
    if (!key_black(k)) {
        if (p < 16u) {
            fa_sq_cur = (uint8_t)(fa_sq_view * 16u + p);
            fa_sr_prev = 0xFFu;
        }
        return;
    }
    if (p < 4u) {
        if (fa_bk_last == p && (uint32_t)(fm1_ms - fa_bk_t) < 400u) {   /* a double press: the part on / off */
            uint32_t m = (fa_parts & 15u) ^ (1u << p);
            if (m)
                fa_parts = (uint8_t)((fa_parts & 16u) | m);
            fa_bk_last = 0xFFu;
        } else {
            fa_bk_last = (uint8_t)p;
            fa_bk_t = fm1_ms;
        }
        fa_sq_view = (uint8_t)p;
        if ((fa_sq_cur >> 4) != p)
            fa_sq_cur = (uint8_t)(p * 16u);
    } else if (p == 4u) {
        fa_parts ^= 16u;
    } else {
        return;
    }
    fa_sq_dirty = 1;
    fa_ac_changed();
}

/* each frame: the length, the record arm, the keys' layer */
static void fa_seq_poll(void)
{
    track_t *t = &trk[0];
    t->p[P_SLEN] = (int16_t)fa_seq_len();
    song.rec = (uint8_t)(fa_sq_rec && song.playing && !fa_acid ? 1u : 0u);   /* (only while playing: no count-in) */
    if (song.playing && !fa_sq && !fa_seq_stepping())
        fa_sq_view = (uint8_t)((t->seq_idx % fa_seq_len()) >> 4);   /* the white keys follow the part playing */
    kb_mask = (1u << panel.btn[B_SEQ]) | (fa_acid ? 1u << panel.btn[B_EDIT] : 0u);
}

static void fa_note_name(char *b, uint32_t note)
{
    static const char NM[12][3] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    uint32_t n = note % 12u;
    uint32_t k = 0;
    b[k++] = NM[n][0];
    if (NM[n][1])
        b[k++] = NM[n][1];
    b[k++] = (char)('0' + (int32_t)(note / 12u) - 1);     /* C4 = 60 */
    b[k] = 0;
}

/* SEQ held: the part shown (its 16 steps, the playing one orange, the cursor red), the parts, HALF, the length */
static void fa_seq_dialog(void)
{
    const track_t *t = &trk[0];
    uint32_t i, len = fa_seq_len(), now = song.playing ? t->seq_idx % len : 99u, v = fa_sq_view;
    char b[24];
    fa_frame("SEQUENCE");
    for (i = 0; i < 16u; i++) {
        uint32_t st = v * 16u + i;
        int32_t x = 15 + (int32_t)(i % 4u) * 53, y = 50 + (int32_t)(i / 4u) * 28;
        int on = seq_on_step(st, len), here = fa_sq_rec && st == fa_sq_cur, playing = st == now;
        const step_t *s = &t->step[st];
        uint16_t bg = playing ? FA_OR : here ? FA_RED : on ? FA_RGB(0x2A, 0x25, 0x20) : FA_RGB(0x16, 0x14, 0x12);
        uint16_t fg = playing || here ? FA_RGB(0x14, 0x12, 0x10) : on ? FA_CR : FA_RGB(0x6A, 0x62, 0x58);
        cv_rect(x, y, 50, 24, bg);
        if (s->time == ST_NOTE && s->n) {
            fa_note_name(b, s->note[0]);
            if (s->flags & SF_SLIDE)
                str_cpy(b + str_len(b), "~", 2);
            fa_ctext(x + 25, y + 4, &AF_M, b, fg);
        } else {
            fa_ctext(x + 25, y + 4, &AF_M, s->time == ST_TIE ? "=" : "-", fg);
        }
    }
    for (i = 0; i < 5u; i++) {                            /* the parts 1..4 and HALF */
        int32_t x = 15 + (int32_t)i * 38;
        int on = i < 4u ? (int)((fa_parts >> i) & 1u) : (int)((fa_parts >> 4) & 1u);
        uint16_t bg = on ? FA_OR : FA_RGB(0x2A, 0x25, 0x20);
        if (i == v)
            cv_rect(x - 2, 162, 38, 24, FA_CR);
        cv_rect(x, 164, 34, 20, bg);
        if (i < 4u) {
            b[0] = (char)('1' + i);
            b[1] = 0;
        } else {
            str_cpy(b, "1/2", 4);
        }
        fa_ctext(x + 17, 167, &AF_M, b, on ? FA_RGB(0x14, 0x12, 0x10) : FA_CR);
    }
    fmt_int(b, (int32_t)fa_seq_count());
    fa_rtext(222, 167, &AF_M, b, FA_CR);
    fa_ctext(120, 194, &AF_S, fa_sq_rec ? (song.playing ? "REC LIVE" : "STEP REC") : "WHITE STEP  BLACK PART  REC CLEAR", fa_sq_rec ? FA_RED : FA_MU);
}

/* the key LEDs of the sequence: SEQ held, the parts on the black keys; playing or recording, the steps on the white
 * keys. 0: nothing to show (the keys light as played) */
static uint32_t fa_white_key(uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (!key_black(k) && key_place(k) == p)
            return k;
    return 0;
}
static uint32_t fa_black_key(uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) && key_place(k) == p)
            return k;
    return 0;
}
static int fa_seq_leds(uint8_t *nl, uint8_t *nm)
{
    const track_t *t = &trk[0];
    uint32_t i, len = fa_seq_len(), v = fa_sq_view, blink = (fm1_ms >> 7) & 1u;
    if (!fa_sq && !song.playing && !fa_sq_rec)
        return 0;
    for (i = 0; i < 16u; i++) {
        uint32_t st = v * 16u + i, k = fa_white_key(i);
        const step_t *s = &t->step[st];
        int lit = song.playing && t->seq_idx % len == st;
        if (fa_sq_rec && st == fa_sq_cur && (fa_seq_stepping() || fa_sq))
            lit = (int)blink;
        led_put(nl, 14u + k, lit);
        led_put(nm, 14u + k, s->time == ST_NOTE && s->n && seq_on_step(st, len));
    }
    if (fa_sq) {
        for (i = 0; i < 4u; i++) {
            uint32_t on = (fa_parts >> i) & 1u;
            led_put(nl, 14u + fa_black_key(i), (int)(i == v ? blink : on));
            led_put(nm, 14u + fa_black_key(i), (int)on);
        }
        led_put(nl, 14u + fa_black_key(4), (int)((fa_parts >> 4) & 1u));
    }
    return 1;
}

/* --------------------------------------------------------- TEMPO --- */
static uint8_t fa_pl_down, fa_pl_turned;
static uint32_t fa_pl_t0;
static void fa_play_btn(uint32_t pressed)                 /* PLAY: a tap starts / stops; held, SELECT is the tempo */
{
    uint32_t down = (fm1_in.buttons >> panel.btn[B_PLAY]) & 1u;
    if ((pressed >> panel.btn[B_PLAY]) & 1u) {
        fa_pl_down = 1;
        fa_pl_turned = 0;
        fa_pl_t0 = fm1_ms;
    }
    if (fa_pl_down && !down) {
        fa_pl_down = 0;
        if (!fa_pl_turned)
            transport_req = (song.playing || transport_req == 1u || ck_wait) ? 2u : 1u;   /* (moogi m5: waiting for the
                                                                                              * norns' bar, PLAY again: no) */
    }
}
static void fa_tempo_turn(int32_t s)
{
    if (!song.g[G_CLOCK])                                 /* moogi m5: on the norns' clock the tempo is the norns' */
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + fmg_acc_env(EN_SELECT, s), GP[G_BPM].min, GP[G_BPM].max);
    fa_pl_turned = 1;
}
static int fa_pl_show(void) { return fa_pl_down && (fa_pl_turned || (uint32_t)(fm1_ms - fa_pl_t0) >= 250u); }
static int fa_pl_led(void)                                /* PLAY held: lit a quarter of each beat */
{
    uint32_t bpm = (uint32_t)clamp(song.g[G_BPM], 20, 400);
    return (int)(((uint32_t)(fm1_ms - fa_pl_t0) * bpm) % 60000u < 15000u);
}
static void fa_tempo_dialog(void)
{
    char b[8];
    cv_rect(40, 70, 160, 92, FA_OR);
    cv_rect(42, 72, 156, 88, FA_RGB(0x1B, 0x19, 0x17));
    fa_ctext(120, 80, &AF_S, "TEMPO", FA_CR);
    if (song.g[G_CLOCK]) {                                /* moogi m5: the norns' clock */
        fa_ctext(120, 100, &AF_L, "USB", FA_OR);
        fa_ctext(120, 142, &AF_X, "CLOCK FROM USB", FA_MU);
        return;
    }
    fmt_int(b, song.g[G_BPM]);
    fa_ctext(120, 100, &AF_L, b, FA_OR);
    fa_ctext(120, 142, &AF_X, "PLAY + SELECT", FA_MU);
}

/* ---------------------------------------------------------- ACID --- */
enum { AV_DENS, AV_MOVE, AV_TENS, AV_ACC, AV_SLD, AV_NOTES, AV_OCT, AV_ROOT, AV_SCALE, AV_GATE, AV_DIV, AV_HIGH, AV_VEL,
       AV_AVEL, AV_BRATE, AV_BDEP, AV_BRET, AV_LFO, AV_N = AV_LFO + 16 };
#define AC_L(n, f) (AV_LFO + (n) * 4u + (f))              /* LFO n's TARGET 0, SHAPE 1, CYCLE 2, DEPTH 3 */
static const uint8_t AC_MIN[AV_N] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 5, 0, 1, 1, 1, 0, 1, 0};
static const uint8_t AC_MAX[AV_N] = {100, 100, 100, 100, 100, 5, 7, 11, 7, 95, 4, 2, 127, 127, 4, 3, 100,
                                     6, 2, 6, 50, 6, 2, 6, 50, 6, 2, 6, 50, 6, 2, 6, 50};
static const uint8_t AC_DEF[AV_N] = {60, 45, 25, 40, 30, 3, 3, 0, 0, 55, 0, 1, 70, 127, 0, 1, 60,
                                     0, 0, 3, 20, 0, 0, 3, 20, 0, 0, 3, 20, 0, 0, 3, 20};
static uint8_t ac_v[AV_N];

static const uint8_t AC_SC[8][8] = {                     /* the scales: their count, then the intervals */
    {7, 0, 1, 3, 5, 7, 8, 10}, {7, 0, 1, 4, 5, 7, 8, 10}, {7, 0, 2, 3, 5, 7, 8, 10}, {7, 0, 2, 3, 5, 7, 8, 11},
    {5, 0, 3, 5, 7, 10, 0, 0}, {7, 0, 2, 3, 5, 7, 9, 10}, {7, 0, 1, 3, 5, 6, 8, 10}, {5, 0, 1, 3, 7, 10, 0, 0}};
static const char *const AC_SCN[8] = {"PHRYG", "PH DOM", "MINOR", "HARM", "PENTA", "DORIAN", "LOCRI", "ACID"};
static const char *const AC_DIVN[5] = {"1/16", "1/8", "1/32", "1/12", "1/24"};
static const uint8_t AC_DIVQ[5] = {4, 2, 8, 3, 6};       /* steps a beat */
static const uint8_t AC_SDIV[5] = {2, 1, 3, 4, 5};       /* the track's DIV (params.c N_DIV) */
static const char *const AC_BRN[5] = {"OFF", "2", "4", "8", "16"};
static const uint8_t AC_BREV[5] = {0, 2, 4, 8, 16};
static const char *const AC_TGN[7] = {"OFF", "DENSITY", "MOVEMENT", "TENSION", "ACCENTS", "SLIDES", "NOTE LEN"};
static const uint8_t AC_TGV[7] = {0, AV_DENS, AV_MOVE, AV_TENS, AV_ACC, AV_SLD, AV_GATE};
static const char *const AC_SHN[3] = {"SINE", "TRIANGLE", "RANDOM"};
static const char *const AC_NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

#define AC_NS 64u
#define AC_NF 7u                                          /* the field's on voice note alt tens acc sld */
enum { AQ_ON, AQ_VOICE, AQ_NOTE, AQ_ALT, AQ_TENS, AQ_ACC, AQ_SLD };
typedef struct { uint16_t on : 1, hi : 1, iv : 4, oct : 2, acc : 1, sld : 1; } ac_step_t;   /* (2 bytes: RAM is short) */
static uint16_t ac_f[AC_NS][AC_NF];                      /* the line's dice (0..65535 = 0..1); as born: ac_born */
static uint16_t ac_ma, ac_mb, ac_mn;
static uint32_t ac_seed, ac_rs;
static uint8_t ac_pool[6], ac_np;
static int8_t ac_tn = -1;                                 /* the tension note (outside the pool), -1 none */
static ac_step_t ac_p[AC_NS];
static uint8_t ac_built[6];                               /* the values the line was last built from (+ its length) */
static int16_t ac_off[7];                                 /* the LFOs' offsets by target */
static uint32_t ac_ph[4];                                 /* the LFOs' phases, 1 << 24 = a cycle */
static int16_t ac_hold[4];                                /* RANDOM's value */
static uint32_t ac_pend;                                  /* a seed to load at the loop's end, 0 none */
static uint8_t ac_pend_keep;                              /* .. not added to the history (one from it) */
static uint32_t ac_hist[10];
static uint8_t ac_nh, ac_hi, ac_fi, ac_cyc, ac_row, ac_hot = 0xFFu, ac_dirty;
static int32_t ac_prev = -1;
static uint32_t ac_until;                                 /* the ACID screen shows until then (a value turned) */
static uint8_t fa_ed_down, fa_ed_used, fa_ed_dbl, fa_ed_pend;
static uint32_t fa_ed_t0, fa_ed_rel;

static uint32_t fa_ac_rnd(void)                           /* breathe's and the seeds' dice (xorshift) */
{
    static uint32_t x = 0x9E3779B9u;
    x ^= fm1_ms * 2654435761u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}
static void ac_seed_rng(uint32_t s)                       /* acid-mod.lua rng_seed / rng: Park-Miller */
{
    s %= 2147483647u;
    ac_rs = s ? s : 2147483646u;
}
static uint32_t ac_r16(void)                              /* 0..65535 for 0..1 (no 64-bit divide: the device has none) */
{
    int32_t x = 16807 * (int32_t)(ac_rs % 127773u) - 2836 * (int32_t)(ac_rs / 127773u);   /* Schrage: s x 16807 mod 2^31-1 */
    ac_rs = (uint32_t)(x > 0 ? x : x + 2147483647);
    return ac_rs >> 15;                                   /* = floor(s x 65536 / (2^31 - 1)) for every s */
}
static uint32_t ac_pick(uint32_t n, uint32_t r) { uint32_t i = (r * n) >> 16; return i < n ? i : n - 1u; }
/* f < pct / 100 x m / 100 (f a die 0..65535) */
static int ac_lt(uint32_t f, uint32_t pct, uint32_t m) { return f * 10000u < pct * 65536u * m; }

static void ac_draw(uint32_t s)                           /* draw_field: 16 steps as the norns mod, then 48 more */
{
    uint32_t i, k;
    ac_seed = s;
    ac_seed_rng(s);
    for (i = 0; i < 16u; i++)
        for (k = 0; k < AC_NF; k++)
            ac_f[i][k] = (uint16_t)ac_r16();
    ac_ma = (uint16_t)ac_r16();
    ac_mb = (uint16_t)ac_r16();
    ac_mn = (uint16_t)ac_r16();
    for (i = 16; i < AC_NS; i++)
        for (k = 0; k < AC_NF; k++)
            ac_f[i][k] = (uint16_t)ac_r16();
    ac_cyc = 0;
}
static uint16_t ac_born(uint32_t i, uint32_t k)           /* die k of step i as ac_draw drew it (the seed's sequence again) */
{
    uint32_t n = i < 16u ? i * AC_NF + k : 16u * AC_NF + 3u + (i - 16u) * AC_NF + k, r;
    ac_seed_rng(ac_seed);
    do
        r = ac_r16();
    while (n--);
    return (uint16_t)r;
}
static void ac_select_pool(void)
{
    const uint8_t *sc = AC_SC[ac_v[AV_SCALE] & 7u];
    uint8_t bag[24], tense[4], nb = 0, nt = 0;
    uint32_t i, guard = 0, want = ac_v[AV_NOTES];
    ac_seed_rng(ac_seed * 31u + 7u);
    for (i = 1; i <= sc[0]; i++) {
        uint32_t v = sc[i], w;
        if (!v)
            continue;
        if (v == 1u || v == 6u || v == 8u || v == 11u) {
            tense[nt++] = (uint8_t)v;
            continue;
        }
        for (w = (v == 7u || v == 10u || v == 3u) ? 3u : 1u; w; w--)
            bag[nb++] = (uint8_t)v;
    }
    ac_pool[0] = 0;
    ac_np = 1;
    while (ac_np < want && nb && guard < 80u) {
        uint32_t p, k;
        guard++;
        p = bag[ac_pick(nb, ac_r16())];
        for (k = 0; k < ac_np && ac_pool[k] != p; k++)
            ;
        if (k == ac_np)
            ac_pool[ac_np++] = (uint8_t)p;
    }
    ac_tn = nt ? (int8_t)tense[ac_pick(nt, ac_r16())] : -1;
}

/* the value the line is built from: the knob plus its LFO's offset */
static uint32_t ac_eff(uint32_t av)
{
    int32_t v = ac_v[av];
    uint32_t t;
    for (t = 1; t < 7u; t++)
        if (AC_TGV[t] == av && ac_off[t]) {
            v += ac_off[t];
            return (uint32_t)clamp(v, av == AV_GATE ? 5 : 0, av == AV_GATE ? 95 : 100);
        }
    return (uint32_t)v;
}

static ac_step_t ac_build_step(uint32_t i)                /* i: 1-based, as the mod */
{
    ac_step_t st = {0, 0, 0, 0, 0, 0};
    const uint16_t *f = ac_f[(i - 1u) % AC_NS];
    uint32_t dens = ac_eff(AV_DENS), move = ac_eff(AV_MOVE), tens = ac_eff(AV_TENS), strong = i % 4u == 1u;
    if (i == 1u) {
        st.on = 1;
        return st;
    }
    st.on = (uint8_t)ac_lt(f[AQ_ON], dens, strong ? 70u : 100u);
    if (!st.on)
        return st;
    st.hi = (uint8_t)ac_lt(f[AQ_VOICE], move, strong ? 50u : 120u);
    if (!st.hi) {
        st.iv = f[AQ_ALT] < 52429u ? 0u : ac_pool[ac_pick(ac_np, f[AQ_NOTE])];
    } else {
        if (ac_tn >= 0 && ac_lt(f[AQ_TENS], tens, 60u))
            st.iv = (uint8_t)ac_tn;
        else
            st.iv = ac_pool[ac_pick(ac_np, f[AQ_NOTE])];
        st.oct = 1;
        if (ac_v[AV_HIGH] == 2u && f[AQ_ALT] > 47186u)
            st.oct = 2;
    }
    return st;
}
static void ac_build(void)                                /* build_pattern over the steps that play */
{
    uint32_t len = fa_seq_count(), i, j, n, cap, used, acc = ac_eff(AV_ACC), sld = ac_eff(AV_SLD);
    ac_built[0] = (uint8_t)ac_eff(AV_DENS);
    ac_built[1] = (uint8_t)ac_eff(AV_MOVE);
    ac_built[2] = (uint8_t)ac_eff(AV_TENS);
    ac_built[3] = (uint8_t)acc;
    ac_built[4] = (uint8_t)sld;
    ac_built[5] = (uint8_t)len;
    if (len > AC_NS)
        len = AC_NS;
    for (i = 1; i <= len; i++)
        ac_p[i - 1u] = ac_build_step(i);
    if (len >= 8u) {
        uint32_t half = len / 2u, qs, span, m[2];
        for (i = half + 1u; i <= half * 2u; i++) {
            ac_p[i - 1u] = ac_p[i - 1u - half];
            ac_p[i - 1u].acc = ac_p[i - 1u].sld = 0;
        }
        qs = len - len / 4u + 1u;
        qs = qs > half + 1u ? qs : half + 1u;
        span = len - qs;
        n = ac_mn < 32768u ? 1u : 2u;
        m[0] = qs + ((ac_ma * (span + 1u)) >> 16);
        m[1] = qs + ((ac_mb * (span + 1u)) >> 16);
        for (j = 0; j < n; j++) {
            i = m[j] < len ? m[j] : len;
            if (i >= 2u)
                ac_p[i - 1u] = ac_build_step(i);
        }
    }
    if (ac_tn >= 0)                                       /* the tension note resolves to the root */
        for (i = 0; i < len; i++)
            if (ac_p[i].on && ac_p[i].iv == (uint8_t)ac_tn) {
                for (j = i + 1u; j < len && !ac_p[j].on; j++)
                    ;
                if (j < len) {
                    ac_p[j].iv = 0;
                    ac_p[j].oct = 0;
                    ac_p[j].hi = 0;
                }
            }
    for (i = n = 0; i < len; i++)
        n += ac_p[i].on;
    cap = n * 4u / 10u;
    cap = cap ? cap : 1u;
    for (i = used = 0; i < len; i++) {                    /* accents: at most 40 % of the notes */
        ac_step_t *st = &ac_p[i];
        uint32_t m = 100u;
        if (!st->on || used >= cap)
            continue;
        if (st->hi)
            m = m * 15u / 10u;
        if ((i + 1u) % 2u == 0u)
            m = m * 13u / 10u;
        if (ac_lt(ac_f[i][AQ_ACC], acc, m)) {
            st->acc = 1;
            used++;
        }
    }
    for (i = 0; i < len; i++) {                           /* slides: likelier on a big jump */
        ac_step_t *st = &ac_p[i], *nx = &ac_p[(i + 1u) % len];
        if (st->on && nx->on) {
            int32_t jump = (int32_t)(nx->iv + nx->oct * 12u) - (int32_t)(st->iv + st->oct * 12u);
            jump = jump < 0 ? -jump : jump;
            st->sld = (uint8_t)ac_lt(ac_f[i][AQ_SLD], sld, jump >= 7 ? 160u : 50u);
        }
    }
    ac_dirty = 1;
}
/* the line into the sequence's steps that play (unchanged steps are not written) */
static void ac_write(void)
{
    track_t *t = &trk[0];
    uint32_t len = fa_seq_count(), i;
    ac_dirty = 0;
    for (i = 0; i < len && i < AC_NS; i++) {
        const ac_step_t *a = &ac_p[i];
        uint32_t at = fa_seq_at(i);
        step_t s;
        memset(&s, 0, sizeof s);
        if (a->on) {
            s.note[0] = (uint8_t)clamp((int32_t)(ac_v[AV_OCT] * 12u + ac_v[AV_ROOT] + a->iv + a->oct * 12u), 0, 127);
            s.n = 1;
            s.time = ST_NOTE;
            s.vel = a->acc ? ac_v[AV_AVEL] : ac_v[AV_VEL];
            s.flags = (uint8_t)((a->acc ? SF_ACCENT : 0u) | (a->sld ? SF_SLIDE : 0u));
        } else {
            s.time = ST_REST;
        }
        if (memcmp(&t->step[at], &s, sizeof s))
            t->step[at] = s;
    }
}
static void ac_hist_push(uint32_t s)
{
    uint32_t i, k = 0;
    uint32_t h[10];
    h[k++] = s;
    for (i = 0; i < ac_nh && k < 10u; i++)
        if (ac_hist[i] != s)
            h[k++] = ac_hist[i];
    memcpy(ac_hist, h, sizeof h);
    ac_nh = (uint8_t)k;
    ac_hi = 0;
}
static void ac_load(uint32_t s, int keep)                 /* new_line */
{
    ac_draw(s);
    ac_select_pool();
    ac_build();
    if (!keep)
        ac_hist_push(s);
    fa_cfg_dirty = 1;
}
static void ac_request(uint32_t s, int keep)              /* now when stopped, else at the loop's end */
{
    if (!song.playing) {
        ac_load(s, keep);
        ac_pend = 0;
    } else {
        ac_pend = s;
        ac_pend_keep = (uint8_t)keep;
    }
}
static uint32_t ac_new_seed(void) { return 1u + fa_ac_rnd() % 99999u; }

static void fa_ac_changed(void)                           /* the parts changed: the line is rebuilt to their length */
{
    if (fa_acid)
        ac_build();
}
static void ac_toggle(void)
{
    track_t *t = &trk[0];
    fa_acid = (uint8_t)!fa_acid;
    if (fa_acid) {
        fa_sq_rec = 0;
        if (!ac_seed)
            ac_load(ac_new_seed(), 0);
        else {
            ac_select_pool();
            ac_build();
        }
        ac_until = fm1_ms + 1500u;
        ac_hot = 0xFFu;
    } else {
        memset(ac_off, 0, sizeof ac_off);
        t->p[P_SGATE] = (int16_t)(ac_v[AV_GATE] * 128u / 100u);
        fa_cfg_dirty = 1;
        fa_cfg_save();
    }
}

static int32_t ac_sin(uint32_t p)                         /* p 0..65535 a cycle -> -32767..32767 (Bhaskara) */
{
    uint32_t h = p & 32767u, q = (h * (32768u - h)) >> 16, v = ((16u * q) << 15) / (5u * 16384u - 4u * q);   /* q: Q14 */
    v = v > 32767u ? 32767u : v;
    return (p & 32768u) ? -(int32_t)v : (int32_t)v;
}
static int32_t ac_wave(uint32_t n)
{
    uint32_t p = ac_ph[n] >> 8, sh = ac_v[AC_L(n, 1)];
    int32_t v;
    if (sh == 0u)
        return ac_sin(p);
    if (sh == 1u) {                                       /* from the center, rising, as the sine */
        v = p < 16384u ? (int32_t)p * 2 : p < 49152u ? 65536 - (int32_t)p * 2 : (int32_t)p * 2 - 131072;
        return clamp(v, -32767, 32767);
    }
    return ac_hold[n];
}
static void ac_offsets(void)                              /* the lower LFO wins a target */
{
    uint32_t n, taken = 0;
    memset(ac_off, 0, sizeof ac_off);
    for (n = 0; n < 4u; n++) {
        uint32_t tg = ac_v[AC_L(n, 0)];
        int32_t o;
        if (!tg || tg > 6u || ((taken >> tg) & 1u))
            continue;
        taken |= 1u << tg;
        o = (int32_t)ac_v[AC_L(n, 3)] * ac_wave(n);
        ac_off[tg] = (int16_t)((o + (o < 0 ? -16384 : 16384)) / 32768);
    }
}
static void ac_lfo_steps(uint32_t steps)                  /* the LFOs count beats: a step is 1 / AC_DIVQ of one */
{
    uint32_t n;
    for (n = 0; n < 4u; n++) {
        uint32_t inc;
        if (!ac_v[AC_L(n, 0)])
            continue;
        inc = (1u << 24) / (AC_DIVQ[ac_v[AV_DIV] % 5u] * 4u * (1u << ac_v[AC_L(n, 2)]));
        ac_ph[n] += inc * steps;
        while (ac_ph[n] >= 1u << 24) {
            ac_ph[n] -= 1u << 24;
            if (ac_v[AC_L(n, 1)] == 2u)
                ac_hold[n] = (int16_t)((int32_t)(fa_ac_rnd() % 65535u) - 32767);
        }
    }
}
static void ac_breathe(void)                              /* every N loops a few steps nudged: back as born, or anew */
{
    uint32_t every = AC_BREV[ac_v[AV_BRATE] % 5u], len = fa_seq_count(), d;
    if (!every || ++ac_cyc < every)
        return;
    ac_cyc = 0;
    if (len < 2u)
        return;
    for (d = 0; d < ac_v[AV_BDEP]; d++) {
        uint32_t i = 1u + fa_ac_rnd() % (len - 1u);
        if (fa_ac_rnd() % 100u < ac_v[AV_BRET]) {
            uint32_t k;
            for (k = 0; k < AC_NF; k++)
                ac_f[i][k] = ac_born(i, k);
        } else
            ac_f[i][fa_ac_rnd() % AC_NF] = (uint16_t)fa_ac_rnd();
    }
    ac_build();
}
static void ac_loop_end(void)
{
    if (ac_pend) {
        ac_load(ac_pend, ac_pend_keep);
        ac_pend = 0;
    }
    ac_breathe();
}

/* each frame: the playhead (the LFOs, the loop's end), then the line into the steps when it changed */
static void fa_acid_poll(void)
{
    track_t *t = &trk[0];
    uint32_t len;
    if (!fa_acid) {
        ac_prev = -1;
        return;
    }
    len = fa_seq_count();
    if (song.playing) {
        int32_t pos = (int32_t)fa_seq_pos(t->seq_idx % fa_seq_len());
        if (pos >= (int32_t)len)
            pos = (int32_t)len - 1;
        if (ac_prev >= 0 && pos != ac_prev) {
            ac_lfo_steps((uint32_t)((pos - ac_prev + (int32_t)len) % (int32_t)len));
            if (ac_prev < (int32_t)len - 1 && (pos == (int32_t)len - 1 || pos < ac_prev))
                ac_loop_end();
        }
        ac_prev = pos;
    } else {
        ac_prev = -1;
        if (ac_pend) {
            ac_load(ac_pend, ac_pend_keep);
            ac_pend = 0;
        }
    }
    ac_offsets();
    if (ac_built[0] != ac_eff(AV_DENS) || ac_built[1] != ac_eff(AV_MOVE) || ac_built[2] != ac_eff(AV_TENS) ||
        ac_built[3] != ac_eff(AV_ACC) || ac_built[4] != ac_eff(AV_SLD) || ac_built[5] != (uint8_t)len)
        ac_build();
    if (ac_dirty)
        ac_write();
    t->p[P_SDIV] = AC_SDIV[ac_v[AV_DIV] % 5u];
    t->p[P_SGATE] = (int16_t)clamp((int32_t)(ac_eff(AV_GATE) * 128u / 100u), 1, 127);
}

/* an acid value turned (the wheels, EDIT + a knob, the ACID list) */
static void ac_turn(uint32_t av, int32_t s, uint32_t role)
{
    int32_t v = ac_v[av] + (AC_MAX[av] - AC_MIN[av] > 20 ? fmg_acc_env(role, s) : (s > 0 ? 1 : -1));
    ac_v[av] = (uint8_t)clamp(v, AC_MIN[av], AC_MAX[av]);
    if (av == AV_NOTES || av == AV_SCALE)
        ac_select_pool();
    if (av < AV_LFO || (av - AV_LFO) % 4u == 0u) {
        if (av >= AV_LFO) {                               /* a new target: the LFO from its start */
            ac_ph[(av - AV_LFO) / 4u] = 0;
            ac_hold[(av - AV_LFO) / 4u] = 0;
        }
    }
    if (fa_acid)
        ac_build();
    ac_hot = (uint8_t)av;
    ac_until = fm1_ms + 1500u;
    fa_cfg_dirty = 1;
}
static void ac_root(uint32_t k)                           /* EDIT held + key k: the root */
{
    uint32_t n = kb_map(&trk[0], k);
    if (n > 127u)
        return;
    ac_v[AV_ROOT] = (uint8_t)(n % 12u);
    if (fa_acid)
        ac_build();
    ac_hot = AV_ROOT;
    ac_until = fm1_ms + 1500u;
    fa_cfg_dirty = 1;
}
static int fa_ac_show(void) { return fa_acid && ((fa_ed_down && (fa_ed_used || (uint32_t)(fm1_ms - fa_ed_t0) >= 300u)) ||
                                                 (ac_until && (int32_t)(fm1_ms - ac_until) < 0)); }

/* EDIT: twice the line on / off; a tap a new line (off: the FILT page, as before); held, the knobs are the line's */
static void fa_go_filt(void);
static void fa_edit_btn_poll(uint32_t pressed)
{
    uint32_t down = (fm1_in.buttons >> panel.btn[B_EDIT]) & 1u;
    if ((pressed >> panel.btn[B_EDIT]) & 1u) {
        fa_ed_dbl = 0;
        if (fa_ed_pend && (uint32_t)(fm1_ms - fa_ed_rel) < 350u) {
            fa_ed_pend = 0;
            fa_ed_dbl = 1;
            ac_toggle();
        }
        fa_ed_down = 1;
        fa_ed_used = 0;
        fa_ed_t0 = fm1_ms;
    }
    if (fa_ed_down && !down) {
        fa_ed_down = 0;
        if (!fa_ed_dbl && !fa_ed_used && (uint32_t)(fm1_ms - fa_ed_t0) < 500u) {
            fa_ed_pend = 1;
            fa_ed_rel = fm1_ms;
        }
        fa_ed_dbl = 0;
    }
    if (fa_ed_pend && (uint32_t)(fm1_ms - fa_ed_rel) >= 350u) {   /* a single tap */
        fa_ed_pend = 0;
        if (fa_acid) {
            ac_request(ac_new_seed(), 0);
            ac_until = fm1_ms + 1500u;
            ac_hot = 0xFFu;
        } else {
            fa_go_filt();
        }
    }
}

/* the ACID screen: the seven values played live, the root and scale; the one turned last highlighted */
static void fa_ac_dialog(void)
{
    static const uint8_t AV[7] = {AV_DENS, AV_MOVE, AV_TENS, AV_ACC, AV_SLD, AV_NOTES, AV_OCT};
    static const char *const LB[7] = {"DENSITY", "MOVEMENT", "TENSION", "ACCENTS", "SLIDES", "NOTES", "RANGE"};
    uint32_t i;
    char b[24];
    fa_frame(ac_pend ? "ACID  NEXT LOOP" : "ACID");
    for (i = 0; i < 7u; i++) {
        fmt_int(b, ac_v[AV[i]]);
        fa_line(52 + (int32_t)i * 20, LB[i], b, ac_hot == AV[i]);
    }
    str_cpy(b, AC_NN[ac_v[AV_ROOT] % 12u], 3);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), AC_SCN[ac_v[AV_SCALE] & 7u], 8);
    fa_line(190, "ROOT", b, ac_hot == AV_ROOT);
}

/* GLO > ACID: the rest of the line's settings, the history, the favorites */
#define AC_NROW 29u
static const uint8_t AC_ROWV[26] = {AV_SCALE, AV_ROOT, AV_GATE, AV_DIV, AV_HIGH, AV_VEL, AV_AVEL, AV_BRATE, AV_BDEP, AV_BRET,
                                    AC_L(0, 0), AC_L(0, 1), AC_L(0, 2), AC_L(0, 3), AC_L(1, 0), AC_L(1, 1), AC_L(1, 2), AC_L(1, 3),
                                    AC_L(2, 0), AC_L(2, 1), AC_L(2, 2), AC_L(2, 3), AC_L(3, 0), AC_L(3, 1), AC_L(3, 2), AC_L(3, 3)};
static const char *const AC_ROWN[10] = {"SCALE", "ROOT", "NOTE LENGTH", "STEP", "HIGH RANGE", "VELOCITY", "ACCENT VEL",
                                        "BREATHE", "BREATHE DEPTH", "BREATHE BACK"};
#define AC_NFAV 7u
#define AC_FAV0 106u                                      /* the favorites' bytes (fa_cfg_byte): 7 x 15 */
static const uint8_t AC_FAVV[12] = {AV_DENS, AV_MOVE, AV_TENS, AV_GATE, AV_ACC, AV_SLD, AV_SCALE, AV_ROOT, AV_OCT,
                                    AV_NOTES, AV_HIGH, AV_DIV};
static uint32_t ac_fav_seed(uint32_t i)
{
    const uint8_t *b = fa_cfg_byte(AC_FAV0 + i * 15u);
    uint32_t s = (uint32_t)b[0] | (uint32_t)*fa_cfg_byte(AC_FAV0 + i * 15u + 1u) << 8 |
                 (uint32_t)*fa_cfg_byte(AC_FAV0 + i * 15u + 2u) << 16;
    return s <= 99999u ? s : 0u;
}
static void ac_fav_save(void)                             /* the line playing as favorite 1 (the others move down) */
{
    uint8_t rec[AC_NFAV][15];
    uint32_t i, k, n = 1;
    if (!ac_seed)
        return;
    rec[0][0] = (uint8_t)ac_seed;
    rec[0][1] = (uint8_t)(ac_seed >> 8);
    rec[0][2] = (uint8_t)(ac_seed >> 16);
    for (k = 0; k < 12u; k++)
        rec[0][3u + k] = ac_v[AC_FAVV[k]];
    for (i = 0; i < AC_NFAV && n < AC_NFAV; i++) {
        uint32_t s = ac_fav_seed(i);
        if (!s || s == ac_seed)
            continue;
        for (k = 0; k < 15u; k++)
            rec[n][k] = *fa_cfg_byte(AC_FAV0 + i * 15u + k);
        n++;
    }
    for (i = 0; i < AC_NFAV; i++)
        for (k = 0; k < 15u; k++)
            *fa_cfg_byte(AC_FAV0 + i * 15u + k) = i < n ? rec[i][k] : 0u;
    ac_fi = 0;
    fa_cfg_dirty = 1;
    fa_cfg_save();
    str_cpy(fa_msg, "FAVORITE SAVED", 16);
    fa_msg_until = fm1_ms + 1500u;
}
static void ac_fav_load(uint32_t i)
{
    uint32_t s = ac_fav_seed(i), k;
    if (!s)
        return;
    for (k = 0; k < 12u; k++) {
        uint32_t av = AC_FAVV[k], v = *fa_cfg_byte(AC_FAV0 + i * 15u + 3u + k);
        ac_v[av] = (uint8_t)clamp((int32_t)v, AC_MIN[av], AC_MAX[av]);
    }
    ac_request(s, 0);
}
static uint32_t ac_nfav(void)
{
    uint32_t i;
    for (i = 0; i < AC_NFAV && ac_fav_seed(i); i++)
        ;
    return i;
}
static void ac_row_val(uint32_t r, char *b)
{
    uint32_t av, v;
    b[0] = 0;
    if (r == 26u) {
        if (!ac_nh) {
            str_cpy(b, "-", 2);
            return;
        }
        fmt_int(b, (int32_t)ac_hi + 1);
        str_cpy(b + str_len(b), "  #", 4);
        fmt_int(b + str_len(b), (int32_t)ac_hist[ac_hi]);
        return;
    }
    if (r == 27u) {
        if (!ac_nfav()) {
            str_cpy(b, "-", 2);
            return;
        }
        fmt_int(b, (int32_t)ac_fi + 1);
        str_cpy(b + str_len(b), "  #", 4);
        fmt_int(b + str_len(b), (int32_t)ac_fav_seed(ac_fi));
        return;
    }
    if (r == 28u) {
        str_cpy(b, "ALGO >", 7);
        return;
    }
    av = AC_ROWV[r];
    v = ac_v[av];
    if (av == AV_SCALE)
        str_cpy(b, AC_SCN[v & 7u], 8);
    else if (av == AV_ROOT)
        str_cpy(b, AC_NN[v % 12u], 3);
    else if (av == AV_DIV)
        str_cpy(b, AC_DIVN[v % 5u], 5);
    else if (av == AV_BRATE)
        str_cpy(b, AC_BRN[v % 5u], 4);
    else if (av >= AV_LFO && (av - AV_LFO) % 4u == 0u)
        str_cpy(b, AC_TGN[v % 7u], 10);
    else if (av >= AV_LFO && (av - AV_LFO) % 4u == 1u)
        str_cpy(b, AC_SHN[v % 3u], 10);
    else if (av >= AV_LFO && (av - AV_LFO) % 4u == 2u) {
        fmt_int(b, 1 << v);
        str_cpy(b + str_len(b), v ? " BARS" : " BAR", 6);
    } else {
        fmt_int(b, (int32_t)v);
        if (av == AV_GATE || av == AV_BRET)
            str_cpy(b + str_len(b), "%", 2);
    }
}
static void ac_row_name(uint32_t r, char *b)
{
    if (r < 10u) {
        str_cpy(b, AC_ROWN[r], 16);
    } else if (r < 26u) {
        static const char *const LF[4] = {" TARGET", " SHAPE", " CYCLE", " DEPTH"};
        str_cpy(b, "LFO ", 5);
        b[4] = (char)('1' + (r - 10u) / 4u);
        b[5] = 0;
        str_cpy(b + 5, LF[(r - 10u) % 4u], 10);
    } else {
        str_cpy(b, r == 26u ? "HISTORY" : r == 27u ? "FAVORITES" : "SAVE FAVORITE", 16);
    }
}
static void ac_menu_turn(int32_t s)
{
    if (ac_row < 26u) {
        ac_turn(AC_ROWV[ac_row], s, EN_ALGO);
    } else if (ac_row == 26u) {
        if (ac_nh) {
            ac_hi = (uint8_t)clamp((int32_t)ac_hi + (s > 0 ? 1 : -1), 0, ac_nh - 1);
            ac_request(ac_hist[ac_hi], 1);
        }
    } else if (ac_row == 27u) {
        uint32_t n = ac_nfav();
        if (n) {
            ac_fi = (uint8_t)clamp((int32_t)ac_fi + (s > 0 ? 1 : -1), 0, (int32_t)n - 1);
            ac_fav_load(ac_fi);
        }
    } else if (s > 0) {
        ac_fav_save();
    }
}
static void ac_menu_dialog(void)
{
    uint32_t top = ac_row < 3u ? 0u : ac_row - 3u, k;
    char b[24];
    if (top + 6u > AC_NROW)
        top = AC_NROW - 6u;
    str_cpy(b, "ACID  ", 7);
    fmt_int(b + 6, (int32_t)ac_row + 1);
    str_cpy(b + str_len(b), "/", 2);
    fmt_int(b + str_len(b), AC_NROW);
    fa_frame(b);
    for (k = 0; k < 6u; k++) {
        char nm[20], v[20];
        uint32_t r = top + k;
        ac_row_name(r, nm);
        ac_row_val(r, v);
        fa_line(60 + (int32_t)k * 21, nm, v, r == ac_row);
    }
    fa_ctext(120, 190, &AF_S, ac_pend ? "NEXT LOOP: THE NEW LINE" : "SELECT  ALGO  GLO=BACK", ac_pend ? FA_OR : FA_MU);
}

/* the settings record (fa_cfg_byte): 67 0xAC, 68 the parts, 69.. the line's values, 102.. its seed, 106.. favorites,
 * 211 KNOB REC, 212 USB AUDIO (moogi m9: 1 = 10 CH; fmini_app.c fa_usb_boot, fa_cfg_save) */
static void fa_seq_cfg_load(void)
{
    uint32_t i;
    memcpy(ac_v, AC_DEF, sizeof ac_v);
    fa_parts = 1u;
    if (*fa_cfg_byte(67) != 0xACu)
        return;
    fa_parts = (uint8_t)(*fa_cfg_byte(68) & 31u);
    if (!(fa_parts & 15u))
        fa_parts = 1u;
    for (i = 0; i < AV_N; i++) {
        uint32_t v = *fa_cfg_byte(69u + i);
        ac_v[i] = (uint8_t)(v >= AC_MIN[i] && v <= AC_MAX[i] ? v : AC_DEF[i]);
    }
    fa_knob_rec = (uint8_t)(*fa_cfg_byte(211) == 1u);
    ac_seed = (uint32_t)*fa_cfg_byte(102) | (uint32_t)*fa_cfg_byte(103) << 8 | (uint32_t)*fa_cfg_byte(104) << 16;
    if (ac_seed > 99999u)
        ac_seed = 0;
    if (ac_seed) {
        ac_draw(ac_seed);
        ac_select_pool();
    }
}
static void fa_seq_cfg_save(void)
{
    uint32_t i;
    *fa_cfg_byte(67) = 0xAC;
    *fa_cfg_byte(68) = fa_parts;
    for (i = 0; i < AV_N; i++)
        *fa_cfg_byte(69u + i) = ac_v[i];
    *fa_cfg_byte(211) = fa_knob_rec;
    *fa_cfg_byte(102) = (uint8_t)ac_seed;
    *fa_cfg_byte(103) = (uint8_t)(ac_seed >> 8);
    *fa_cfg_byte(104) = (uint8_t)(ac_seed >> 16);
}

/* the keys pressed this frame that belong to SEQ (held) or to EDIT (held, the line on) */
static void fa_layer_keys(uint32_t edges)
{
    uint32_t k, seq = (fm1_in.buttons >> panel.btn[B_SEQ]) & 1u, ed = (fm1_in.buttons >> panel.btn[B_EDIT]) & 1u;
    for (k = 0; k < 27u; k++) {
        if (!((edges >> k) & 1u))
            continue;
        if (seq)
            fa_seq_key(k);
        else if (fa_dr)
            fa_dr_key(k);                                 /* moogi m6: the drum mode (fmini_drum.c) */
        else if (ed && fa_acid) {
            ac_root(k);
            fa_ed_used = 1;
        }
    }
}

/* the screen's signature of this file's state */
static uint32_t fa_seq_sig(uint32_t h)
{
    const track_t *t = &trk[0];
    uint32_t i, v = fa_sq_view;
    h = (h ^ fa_sq ^ (uint32_t)fa_sq_rec << 1 ^ (uint32_t)fa_sq_cur << 2 ^ (uint32_t)fa_parts << 9 ^ v << 14 ^
         (uint32_t)fa_acid << 16 ^ (uint32_t)fa_pl_show() << 17 ^ (uint32_t)fa_ac_show() << 18 ^ (uint32_t)ac_hot << 19) * 16777619u;
    h = (h ^ (song.playing ? t->seq_idx : 0xFFFFu) ^ (uint32_t)song.playing << 16 ^ (uint32_t)(ac_pend != 0u) << 17 ^
         (uint32_t)ac_row << 20) * 16777619u;
    h = (h ^ ac_hi ^ (uint32_t)ac_fi << 4 ^ (uint32_t)ac_nh << 8 ^ ac_seed << 12 ^ (uint32_t)fa_knob_rec << 30) * 16777619u;
    for (i = 0; i < AV_N; i++)
        h = (h ^ ac_v[i]) * 16777619u;
    for (i = 0; i < 16u; i++) {
        const step_t *s = &t->step[v * 16u + i];
        h = (h ^ (uint32_t)(s->n ? s->note[0] : 0u) ^ (uint32_t)s->time << 8 ^ (uint32_t)s->flags << 12) * 16777619u;
    }
    return h;
}
