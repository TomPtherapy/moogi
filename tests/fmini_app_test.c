/* SPDX-License-Identifier: GPL-3.0-only
 * FMini-1 screen: renders every page of fmini_app.c with the real drawing code to PPMs (OUT/fa_<page>.ppm) and
 * checks that the screen draws and that SELECT / ALGORITHM / PRESETS reach every control. */
#define UI_TEST_NO_MAIN 1
#define FELUCCA_FMINI_ONLY 1
#include "ui_test.c"
#include "../firmware/src/fmini_app.c"
#include <stdio.h>
static void dump(const char *dir, int n)
{
    char p[256];
    uint32_t i;
    FILE *f;
    snprintf(p, sizeof p, "%s/fa_%d.ppm", dir, n);
    f = fopen(p, "wb");
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}
/* the FMini-1 loop's frame: fa_input is its input (ui_input is Felucca's) */
static void fframe(void) { host_ticks += 16000u; fm1_ms += 16u; fa_input(); }
static void fframes(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) fframe(); }
static void fpress(uint32_t label)
{
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    fframe();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    fframe();
}
static void fhold(uint32_t label)
{
    uint32_t k;
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    for (k = 0; k < 52u; k++)
        fframe();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    fframe();
}
static void fturn(uint32_t role, int32_t d)
{
    host_enc[panel.enc[role]] += d * panel.dir[role];
    fframe();
    host_ticks += 200000u;
}
/* moogi m5: the norns' clock. ck_ms runs the audio in blocks with fm1_ms in step, a pulse every pulse_ms when on, the UI
 * every 16 ms; it counts the pulses sent since the last START */
static double ck_t, ck_next_pulse;
static uint32_t ck_sent;
static void ck_run(double ms, double pulse_ms, int pulses)
{
    double end = ck_t + ms, ui = ck_t;
    while (ck_t < end) {
        int32_t o[2 * CTL];
        ck_t += (double)CTL * 1000.0 / (double)FS;
        fm1_ms = (uint32_t)ck_t;
        while (pulses && ck_next_pulse <= ck_t) {
            midi_enqueue(0xFu | 0xF8u << 8, 1u);
            ck_sent++;
            ck_next_pulse += pulse_ms;
        }
        if (!pulses)
            ck_next_pulse = ck_t;
        mix_block(o, CTL);
        if (ck_t - ui >= 16.0) {
            ui = ck_t;
            host_ticks += 16000u;
            fa_input();
        }
    }
}
static void ck_button(uint32_t label, int down)
{
    if (down) {
        fm1_in.buttons |= 1u << panel.btn[label];
        host_pressed |= 1u << panel.btn[label];
    } else {
        fm1_in.buttons &= ~(1u << panel.btn[label]);
    }
    host_ticks += 16000u;
    fa_input();
}
/* moogi m6: a key pressed and let go (the UI's frame sees it), and the drums' track's last step: did lane l sound in it */
static void fkey(uint32_t k)
{
    fm1_in.notes |= 1u << k;
    host_notes |= 1u << k;
    fframe();
    fm1_in.notes &= ~(1u << k);
    fframe();
}
static int dr_played(uint32_t l)
{
    const track_t *t = &trk[FA_DR_TRK];
    uint32_t i;
    for (i = 0; i < t->seq_n; i++)
        if (t->seq_notes[i] == DRUM_LANE_NOTE[l])
            return 1;
    return 0;
}
/* the audio (and the UI every 16 ms) for one 1/16 step at the tempo: which of lanes' hits sounded in it (bit l) */
static uint32_t dr_step(void)
{
    uint32_t idx = trk[FA_DR_TRK].seq_idx, got = 0, l, guard = 0;
    while (trk[FA_DR_TRK].seq_idx == idx && guard++ < 2000u) {
        ck_t = (double)fm1_ms;
        ck_run(1.0, 1000.0, 0);
        if (!(guard % 16u)) {                              /* (the UI's frame every 16 ms) */
            host_ticks += 16000u;
            fa_input();
        }
    }
    for (l = 0; l < NLANE; l++)
        got |= (uint32_t)dr_played(l) << l;
    return got;
}
int main(int argc, char **argv)
{
    int pg;
    const char *dir = argc > 1 ? argv[1] : ".";
    { uint32_t gi; for (gi = 0; gi < G_COUNT; gi++) song.g[gi] = GP[gi].def; }   /* main.c's power-on globals */
    fa_init();
    for (pg = 0; pg < 7; pg++) {
        fa_page = (uint8_t)pg;
        fmg_cur[pg] = pg == 1 ? 5 : 1;
        ui.force = 1;
        fa_draw();
        dump(dir, pg);
    }
    {   /* every knob of FILT / F.ENV / A.ENV / FX moves when turned */
        int pgs[4] = {3, 4, 5, 6}, k, bad = 0;
        for (k = 0; k < 16; k++) {
            int pg2 = pgs[k / 4], kn = k % 4;
            uint32_t before = 0, after = 0, i;
            fa_page = (uint8_t)pg2;
            for (i = 0; i < FG_SIZE; i++) before = before * 31u + fmini_patch[0][i];
            for (i = 0; i < P_COUNT; i++) before = before * 31u + (uint32_t)TSEL->p[i];
            fmg_force_page = pg2;
            fmg_knob((uint32_t)kn, 3);
            for (i = 0; i < FG_SIZE; i++) after = after * 31u + fmini_patch[0][i];
            for (i = 0; i < P_COUNT; i++) after = after * 31u + (uint32_t)TSEL->p[i];
            if (before == after) { printf("page %d knob %d does not move\n", pg2, kn); bad++; }
        }
        if (bad) return 1;
    }
    fa_splash();
    dump(dir, 9);
    {   /* the power-on splash carries the version, bottom right, as ABOUT does: the same picture, and not the bare one */
        uint32_t i, diff = 0, same = 1;
        static uint16_t sp[240u * 240u];
        for (i = 0; i < 240u * 240u; i++) sp[i] = host_screen[i];
        fa_gl = 3; fa_sig_prev = 0; ui.force = 1; fa_draw(); fa_gl = 0;
        for (i = 0; i < 240u * 240u; i++) if (sp[i] != host_screen[i]) same = 0;
        if (!same) { printf("splash != ABOUT\n"); return 1; }
        for (i = 0; i < 240u * 240u; i++) {
            uint32_t x = i % 240u, y = i / 240u;
            if (y >= 218u && y < 232u && x >= 232u - text_w(&AF_S, FMINI_VERSION) && x < 232u && sp[i] != swap16(FMINI_SPLASH_PAL[FMINI_SPLASH[i]])) diff++;
        }
        if (!diff) { printf("splash: no version text\n"); return 1; }
    }
    fa_page = 3;                                       /* patch 100 and the SAVE dialog */
    TSEL->p[P_E7] = 99;
    fmini_load_slot(0, 99);
    ui.force = 1;
    fa_draw();
    dump(dir, 10);
    fa_sv_open();
    fa_sv_name[0] = 'M';
    ui.force = 1;
    fa_draw();
    dump(dir, 11);
    {   /* SAVE: a user preset in the first free slot, found again by PRESETS; the bank is mostly mono, its pads, keys and plucks poly */
        char nm[16];
        uint32_t before = fa_nuser();
        apply_preset_to(TSEL, 5);
        TSEL->p[P_E7] = 5;
        fmini_poll();
        fa_sv_open();
        if (!fa_sv || fa_sv_slot != 0 || before != 0) { printf("save: open\n"); return 1; }
        str_cpy(fa_sv_name, "MY PATCH", 13);
        fa_sv_do();
        if (fa_sv || fa_nuser() != 1 || !fa_on_user() || TSEL->user != 1) { printf("save: not stored\n"); return 1; }
        up_name(0, nm);
        if (!str_eq(nm, "MY PATCH")) { printf("save: name %s\n", nm); return 1; }
        apply_preset_to(TSEL, 7);
        TSEL->user = 0;
        TSEL->p[P_E7] = (int16_t)(FMINI_NFACTORY - 1u);
        fa_preset(1);                                   /* the last factory patch -> the saved one */
        if (!fa_on_user() || TSEL->user != 1) { printf("save: PRESETS does not reach it\n"); return 1; }
        fa_preset(1);                                   /* ... and round to the first factory patch */
        if (fa_on_user() || TSEL->p[P_E7] != 0) { printf("save: no wrap\n"); return 1; }
        TSEL->p[P_VOICE] = V_LEGATO;
        fa_mono();
        if (TSEL->p[P_VOICE] != V_MONO) { printf("not mono\n"); return 1; }
        TSEL->p[P_VOICE] = V_POLY;
        fa_mono();
        if (TSEL->p[P_VOICE] != V_POLY) { printf("POLY is not kept\n"); return 1; }
        TSEL->p[P_VOICE] = V_MONO;
        {   /* the bank: 100 patches, a voice mode each (leads, basses and SFX mono; pads, keys, brass chords and plucks poly), names that fit */
            uint32_t k, npoly = 0;
            if (FMINI_NFACTORY != 100u || strlen(FMINI_CATS) != 100u || NELEM(FMINI_PRESETS) != 100u) { printf("bank size %u\n", (uint32_t)FMINI_NFACTORY); return 1; }
            for (k = 0; k < FMINI_NFACTORY; k++) {
                apply_preset_to(TSEL, k);
                fa_mono();
                if (TSEL->p[P_VOICE] != (ENGINES[ENGI_FMINI]->presets[k].mono ? V_MONO : V_POLY)) { printf("preset %u: voice mode %d\n", k, TSEL->p[P_VOICE]); return 1; }
                if (strlen(ENGINES[ENGI_FMINI]->presets[k].name) > 12u) { printf("preset %u: name too long\n", k); return 1; }
                npoly += !ENGINES[ENGI_FMINI]->presets[k].mono;
            }
            if (npoly < 25u || npoly > 75u) { printf("bank: %u poly patches\n", npoly); return 1; }
            TSEL->p[P_VOICE] = V_MONO;
        }
    }
    {   /* CTRL's VOICES (the ninth control): ALGORITHM sets POLY / MONO, SAVE keeps it, a factory patch is MONO, 12 voices play */
        uint32_t k, n = 0;
        fa_page = 0;
        fmg_force_page = 0;
        fmg_cur[0] = 8;
        apply_preset_to(TSEL, 1);
        fa_mono();
        fmg_algo(1);
        if (TSEL->p[P_VOICE] != V_POLY) { printf("voices: ALGORITHM does not set POLY\n"); return 1; }
        fa_sv_open();
        fa_sv_slot = 2;
        str_cpy(fa_sv_name, "POLY PATCH", 13);
        fa_sv_do();
        apply_preset_to(TSEL, 0);
        fa_mono();
        if (TSEL->p[P_VOICE] != V_MONO) { printf("voices: the first factory patch (a lead) is not MONO\n"); return 1; }
        up_load(2);
        fa_mono();
        if (TSEL->p[P_VOICE] != V_POLY) { printf("voices: the preset does not keep POLY\n"); return 1; }
        TSEL->p[P_MUTE] = 0;
        for (k = 0; k < 12u; k++)
            trk_note_on(TSEL, (uint8_t)(48u + 3u * k), 100);
        for (k = 0; k < NVOICE; k++)
            n += TSEL->v[k].active && TSEL->v[k].gate;
        if (n != 12u) { printf("voices: %u of 12 voices sound\n", n); return 1; }
        for (k = 0; k < NVOICE; k++) TSEL->v[k].active = TSEL->v[k].gate = 0;
        fmg_algo(-1);
        if (TSEL->p[P_VOICE] != V_MONO) { printf("voices: ALGORITHM does not set MONO\n"); return 1; }
        fmg_cur[0] = 0;
        apply_preset_to(TSEL, 0);
        fa_mono();
    }
    {   /* the ARP settings and the tempo are part of a saved preset: SAVE keeps them, loading the preset sets them,
         * a factory patch brings its own (the FMINI_X table) */
        int16_t bpm0 = song.g[G_BPM];
        apply_preset_to(TSEL, 2);
        TSEL->p[P_AMODE] = 3; TSEL->p[P_ARATE] = 4; TSEL->p[P_AOCT] = 3; TSEL->p[P_AGATE] = 90; TSEL->p[P_AHOLD] = 1;
        song.g[G_BPM] = 93;
        song.octave = -2;
        song.g[G_RSIZE] = 11; song.g[G_DRREV] = 0; song.g[G_CRATE] = 100; song.g[G_DRLVL] = 127;
        fa_sv_open();
        fa_sv_slot = 1;
        str_cpy(fa_sv_name, "ARP PATCH", 13);
        fa_sv_do();
        apply_preset_to(TSEL, 4);
        song.g[G_BPM] = 150;
        song.octave = 3;
        song.g[G_RSIZE] = 50; song.g[G_DRREV] = 50; song.g[G_CRATE] = 50; song.g[G_DRLVL] = 50;
        if (TSEL->p[P_AMODE] != 0 || TSEL->p[P_AHOLD] != 0) { printf("arp: a factory patch leaves the ARP on\n"); return 1; }
        fa_preset(1);                                   /* the next factory patch (5, DUB STAB): its own FX settings, arpeggiator and octave */
        if (fa_on_user() || TSEL->p[P_E7] != 5 || song.g[G_RSIZE] != FMINI_X[5].fx[1] || song.g[G_DRLVL] != FMINI_X[5].fx[10] ||
            song.g[G_RTYPE] != FMINI_X[5].fx[0] || song.g[G_DTIME] != FMINI_X[5].fx[3] || song.octave != FMINI_X[5].oct) {
            printf("fx: factory patch 5 keeps the old FX settings (%d %d %d)\n", song.g[G_RSIZE], song.g[G_DRLVL], song.octave); return 1;
        }
        up_load(1);
        if (song.octave != -2) { printf("octave: preset load -> %d\n", song.octave); return 1; }
        if (song.g[G_RSIZE] != 11 || song.g[G_DRREV] != 0 || song.g[G_CRATE] != 100 || song.g[G_DRLVL] != 127) {
            printf("fx: preset load: %d %d %d %d\n", song.g[G_RSIZE], song.g[G_DRREV], song.g[G_CRATE], song.g[G_DRLVL]);
            return 1;
        }
        if (TSEL->p[P_AMODE] != 3 || TSEL->p[P_ARATE] != 4 || TSEL->p[P_AOCT] != 3 || TSEL->p[P_AGATE] != 90 || TSEL->p[P_AHOLD] != 1 ||
            song.g[G_BPM] != 93) {
            printf("arp: preset load: mode %d rate %d oct %d gate %d hold %d bpm %d\n", TSEL->p[P_AMODE], TSEL->p[P_ARATE],
                   TSEL->p[P_AOCT], TSEL->p[P_AGATE], TSEL->p[P_AHOLD], song.g[G_BPM]);
            return 1;
        }
        up_load(0);                                     /* the first saved patch has its own (ARP off, its tempo) */
        TSEL->p[P_AMODE] = 0; TSEL->p[P_AHOLD] = 0; TSEL->p[P_ARATE] = TP[P_ARATE].def; TSEL->p[P_AOCT] = 1; TSEL->p[P_AGATE] = TP[P_AGATE].def;
        song.g[G_BPM] = bpm0;
        song.octave = 0;
        apply_preset_to(TSEL, 0);
        TSEL->user = 0;
    }
    {   /* every factory patch brings its own arpeggiator, LFO, modulation, FX settings, LEVEL and octave (the PRESETS knob's way in);
         * about a quarter has the ARP on, the brass (50..59) comes in an octave down, the tempo stays the player's */
        uint32_t k, on = 0, fxk, bad = 0;
        int16_t bpm0 = song.g[G_BPM];
        song.g[G_BPM] = 117;
        for (k = 0; k < FMINI_NFACTORY; k++) {
            const fmini_x_t *x = &FMINI_X[k];
            TSEL->user = 0;
            if (k) {
                TSEL->p[P_E7] = (int16_t)(k - 1u);
                fa_preset(1);
            } else {
                apply_preset_to(TSEL, 0);
                TSEL->p[P_E7] = 0;
                fa_factory_extras(TSEL, 0);
            }
            if (TSEL->p[P_E7] != (int16_t)k || fa_on_user()) { printf("patch %u: PRESETS -> %d\n", k, TSEL->p[P_E7]); return 1; }
            if (TSEL->p[P_AMODE] != (x->arp[0] ? x->arp[1] : 0) || TSEL->p[P_ARATE] != x->arp[2] || TSEL->p[P_AOCT] != x->arp[3] ||
                TSEL->p[P_AGATE] != x->arp[4] || TSEL->p[P_ASWING] != x->arp[5] || TSEL->p[P_APROB] != x->arp[6] || TSEL->p[P_AHOLD] != 0) {
                printf("patch %u: arp %d %d %d %d\n", k, TSEL->p[P_AMODE], TSEL->p[P_ARATE], TSEL->p[P_AOCT], TSEL->p[P_AGATE]); bad++;
            }
            if (x->arp[0] && (x->arp[1] < 1 || x->arp[1] > 14 || x->arp[3] < 1 || x->arp[3] > 4 || x->arp[4] < 1)) { printf("patch %u: arp range\n", k); bad++; }
            on += x->arp[0];
            if (TSEL->p[P_LEVEL] != x->level || x->level < 60 || x->level > 127) { printf("patch %u: LEVEL %d\n", k, TSEL->p[P_LEVEL]); bad++; }
            if (TSEL->p[P_LRATE] != x->lfo[0] || TSEL->p[P_LWAVE] != x->lfo[1] || TSEL->p[P_LFADE] != x->lfo[2] || TSEL->p[P_LSYNC] != x->lfo[3] ||
                TSEL->p[P_LD_PIT] != x->mod[0] || TSEL->p[P_LD_AMP] != x->mod[1] || TSEL->p[P_ED_PIT] != x->mod[2]) { printf("patch %u: LFO / mod\n", k); bad++; }
            for (fxk = 0; fxk < 12u; fxk++)
                if (song.g[FA_XG[fxk]] != clamp(x->fx[fxk], GP[FA_XG[fxk]].min, GP[FA_XG[fxk]].max) || x->fx[fxk] > GP[FA_XG[fxk]].max) {
                    printf("patch %u: FX %u = %d (%d)\n", k, fxk, song.g[FA_XG[fxk]], x->fx[fxk]); bad++;
                }
            if (song.octave != x->oct || (k >= 50u && k < 60u && x->oct != -1)) { printf("patch %u: octave %d\n", k, song.octave); bad++; }
            if (k >= 30u && k < 40u && song.octave != -1) { printf("patch %u: a bass comes in with OCT-1 (%d)\n", k, song.octave); bad++; }
            if ((k == 0u || k == 2u || k == 10u) && (fmini_patch[0][FG_SW1] & FS_MOSC)) { printf("patch %u: MOD OSC is off\n", k); bad++; }
            if (k == 0u && (song.octave != 1 || TSEL->p[P_E0] != 61 || fmini_patch[0][FG_GLID] != 50)) {      /* CUTOFF -0.2 (the ladder's dial, 1.7.0), GLIDE 3.9 (-10 %), OCT+ */
                printf("patch 0: octave %d cutoff %d glide %d\n", song.octave, TSEL->p[P_E0], fmini_patch[0][FG_GLID]); bad++;
            }
            if (k == 3u && (TSEL->p[P_E0] != 70 || TSEL->p[P_REV] != 64 || song.g[G_RTYPE] != 1)) {          /* CUTOFF +0.5 (the ladder's dial, 1.7.0), REVERB 5.0, HALL */
                printf("patch 3: cutoff %d reverb %d type %d\n", TSEL->p[P_E0], TSEL->p[P_REV], song.g[G_RTYPE]); bad++;
            }
        }
        if (on < 22u || on > 28u) { printf("bank: ARP on in %u of 100 patches\n", on); bad++; }
        if (song.g[G_BPM] != 117) { printf("bank: a patch changed the tempo (%d)\n", song.g[G_BPM]); bad++; }
        if (bad) return 1;
        song.g[G_BPM] = bpm0;                            /* (a Program Change brings the same: ui.c midi_pc_load) */
        ui.mpc[0] = (uint8_t)(0x80u | 14u);              /* 14 RAVE ARP */
        midi_pc_poll();
        if (TSEL->p[P_E7] != 14 || TSEL->p[P_AMODE] != FMINI_X[14].arp[1] || song.g[G_RSIZE] != FMINI_X[14].fx[1] || song.octave != 0) {
            printf("Program Change 14: slot %d arp %d\n", TSEL->p[P_E7], TSEL->p[P_AMODE]); return 1;
        }
        ui.mpc[0] = (uint8_t)(0x80u | 55u);              /* 55 BRASS SWELL: OCT-1 */
        midi_pc_poll();
        if (TSEL->p[P_E7] != 55 || song.octave != -1 || TSEL->p[P_AMODE] != 0) { printf("Program Change 55: octave %d\n", song.octave); return 1; }
        song.octave = 0;
        TSEL->p[P_AMODE] = 0;
        TSEL->user = 0;
        apply_preset_to(TSEL, 0);
        TSEL->p[P_E7] = 0;
        fa_factory_extras(TSEL, 0);
        TSEL->p[P_AMODE] = 0;
    }
    {   /* a new preset always goes HOME (moogi: FILT, was CTRL), from any page, with the pop-ups closed: PRESETS, a Program Change, a slot change */
        uint32_t pgi;
        panel_init();
        fa_init();
        fa_gl = 0; fa_ar = 0; fa_sv = 0; fa_sq = 0;
        fframes(160);
        for (pgi = 0; pgi < 7u; pgi++) {
            if (pgi == FA_HOME_PAGE)
                continue;
            fa_page = (uint8_t)pgi;
            fmg_force_page = (int8_t)pgi;
            fa_ar = pgi == 3u;                           /* (the ARP pop-up open on one of them) */
            fturn(EN_PRESET, 1);
            if (fa_page != FA_HOME_PAGE || fa_ar) { printf("HOME: PRESETS on page %u -> page %d, arp pop-up %d\n", pgi, fa_page, fa_ar); return 1; }
        }
        fa_page = 4;
        fmg_force_page = 4;
        ui.mpc[0] = (uint8_t)(0x80u | 33u);
        fframes(48);
        if (TSEL->p[P_E7] != 33 || fa_page != FA_HOME_PAGE) { printf("HOME: Program Change -> slot %d page %d\n", TSEL->p[P_E7], fa_page); return 1; }
        fa_page = 2;
        fmg_force_page = 2;
        fframes(48);
        if (fa_page != 2) { printf("HOME: no preset change, but the page moved (%d)\n", fa_page); return 1; }
        fa_page = 0;
        fmg_force_page = 0;
    }
    {   /* power-on with the autosaved patch 00 (the track keeps its sound, the octave is not autosaved): OCT+ is on, as the preset says */
        panel_init();
        TSEL->user = 0;
        apply_preset_to(TSEL, 0);
        TSEL->p[P_E7] = 0;
        TSEL->eng_req = ENGI_FMINI;
        song.octave = 0;
        fa_init();
        if (song.octave != FMINI_X[0].oct || song.octave != 1) { printf("power-on: patch 00 octave %d\n", song.octave); return 1; }
        TSEL->p[P_E7] = 35;
        fa_init();
        if (song.octave != FMINI_X[35].oct) { printf("power-on: patch 35 octave %d\n", song.octave); return 1; }
        song.octave = 0;
    }
    {   /* a CTRL / OSC / MIX knob edits the patch (SLOT becomes OWN): PRESETS still goes on from the patch it came from (13 -> 14, then 14 -> 13) */
        static const uint8_t CASE[5][2] = {{0, 0}, {0, 1}, {0, 2}, {1, 3}, {2, 0}};     /* TUNE, GLIDE, MOD MIX; OSC DET 2; MIX OSC 1 level */
        uint32_t c;
        panel_init();
        fa_init();
        fa_gl = 0; fa_ar = 0; fa_sv = 0; fa_sq = 0;
        for (c = 0; c < 5u; c++) {
            TSEL->user = 0;
            apply_preset_to(TSEL, 13);
            TSEL->p[P_E7] = 13;
            fa_factory_extras(TSEL, 13);
            fmini_poll();
            fframes(48);                                  /* (the new preset goes HOME first: moogi's HOME is FILT) */
            fa_page = CASE[c][0];
            fmg_force_page = (int8_t)fa_page;
            fframes(48);
            fturn(EN_K1 + CASE[c][1], 3);
            fframes(48);
            if (TSEL->p[P_E7] != (int16_t)FMINI_OWN) { printf("edit %u: the slot is %d, not OWN\n", c, TSEL->p[P_E7]); return 1; }
            fturn(EN_PRESET, 1);
            fframes(48);
            if (TSEL->p[P_E7] != 14 || fa_page != FA_HOME_PAGE) { printf("edit %u: PRESETS from 13 -> %d (page %d), not 14\n", c, TSEL->p[P_E7], fa_page); return 1; }
            fa_page = CASE[c][0];
            fmg_force_page = (int8_t)fa_page;
            fturn(EN_K1 + CASE[c][1], 3);
            fframes(48);
            fturn(EN_PRESET, -1);
            fframes(48);
            if (TSEL->p[P_E7] != 13) { printf("edit %u: PRESETS from 14 -> %d, not 13\n", c, TSEL->p[P_E7]); return 1; }
        }
        fa_page = 0;
        fmg_force_page = 0;
        TSEL->user = 0;
        apply_preset_to(TSEL, 0);
        TSEL->p[P_E7] = 0;
        fa_factory_extras(TSEL, 0);
        fmini_poll();
    }
    {   /* GLIDE: the knob alone makes the track glide; ARP pop-up changes the arp; HOME */
        int i;
        fa_page = 0;
        fmg_force_page = 0;
        fmini_load_slot(0, 0);
        TSEL->p[P_E7] = 0;
        fmg_glide_sync();
        for (i = 0; i < 10; i++)
            fmg_knob(1, 2);
        if (TSEL->p[P_GLIDE] <= 0) { printf("glide knob does not glide (%d)\n", TSEL->p[P_GLIDE]); return 1; }
        fa_ar = 1;
        fa_ar_turn(0, 1);                               /* MODE: OFF -> UP */
        fa_ar_turn(1, 1);
        fa_ar_turn(4, 1);                               /* HOLD on */
        if (!TSEL->p[P_AHOLD]) { printf("arp hold\n"); return 1; }
        fa_ar_turn(5, 5);
        if (TSEL->p[P_AMODE] != 1) { printf("arp mode\n"); return 1; }
        ui.force = 1;
        fa_draw();
        dump(dir, 12);
        fa_ar = 0;
        fa_page = 5;
        fa_goto(0);
        if (fa_page != 0) { printf("home\n"); return 1; }
    }
    {   /* GLO: MIDI IN / CHANNEL filter, every control answers to its CC, the pop-ups draw */
        uint32_t n, m, bad = 0;
        fa_cfg_load();
        for (n = 0; n < FA_NCC; n++) {
            if (!fa_cc[n] || !fa_cc_valid(fa_cc[n])) { printf("cc default %u\n", n); return 1; }
            for (m = n + 1u; m < FA_NCC; m++)
                if (fa_cc[n] == fa_cc[m]) { printf("cc %u twice (%u, %u)\n", fa_cc[n], n, m); return 1; }
        }
        fmini_load_slot(0, 0);
        for (n = 0; n < FA_NCC; n++) {                  /* a CC of each control moves it */
            uint32_t h0 = 0, h1 = 0, i;
            fa_ctl_apply(n, 0);
            for (i = 0; i < FG_SIZE; i++) h0 = h0 * 31u + fmini_patch[0][i];
            for (i = 0; i < P_COUNT; i++) h0 = h0 * 31u + (uint32_t)TSEL->p[i];
            for (i = 0; i < G_COUNT; i++) h0 = h0 * 31u + (uint32_t)song.g[i];
            fa_ctl_apply(n, 127);
            for (i = 0; i < FG_SIZE; i++) h1 = h1 * 31u + fmini_patch[0][i];
            for (i = 0; i < P_COUNT; i++) h1 = h1 * 31u + (uint32_t)TSEL->p[i];
            for (i = 0; i < G_COUNT; i++) h1 = h1 * 31u + (uint32_t)song.g[i];
            if (h0 == h1) { const char *tg; char nm[16]; fa_ctl_name(n, &tg, nm); printf("control %u (%s %s) ignores its CC\n", n, tg, nm); bad++; }
        }
        if (bad) return 1;
        /* via the ISR's notes: CC74 -> CUTOFF */
        fa_cc_i = 0;
        fa_gl = 0;
        TSEL->p[P_E0] = 10;
        fm_cc_rx[74] = 100;
        fm_cc_pend[74 >> 5] |= 1u << (74 & 31);
        fa_cc_poll();
        if (TSEL->p[P_E0] == 10) { printf("CC74 did not reach the cutoff\n"); return 1; }
        /* learn: in the MIDI CC list the next CC is taken for the highlighted line */
        fa_gl = 2;
        fa_cc_i = 3;
        fm_cc_rx[99 - 1] = 5;
        fm_cc_pend[0] = 0;
        fm_cc_pend[(80) >> 5] |= 1u << (80 & 31);
        fa_cc_poll();
        if (fa_cc[3] != 80) { printf("learn: %u\n", fa_cc[3]); return 1; }
        /* the filter: USB 1, MIDI 2, BT 3 */
        fm_midi_src = 0; fm_midi_ch = 0;
        fm_bt = 1;
        if (!fm_midi_ok(1, 5) || fm_midi_ok(2, 5) || fm_midi_ok(3, 5)) { printf("src USB\n"); return 1; }
        fm_midi_src = 1;
        if (fm_midi_ok(1, 5) || !fm_midi_ok(2, 5)) { printf("src MIDI\n"); return 1; }
        fm_midi_src = 3;
        if (!fm_midi_ok(1, 0) || !fm_midi_ok(2, 0) || !fm_midi_ok(3, 0)) { printf("src ALL\n"); return 1; }
        fm_midi_ch = 3;
        if (!fm_midi_ok(1, 2) || fm_midi_ok(1, 3) || fm_midi_ok(1, 0)) { printf("channel 3\n"); return 1; }
        fm_midi_ch = 0;
        /* the pop-ups */
        fa_page = 1;
        fa_gl = 1; fa_gl_row = 0; fm_midi_src = 3; ui.force = 1; fa_draw(); dump(dir, 13);
        fa_gl = 2; fa_cc_i = 20; ui.force = 1; fa_draw(); dump(dir, 14);
        fa_gl = 3; ui.force = 1; fa_draw(); dump(dir, 15);
        fa_gl = 0; fm_midi_src = 3;
        fa_ar = 1; ui.force = 1; fa_draw(); dump(dir, 16);
        fa_ar = 0;
    }
    {   /* preset number always valid, the pop-up, pitch bend ranges, mod wheel */
        char num[8], nm[16];
        uint32_t k;
        fa_init();
        fa_gl = 0; fa_ar = 0; fa_sv = 0;
        apply_preset_to(TSEL, 42);
        TSEL->p[P_E7] = 42;
        fmini_poll();
        fa_track_preset();
        fa_preset_label(num, nm);
        if (strcmp(num, "42") || !fa_pp_on()) { printf("label %s pop %d\n", num, fa_pp_on()); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 17);
        fm1_ms += 1600;
        if (fa_pp_on()) { printf("pop-up stays\n"); return 1; }
        fmg_write(FG_TUNE, 70);                          /* edited: SLOT OWN, the number stays */
        fa_track_preset();
        fa_preset_label(num, nm);
        if (strcmp(num, "42") || fa_pp_on() || nm[0] == 0) { printf("edited label %s\n", num); return 1; }
        for (k = 0; k < FMINI_NFACTORY; k++) {            /* every number 00..99 */
            TSEL->p[P_E7] = (int16_t)k;
            fa_track_preset();
            fa_preset_label(num, nm);
            if (num[0] < '0' || num[0] > '9' || num[1] < '0' || num[1] > '9' || num[2]) { printf("label %u\n", k); return 1; }
        }
        for (k = 0; k < 4; k++) {                        /* GLO > PITCH BEND */
            static const int UP[4] = {2, 12, 2, 2}, DN[4] = {2, 12, 12, 24};
            fm_bend_mode = (uint8_t)k;
            midi_event(0xE0u, 0, 127, 127);
            if (midi_bend_target[0] != UP[k] * 256) { printf("bend up mode %u: %d\n", k, midi_bend_target[0]); return 1; }
            midi_event(0xE0u, 0, 0, 0);
            if (midi_bend_target[0] != -DN[k] * 256) { printf("bend down mode %u: %d\n", k, midi_bend_target[0]); return 1; }
            midi_event(0xE0u, 0, 0, 64);
            if (midi_bend_target[0] != 0) { printf("bend centre\n"); return 1; }
        }
        midi_event(0xB0u, 0, 1, 127);                    /* the mod wheel (CC1) reaches the track */
        if (trk[0].mw != 127) { printf("mod wheel not stored (%d)\n", trk[0].mw); return 1; }
        midi_event(0xB0u, 0, 1, 0);
        if (trk[0].mw != 0) { printf("mod wheel back\n"); return 1; }
    }
    {   /* buttons: ARP tap / hold, SEQ + REC + PLAY, the lit keys, GLO fixes */
        panel_init();
        uint32_t sig, k, i;
        fa_init();
        trk[0].p[P_AMODE] = 0; trk[0].p[P_AHOLD] = 0; fa_ar = 0;
        fpress(B_ARP);
        if (trk[0].p[P_AMODE] != 1 || !fa_ar) { printf("arp tap on (%d) down=%d done=%d btn=%u pressedmask=%u\n", trk[0].p[P_AMODE], fa_arp_down, fa_arp_done, panel.btn[B_ARP], host_pressed); return 1; }
        fpress(B_ARP);
        if (trk[0].p[P_AMODE] != 0) { printf("arp tap off\n"); return 1; }
        fpress(B_ARP);
        fhold(B_ARP);
        if (!trk[0].p[P_AHOLD] || trk[0].p[P_AMODE] != 1) { printf("arp hold: hold %d mode %d\n", trk[0].p[P_AHOLD], trk[0].p[P_AMODE]); return 1; }
        fhold(B_ARP);
        if (trk[0].p[P_AHOLD]) { printf("arp hold off\n"); return 1; }
        fframes(4500);
        if (fa_ar) { printf("arp pop-up stays\n"); return 1; }
        trk[0].p[P_AMODE] = 0;
        /* Acid Moogi: the sequencer on the keys */
        fa_seq_clear();
        fa_parts = 1;
        if (!(kb_mask & (1u << panel.btn[B_SEQ]))) { printf("SEQ is not a key layer\n"); return 1; }
        fpress(B_REC);                                   /* stopped: step record, the cursor on step 1 */
        if (!fa_sq_rec || fa_sq_cur != 0 || !fa_seq_stepping()) { printf("step rec arm\n"); return 1; }
        input_on(&trk[0], 60, 100); input_off(&trk[0], 60);
        fpress(B_ARP);                                   /* ARP: a rest (the arpeggiator stays off) */
        if (trk[0].p[P_AMODE] || fa_sq_cur != 2 || trk[0].step[1].time != ST_REST) { printf("step rec rest %d %d\n", fa_sq_cur, trk[0].p[P_AMODE]); return 1; }
        input_on(&trk[0], 62, 100); input_on(&trk[0], 65, 100);   /* 65 pressed while 62 is held: a slide */
        input_off(&trk[0], 62); input_off(&trk[0], 65);
        input_on(&trk[0], 67, 100); input_off(&trk[0], 67);
        trk_all_off(&trk[0]);
        if (trk[0].step[0].note[0] != 60 || trk[0].step[2].note[0] != 62 || trk[0].step[3].note[0] != 65 || trk[0].step[4].note[0] != 67 ||
            !(trk[0].step[2].flags & SF_SLIDE) || (trk[0].step[3].flags & SF_SLIDE) || fa_sq_cur != 5) { printf("step rec notes / slide (cursor %d)\n", fa_sq_cur); return 1; }
        ui.force = 1; fa_draw();
        {   /* SEQ held: a white key moves the cursor, black 2 twice turns part 2 on, black 5 is HALF; the keys play nothing */
            uint32_t w8 = fa_white_key(8), b2 = fa_black_key(1), b5 = fa_black_key(4);
            fm1_in.buttons |= 1u << panel.btn[B_SEQ]; host_pressed |= 1u << panel.btn[B_SEQ]; fframe();
            if (!fa_sq) { printf("SEQ held\n"); return 1; }
            host_notes |= 1u << w8; fframe();
            if (fa_sq_cur != 8) { printf("SEQ + white key: cursor %d\n", fa_sq_cur); return 1; }
            host_notes |= 1u << b2; fframe();
            if (fa_parts != 1 || fa_sq_view != 1) { printf("SEQ + black 2 once: parts %d view %d\n", fa_parts, fa_sq_view); return 1; }
            fframes(100);
            host_notes |= 1u << b2; fframe();
            if (fa_parts != 3 || trk[0].p[P_SLEN] != 32 || fa_seq_count() != 32) { printf("SEQ + black 2 twice: parts %d len %d\n", fa_parts, trk[0].p[P_SLEN]); return 1; }
            host_notes |= 1u << b5; fframe();
            if (fa_parts != 19 || fa_seq_count() != 24 || trk[0].p[P_SLEN] != 32) { printf("HALF: parts %d count %u\n", fa_parts, fa_seq_count()); return 1; }
            ui.force = 1; fa_draw(); dump(dir, 18);
            fa_leds();
            fm1_in.buttons &= ~(1u << panel.btn[B_SEQ]); fframe();
            if (fa_sq) { printf("SEQ let go\n"); return 1; }
        }
        fpress(B_REC);
        if (fa_sq_rec) { printf("rec off\n"); return 1; }
        {   /* parts 1 and 3 play, part 2 is skipped; 1, 3 with HALF: 1..16 then 33..40 */
            uint32_t seen1 = 0, seen2 = 0, seen3 = 0, past = 0, j;
            for (j = 0; j < 64u; j++) { memset(&trk[0].step[j], 0, sizeof trk[0].step[j]); trk[0].step[j].note[0] = (uint8_t)(40u + j % 24u); trk[0].step[j].n = 1; trk[0].step[j].time = ST_NOTE; }
            fa_parts = 1 | 4 | 16;
            fframe();
            if (trk[0].p[P_SLEN] != 48 || fa_seq_count() != 24) { printf("parts 1+3 half: len %d count %u\n", trk[0].p[P_SLEN], fa_seq_count()); return 1; }
            fpress(B_PLAY);
            if (transport_req != 1) { printf("play request\n"); return 1; }
            for (i = 0; i < 6000; i++) {
                int32_t o[2 * CTL];
                mix_block(o, CTL);
                if (song.playing) {
                    uint32_t q = trk[0].seq_idx;
                    seen1 |= q < 16u; seen2 |= q >= 16u && q < 32u; seen3 |= q >= 32u && q < 40u; past |= q >= 40u;
                }
            }
            if (!song.playing || !seen1 || seen2 || !seen3 || past) { printf("part skip: playing %d 1:%u 2:%u 3:%u past:%u\n", song.playing, seen1, seen2, seen3, past); return 1; }
            fa_leds();
            ui.force = 1; fa_draw(); dump(dir, 19);
            fpress(B_PLAY);
            events_block(CTL);
            if (song.playing) { printf("stop\n"); return 1; }
        }
        {   /* live record: REC while playing; a note pressed while the last is held slides into it; a hard note no accent */
            uint32_t j, slides = 0, acc = 0, n55 = 0, n48 = 0;
            fa_seq_clear();
            fa_parts = 1;
            fframe();
            fpress(B_PLAY);
            for (i = 0; i < 40; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
            fpress(B_REC);
            if (song.rec != 1 || !fa_sq_rec) { printf("live rec arm (rec %d)\n", song.rec); return 1; }
            input_on(&trk[0], 48, 127);
            for (i = 0; i < 400; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
            input_on(&trk[0], 55, 127);
            input_off(&trk[0], 48);
            for (i = 0; i < 200; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
            input_off(&trk[0], 55);
            for (j = 0; j < 16u; j++) {
                const step_t *st = &trk[0].step[j];
                slides += (st->flags & SF_SLIDE) != 0;
                acc += (st->flags & SF_ACCENT) != 0;
                n55 += st->time == ST_NOTE && st->n && st->note[0] == 55;
                n48 += st->time == ST_NOTE && st->n && st->note[0] == 48;
            }
            if (slides != 1 || acc || n55 != 1 || n48 != 1) { printf("live rec: slides %u accents %u 48:%u 55:%u\n", slides, acc, n48, n55); return 1; }
            fpress(B_REC);
            fpress(B_PLAY);
            events_block(CTL);
            trk_all_off(&trk[0]);
            if (song.playing || song.rec) { printf("live rec stop\n"); return 1; }
        }
        {   /* the knobs' motion: recorded with the notes only with KNOB REC on; REC + ARP undoes it; SEQ + REC clears it */
            fa_seq_clear();
            fa_parts = 1;
            fa_page = 3;
            fa_knob_rec = 0;
            fframe();
            fpress(B_PLAY);
            for (i = 0; i < 40; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
            fpress(B_REC);
            fturn(EN_K1, 3);
            for (i = 0; i < 200; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
            if (motion_count(&trk[0])) { printf("KNOB REC off: %u knob events recorded\n", motion_count(&trk[0])); return 1; }
            fpress(B_REC);
            fa_knob_rec = 1;
            fpress(B_REC);
            if (!fa_sq_rec) { printf("rec arm on release\n"); return 1; }
            fframes(200); fturn(EN_K1, 3);
            for (i = 0; i < 200; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
            fframes(200); fturn(EN_K1, -2);
            if (!motion_count(&trk[0])) { printf("KNOB REC on: nothing recorded\n"); return 1; }
            fm1_in.buttons |= 1u << panel.btn[B_REC]; host_pressed |= 1u << panel.btn[B_REC]; fframe();
            fm1_in.buttons |= 1u << panel.btn[B_ARP]; host_pressed |= 1u << panel.btn[B_ARP]; fframe();
            fm1_in.buttons &= ~(1u << panel.btn[B_ARP]); fframe();
            fm1_in.buttons &= ~(1u << panel.btn[B_REC]); fframe();
            if (motion_count(&trk[0]) || trk[0].p[P_AMODE] || !fa_sq_rec) { printf("REC + ARP: %u left, arp %d, rec %d\n", motion_count(&trk[0]), trk[0].p[P_AMODE], fa_sq_rec); return 1; }
            fframes(200); fturn(EN_K1, 3);
            for (i = 0; i < 100; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
            if (!motion_count(&trk[0])) { printf("KNOB REC again: nothing recorded\n"); return 1; }
            fm1_in.buttons |= 1u << panel.btn[B_SEQ]; host_pressed |= 1u << panel.btn[B_SEQ]; fframe();
            fpress(B_REC);
            fm1_in.buttons &= ~(1u << panel.btn[B_SEQ]); fframe();
            if (motion_count(&trk[0]) || !fa_sq_rec) { printf("SEQ + REC: knobs %u, rec %d\n", motion_count(&trk[0]), fa_sq_rec); return 1; }
            fpress(B_REC);
            fpress(B_PLAY);
            events_block(CTL);
            trk_all_off(&trk[0]);
            fa_knob_rec = 0;
            if (song.playing || fa_sq_rec) { printf("knob rec stop\n"); return 1; }
            fmini_fine[0][0] = fmini_fine[0][1] = fmini_fine[0][2] = -1;   /* (the CUTOFF turned here: no quarter step left for the glide test) */
        }
        {   /* the tempo: PLAY held + SELECT; the hold does not start the sequence, a tap does */
            int32_t b0 = song.g[G_BPM] = 120;
            fm1_in.buttons |= 1u << panel.btn[B_PLAY]; host_pressed |= 1u << panel.btn[B_PLAY]; fframe();
            fframes(300);
            if (!fa_pl_show()) { printf("tempo screen\n"); return 1; }
            fturn(EN_SELECT, 1); fframes(200); fturn(EN_SELECT, 1); fframes(200); fturn(EN_SELECT, -1); fframes(200); fturn(EN_SELECT, 1);
            if (song.g[G_BPM] != b0 + 2) { printf("tempo %d -> %d\n", b0, song.g[G_BPM]); return 1; }
            ui.force = 1; fa_draw(); dump(dir, 41);
            fm1_in.buttons &= ~(1u << panel.btn[B_PLAY]); fframe();
            if (transport_req) { printf("tempo hold started the sequence\n"); return 1; }
            if (fa_page == 0) fa_page = 3;
        }
        {   /* the acid line: EDIT twice on (it lights, the line is written), a tap a new line, the wheels and EDIT + knobs */
            uint32_t j, notes = 0, s0, sig0;
            uint8_t before[16];
            fa_parts = 1;
            fa_seq_clear();
            fframes(500);
            fpress(B_EDIT); fframes(100); fpress(B_EDIT);
            if (!fa_acid) { printf("acid: EDIT twice\n"); return 1; }
            fframe();
            for (j = 0; j < 16u; j++) notes += trk[0].step[j].time == ST_NOTE && trk[0].step[j].n;
            if (!notes || trk[0].step[0].time != ST_NOTE || !(kb_mask & (1u << panel.btn[B_EDIT]))) { printf("acid: no line (%u notes)\n", notes); return 1; }
            fa_leds();
            s0 = ac_seed;
            fframes(500);
            fpress(B_EDIT); fframes(400);              /* a tap: a new line (stopped: now) */
            if (ac_seed == s0 || !fa_acid) { printf("acid: new line %u %u\n", s0, ac_seed); return 1; }
            ac_load(12345u, 0); fframe();
            for (j = 0; j < 16u; j++) before[j] = trk[0].step[j].n ? trk[0].step[j].note[0] : 0;
            {   /* moogi m5: without EDIT the wheels are the page's: SELECT does not touch DENSITY, ALGORITHM not MOVEMENT */
                int16_t d0 = ac_v[AV_DENS], m0 = ac_v[AV_MOVE];
                fturn(EN_SELECT, -1); fframes(100);
                fturn(EN_ALGO, 1); fframes(100);
                if (ac_v[AV_DENS] != d0 || ac_v[AV_MOVE] != m0) { printf("acid: the wheels without EDIT %d %d\n", ac_v[AV_DENS], ac_v[AV_MOVE]); return 1; }
                fturn(EN_ALGO, -1); fframes(100);
                fturn(EN_SELECT, 1); fframes(100);
            }
            fm1_in.buttons |= 1u << panel.btn[B_EDIT]; host_pressed |= 1u << panel.btn[B_EDIT]; fframe();
            fturn(EN_SELECT, -1); fframes(100);
            fturn(EN_SELECT, -1); fframes(100);
            if (ac_v[AV_DENS] != 58) { printf("acid: EDIT + SELECT density %d\n", ac_v[AV_DENS]); return 1; }
            ac_v[AV_DENS] = 60; ac_build(); fframe();
            for (j = 0; j < 16u; j++) if (before[j] != (trk[0].step[j].n ? trk[0].step[j].note[0] : 0)) { printf("acid: same seed, same line (%u)\n", j); return 1; }
            fturn(EN_PRESET, 1); fframes(100);
            if (ac_v[AV_TENS] != 26) { printf("acid: EDIT + PRESETS tension %d\n", ac_v[AV_TENS]); return 1; }
            fturn(EN_K1 + 2, 1); fframes(100);
            if (ac_v[AV_NOTES] != 4) { printf("acid: EDIT + K3 notes %d\n", ac_v[AV_NOTES]); return 1; }
            host_notes |= 1u << fa_white_key(2); fframe();
            if (ac_v[AV_ROOT] != kb_map(&trk[0], fa_white_key(2)) % 12u) { printf("acid: EDIT + key root %d\n", ac_v[AV_ROOT]); return 1; }
            if (!fa_ac_show()) { printf("acid screen\n"); return 1; }
            ui.force = 1; fa_draw(); dump(dir, 42);
            fm1_in.buttons &= ~(1u << panel.btn[B_EDIT]); fframes(500);
            if (!fa_acid || ac_pend) { printf("acid: EDIT hold with a knob is no tap\n"); return 1; }
            ac_v[AV_ROOT] = 0;
            fa_gl = 4; ac_row = 10;                       /* GLO > ACID: LFO 1 on DENSITY; playing, the line moves */
            fturn(EN_ALGO, 1);
            if (ac_v[AC_L(0, 0)] != 1) { printf("acid: LFO target %d\n", ac_v[AC_L(0, 0)]); return 1; }
            ac_v[AC_L(0, 2)] = 0; ac_v[AC_L(0, 3)] = 50;
            ui.force = 1; fa_draw(); dump(dir, 43);
            fa_gl = 0;
            sig0 = 0;
            fpress(B_PLAY);
            {   uint32_t moved = 0, d0 = ac_built[0];
                for (i = 0; i < 3000; i++) {
                    int32_t o[2 * CTL];
                    mix_block(o, CTL);
                    if ((i & 15u) == 0u) { fframe(); moved |= ac_built[0] != d0; }
                }
                if (!moved) { printf("acid: the LFO does not move DENSITY\n"); return 1; }
            }
            fpress(B_EDIT); fframes(400);              /* playing: the new line waits for the loop's end */
            (void)sig0;
            for (i = 0; i < 3000 && ac_pend; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); if ((i & 15u) == 0u) fframe(); }
            if (ac_pend) { printf("acid: the new line never came\n"); return 1; }
            ac_v[AC_L(0, 0)] = 0;
            fpress(B_PLAY);
            events_block(CTL);
            fpress(B_EDIT); fframes(100); fpress(B_EDIT);
            if (fa_acid) { printf("acid: EDIT twice off\n"); return 1; }
            fframes(500);
            trk_all_off(&trk[0]);
        }
        /* the keys of a sounding note light */
        trk_note_on(&trk[0], kb_map(&trk[0], 5), 100);
        if (!((fa_sound_keys() >> 5) & 1u)) { printf("key led\n"); return 1; }
        trk_all_off(&trk[0]);
        fa_gl = 1; fa_gl_row = 0; fm_midi_src = 3;
        fturn(EN_ALGO, 1);
        if (fm_midi_src != 3) { printf("src max\n"); return 1; }
        fturn(EN_ALGO, -1); fturn(EN_ALGO, -1); fturn(EN_ALGO, -1);
        if (fm_midi_src != 0) { printf("src min\n"); return 1; }
        fturn(EN_SELECT, -1);
        if (fa_gl_row != FA_GL_N - 1u) { printf("rows wrap\n"); return 1; }   /* (moogi m6: ABOUT is row 7) */
        /* GLO: PITCH BEND draws at once, ABOUT closes with either encoder */
        fa_gl = 1; fa_gl_row = 3; fm_bend_mode = 0;
        sig = fa_signature();
        fturn(EN_ALGO, 1);
        if (fm_bend_mode != 1 || fa_signature() == sig) { printf("bend redraw\n"); return 1; }
        /* GLO order: MIDI IN, CHANNEL, MIDI CC (above PITCH BEND), PITCH BEND, ABOUT */
        fa_gl = 1; fa_gl_row = 1; fturn(EN_SELECT, 1);
        if (fa_gl_row != 2) { printf("MIDI CC is row 2\n"); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 40);
        fturn(EN_ALGO, 1);
        if (fa_gl != 2) { printf("MIDI CC opens\n"); return 1; }
        fa_gl = 1; fa_gl_row = 2; fturn(EN_SELECT, 1);
        if (fa_gl_row != 3) { printf("PITCH BEND follows MIDI CC\n"); return 1; }
        fa_gl = 1; fa_gl_row = 4; fa_knob_rec = 0; fturn(EN_ALGO, 1);
        if (fa_gl != 1 || !fa_knob_rec) { printf("KNOB REC on\n"); return 1; }
        fturn(EN_ALGO, -1);
        if (fa_knob_rec) { printf("KNOB REC off\n"); return 1; }
        fa_gl = 1; fa_gl_row = 5; fturn(EN_ALGO, 1);
        if (fa_gl != 4) { printf("ACID opens\n"); return 1; }
        {   /* moogi m7: GLO > DRUMS (row 6) opens its page: KIT steps through the kits (the drums' track takes it), SIDECHAIN
             * 0..100 by 5 (0 OFF), RELEASE 0..100; GLO goes back to the list */
            uint8_t was = fa_dr_kit;
            fa_gl = 1; fa_gl_row = 6; fa_dr_kit = DK_STD; trk[FA_DR_TRK].p[P_E0] = DK_STD;
            fturn(EN_ALGO, 1);
            if (fa_gl != 5) { printf("GLO > DRUMS does not open (%d)\n", fa_gl); return 1; }
            fa_dr_row = 0;
            fturn(EN_ALGO, 1);
            if (fa_dr_kit != DK_80 || trk[FA_DR_TRK].p[P_E0] != DK_80) { printf("DRUMS > KIT: %d\n", fa_dr_kit); return 1; }
            fturn(EN_ALGO, -1); fturn(EN_ALGO, -1);
            if (fa_dr_kit != DK_STD) { printf("DRUMS > KIT stops at STD\n"); return 1; }
            fturn(EN_SELECT, 1); fa_sc_amt = 0;
            fturn(EN_ALGO, 1); fturn(EN_ALGO, 1);
            if (fa_dr_row != 1u || fa_sc_amt != 10u) { printf("DRUMS > SIDECHAIN %u\n", fa_sc_amt); return 1; }
            fturn(EN_ALGO, -1); fturn(EN_ALGO, -1); fturn(EN_ALGO, -1);   /* (5 a detent, whatever the speed) */
            if (fa_sc_amt != 0u) { printf("DRUMS > SIDECHAIN stops at OFF\n"); return 1; }
            fturn(EN_SELECT, 1); fa_sc_rel = 4;
            fturn(EN_ALGO, 1);
            if (fa_dr_row != 2u || fa_sc_rel != 5u) { printf("DRUMS > RELEASE %u\n", fa_sc_rel); return 1; }
            fturn(EN_ALGO, 1); fturn(EN_ALGO, 1); fturn(EN_ALGO, 1); fturn(EN_ALGO, 1);
            if (fa_sc_rel != 8u) { printf("DRUMS > RELEASE stops at 1/2 (%u)\n", fa_sc_rel); return 1; }
            fa_sc_rel = 1; fturn(EN_ALGO, -1);
            if (fa_sc_rel != 1u) { printf("DRUMS > RELEASE stops at 1/16\n"); return 1; }
            fa_sc_rel = 4;
            fturn(EN_SELECT, 1);
            if (fa_dr_row != 0u) { printf("DRUMS rows wrap\n"); return 1; }
            fa_pp_until = 0; ui.force = 1; fa_draw(); dump(dir, 25);
            fpress(B_GLO);
            if (fa_gl != 1) { printf("GLO from DRUMS: back to the list (%d)\n", fa_gl); return 1; }
            fa_dr_kit = was; trk[FA_DR_TRK].p[P_E0] = was;
        }
        fa_gl = 1; fa_gl_row = 7; fturn(EN_ALGO, 1);
        if (fa_gl != 3) { printf("ABOUT opens\n"); return 1; }
        fa_gl = 3; fturn(EN_ALGO, 1);
        if (fa_gl != 1) { printf("about ALGO\n"); return 1; }
        fa_gl = 3; fturn(EN_SELECT, 1);
        if (fa_gl != 1) { printf("about SELECT\n"); return 1; }
        fa_gl = 0;
        {   /* FX: the cursor walks onto the four EDIT buttons; ALGORITHM opens each pop-up, K1..K4 edit its four values */
            static const uint8_t PMAP[4][4] = {{P_REV, G_RTYPE, G_RSIZE, G_RDAMP}, {P_DLY, G_DTIME, G_DRREV, G_DFDBK},
                                               {P_CHOR, G_ENGGO, G_CDEPTH, G_CRATE}, {P_DIST, G_INFO, G_DRLVL, G_MIDI}};
            uint32_t q, k;
            fa_ar = 0; fa_page = 6; fmg_cur[6] = 3; fmg_fx = 0;
            for (q = 0; q < 4u; q++) {
                fturn(EN_SELECT, 1); fframes(64);
                if (fmg_cur[6] != 4u + q) { printf("FX cursor %u -> %u\n", q, fmg_cur[6]); return 1; }
                fturn(EN_ALGO, 1); fframes(64);
                if (fmg_fx != q + 1u) { printf("EDIT %u opens pop-up %u\n", q, fmg_fx); return 1; }
                for (k = 0; k < 4u; k++) {
                    int32_t b4 = k == 0u ? TSEL->p[PMAP[q][0]] : song.g[PMAP[q][k]];
                    uint32_t g0;
                    if (k == 0u) TSEL->p[PMAP[q][0]] = 30; else song.g[PMAP[q][k]] = GP[PMAP[q][k]].def;
                    b4 = k == 0u ? TSEL->p[PMAP[q][0]] : song.g[PMAP[q][k]];
                    g0 = fa_signature();
                    fturn(EN_K1 + k, 1); fframes(64);
                    fturn(EN_K1 + k, 1); fframes(64);
                    if ((k == 0u ? TSEL->p[PMAP[q][0]] : song.g[PMAP[q][k]]) == b4) { printf("FX pop-up %u knob %u does not move (%d)\n", q, k, (int)b4); return 1; }
                    if (fa_signature() == g0) { printf("FX pop-up %u knob %u: no redraw\n", q, k); return 1; }
                }
                ui.force = 1; fa_draw(); dump(dir, 20 + (int)q);
                fpress(B_FX);                              /* FX again: the next pop-up (after the last, the page) */
                if (fmg_fx != (q < 3u ? q + 2u : 0u)) { printf("FX cycles %u -> %u\n", q, fmg_fx); return 1; }
                fmg_fx = 0; fmg_cur[6] = (uint8_t)(4u + q);
            }
            {   /* a type's names: the five reverbs, seven delays, three choruses */
                char v[16]; const char *u; uint32_t n;
                static const struct { uint8_t id; uint8_t n; const char *last; } TY[4] = {{G_RTYPE, 5, "CLOUD"}, {G_DTIME, 7, "REVERSE"}, {G_ENGGO, 3, "MULTI"}, {G_INFO, 2, "TUBE"}};
                for (n = 0; n < 4u; n++) {
                    str_cpy(v, GP[TY[n].id].names[GP[TY[n].id].max], 16);
                    if (GP[TY[n].id].max + 1 != TY[n].n || strcmp(v, TY[n].last)) { printf("type list %u: %d '%s'\n", n, GP[TY[n].id].max + 1, v); return 1; }
                }
            }
            {   /* TAP TEMPO (SCL in the delay pop-up): two taps 500 ms apart set the delay TIME to ~500 ms and do not leave the page */
                int32_t ms;
                fmg_fx = 2; fa_page = 6;
                fm1_ms += 3000u; fpress(B_SCL);
                fm1_ms += 500u - 32u; fpress(B_SCL);
                ms = dly_ms(song.g[G_DRREV]);
                if (fa_page != 6 || ms < 480 || ms > 520) { printf("tap tempo: page %d, %d ms\n", fa_page, (int)ms); return 1; }
                fmg_fx = 0;
            }
        }
        fpress(B_SCL);                                   /* SCL: OSC, then MIX, then OSC */
        if (fa_page != 1) { printf("SCL -> OSC (%d)\n", fa_page); return 1; }
        fpress(B_SCL);
        if (fa_page != 2) { printf("SCL -> MIX (%d)\n", fa_page); return 1; }
        fpress(B_SCL);
        if (fa_page != 1) { printf("SCL -> OSC again (%d)\n", fa_page); return 1; }
        fpress(B_EDIT); fframes(400);                    /* EDIT (the acid line off): FILT, once it is not a double press */
        if (fa_page != 3) { printf("EDIT -> FILT (%d)\n", fa_page); return 1; }
        fpress(B_LFO);                                   /* LFO: CTRL */
        fa_leds();
        if (fa_page != 0) { printf("LFO -> CTRL (%d)\n", fa_page); return 1; }
        fpress(B_SCL);
        fpress(B_HOME);                                  /* moogi: HOME is FILT (was CTRL) */
        if (fa_page != FA_HOME_PAGE) { printf("HOME -> FILT (%d)\n", fa_page); return 1; }
    }
    {   /* 1.8.0: the battery icon bottom right, four bars, red on the last one, a bolt on USB power; no "n/7" any more */
        static const struct { int32_t raw, usb, bars, red, bolt; } B[6] = {
            {0, 0, 4, 0, 0}, {600, 0, 4, 0, 0}, {570, 0, 3, 0, 0}, {540, 0, 2, 0, 0}, {500, 0, 1, 1, 0}, {500, 1, 1, 1, 1}};
        uint32_t k, bad = 0;
        for (k = 0; k < 6u; k++) {
            uint32_t b, red = 0, bolt = 0, lit = 0, i;
            song.batt_raw = B[k].raw;
            usb.config = (uint8_t)B[k].usb;
            usb.suspended = 0;
            fa_page = 3;
            ui.force = 1;
            fa_draw();
            for (b = 0; b < 4u; b++) {                       /* the bar's centre pixel: 4 bars of 4 x 6 px from x 203, y 229..234 */
                uint16_t c = swap16(host_screen[231u * 240u + 203u + 5u * b + 1u]);
                lit += c != swap16(FA_RGB(0x0E, 0x0D, 0x0C)) && c != swap16(FA_RGB(0x1E, 0x1B, 0x19)) && (c >> 11) > 12u;
            }
            for (i = 0; i < 25u * 10u; i++) {
                uint16_t c = host_screen[(227u + i / 25u) * 240u + 201u + i % 25u];
                red += c == swap16(FA_RED);
                bolt += c == swap16(FA_OR);
            }
            if ((int32_t)lit != B[k].bars - (B[k].bolt ? 0 : 0) && !B[k].bolt) { printf("battery raw %d: %u bars lit, wanted %d\n", B[k].raw, lit, B[k].bars); bad++; }
            if ((red > 0u) != (B[k].red != 0)) { printf("battery raw %d: red %u, wanted %d\n", B[k].raw, red, B[k].red); bad++; }
            if ((bolt > 0u) != (B[k].bolt != 0)) { printf("battery raw %d usb %d: bolt pixels %u, wanted %d\n", B[k].raw, B[k].usb, bolt, B[k].bolt); bad++; }
        }
        usb.config = 0;
        song.batt_raw = 0;
        if (bad) return 1;
    }
    printf("fmini_app_test: ok\n");
    {   /* the CUTOFF / EMPH / CNTR knobs glide (moogi: 25 ms, was 200 ms): a click is a ramp of small steps, silence snaps */
        track_t *t0 = &trk[0];
        int32_t last, d, n, kk, big;
        uint32_t vi;
        static const uint8_t KN3[3] = {P_E0, P_E1, P_E2};
        for (kk = 0; kk < 3; kk++) {
            int32_t at275 = 0;
            for (vi = 0; vi < NVOICE; vi++)             /* (the patches played before may be polyphonic: their released voices are silent) */
                t0->v[vi].active = 0;
            t0->p[KN3[kk]] = 64;
            fmini_block(t0);
            if (fmini_sm[0][kk] != 64 << 16) { printf("knob glide: silent block does not snap\n"); return 1; }
            t0->v[0].active = 1;
            fmini_block(t0);                        /* the first block of a note follows the knobs at once */
            t0->p[KN3[kk]] = 65;
            last = fmini_sm[0][kk];
            for (n = 0, big = 0; fmini_sm[0][kk] != 65 << 16 && n < 20000; ) {
                fmini_block(t0);
                n++;
                d = fmini_sm[0][kk] - last;
                if (d <= 0 || fmini_sm[0][kk] > 65 << 16) { printf("knob glide: not monotonic\n"); return 1; }
                if (d > 2048) big++;                     /* no step above 1/32 of a byte (a 0.73 ms block) */
                last = fmini_sm[0][kk];
                if (n == 34) at275 = fmini_sm[0][kk] - (64 << 16);
            }
            /* one time constant (34 blocks = 25 ms) = 63 % of the click */
            if (n >= 20000 || big || at275 < 65536 * 55 / 100 || at275 > 65536 * 72 / 100) {
                printf("knob glide %d: steps %d big %d at 25 ms %d/65536\n", (int)kk, (int)n, (int)big, (int)at275);
                return 1;
            }
            t0->v[0].active = 0;
            fmini_block(t0);
        }
    }
    {   /* moogi: the fine knobs. A slow detent moves CUTOFF / EMPH / CNTR a quarter step: the stored byte is the rounded
         * value, the engine aims at the quarter; a fast turn moves up to 16 bytes a detent; a preset or a CC (any other writer
         * of the byte) overrides the fine position at once */
        track_t *t0 = &trk[0];
        static const uint8_t KN3[3] = {P_E0, P_E1, P_E2};
        int32_t kk, k, slot = -1, w;
        uint32_t pg, i;
        for (pg = 0; pg < 7u && slot < 0; pg++)              /* the FILTER page: the field of CUTOFF */
            for (i = 0; i < 4u; i++)
                if (fmg_field(pg, i)->kind == FK_PAR && fmg_field(pg, i)->a == P_E0) { slot = (int32_t)(pg * 4u + i); break; }
        if (slot < 0) { printf("fine knob: no CUTOFF field\n"); return 1; }
        for (kk = 0; kk < 3; kk++) {
            uint32_t fp = 0, fi = 0;
            for (pg = 0; pg < 7u; pg++)
                for (i = 0; i < 4u; i++)
                    if (fmg_field(pg, i)->kind == FK_PAR && fmg_field(pg, i)->a == KN3[kk]) { fp = pg; fi = i; }
            t0->p[KN3[kk]] = 64;
            fmini_fine[0][kk] = 0;
            for (i = 0; i < 16u; i++)
                fmg_tl[i] = 0;
            fm1_ms = 100000;
            fmg_turn(fp, fi, 1, EN_K1);                       /* a new turn: one quarter */
            w = fmini_want(t0, 0, (uint32_t)kk, KN3[kk]);
            if (w != (64 << 16) + 16384 || t0->p[KN3[kk]] != 64) {
                printf("fine knob %d: one detent -> %d (byte %d)\n", (int)kk, (int)w, (int)t0->p[KN3[kk]]); return 1;
            }
            for (k = 0; k < 3; k++) {                         /* slow detents (200 ms apart): a quarter each */
                fm1_ms += 140;
                fmg_turn(fp, fi, 1, EN_K1);
            }
            w = fmini_want(t0, 0, (uint32_t)kk, KN3[kk]);
            if (w != (65 << 16) || t0->p[KN3[kk]] != 65) {
                printf("fine knob %d: four slow detents -> %d (byte %d)\n", (int)kk, (int)w, (int)t0->p[KN3[kk]]); return 1;
            }
            for (k = 0; k < 10; k++) {                        /* a flick: 5 ms a detent, up to 16 bytes each */
                fm1_ms += 5;
                fmg_turn(fp, fi, 1, EN_K1);
            }
            if (t0->p[KN3[kk]] != 127) { printf("fine knob %d: a flick reached %d\n", (int)kk, (int)t0->p[KN3[kk]]); return 1; }
            fm1_ms += 1000;
            fmg_turn(fp, fi, -1, EN_K1);                      /* back: a quarter below the top */
            if (fmini_want(t0, 0, (uint32_t)kk, KN3[kk]) != (127 << 16) - 16384 || t0->p[KN3[kk]] != 127) {
                printf("fine knob %d: one detent back from the top\n", (int)kk); return 1;
            }
            t0->p[KN3[kk]] = 30;                              /* a preset / CC writes the byte: it wins */
            if (fmini_want(t0, 0, (uint32_t)kk, KN3[kk]) != 30 << 16) { printf("fine knob %d: the byte does not win\n", (int)kk); return 1; }
            t0->p[KN3[kk]] = 64;
            fmini_fine[0][kk] = 0;
        }
        printf("fmini fine knobs: ok\n");
    }
    {   /* moogi 1.8.0-m3: the contours' knobs 1 a detent on a slow turn, up to 16 on a flick; MONO is LAST (the newest key
         * wins, a released one falls back to the key held); HOME is the FILT page */
        track_t *t0 = &trk[0];
        static const uint8_t EV[6] = {P_E3, P_E4, P_E5, P_ATK, P_DEC, P_SUS};
        uint32_t pg, i, j, k;
        for (j = 0; j < 6u; j++) {
            uint32_t fp = 99, fi = 0;
            for (pg = 0; pg < 7u && fp == 99; pg++)
                for (i = 0; i < 4u; i++)
                    if (fmg_field(pg, i)->kind == FK_PAR && fmg_field(pg, i)->a == EV[j]) { fp = pg; fi = i; break; }
            if (fp == 99) { printf("env knob %u: no field\n", j); return 1; }
            t0->p[EV[j]] = 20;
            for (i = 0; i < 16u; i++)
                fmg_tl[i] = 0;
            fm1_ms = 300000;
            fmg_turn(fp, fi, 1, EN_K2);
            for (k = 0; k < 3u; k++) {
                fm1_ms += 120;
                fmg_turn(fp, fi, 1, EN_K2);
            }
            if (t0->p[EV[j]] != 24) { printf("env knob %u: four slow detents -> %d (want 24)\n", j, (int)t0->p[EV[j]]); return 1; }
            for (k = 0; k < 4u; k++) {
                fm1_ms += 5;
                fmg_turn(fp, fi, 1, EN_K2);
            }
            if (t0->p[EV[j]] != 24 + 64) { printf("env knob %u: a flick -> %d (want 88)\n", j, (int)t0->p[EV[j]]); return 1; }
        }
        fa_mono();
        if (t0->p[P_PRIO] != 0) { printf("mono: priority %d, want LAST\n", (int)t0->p[P_PRIO]); return 1; }
        t0->p[P_VOICE] = V_MONO;
        trk_all_off(t0);
        for (i = 0; i < NVOICE; i++)
            t0->v[i].active = 0;
        t0->nmono = 0;
        t0->mono_note = 0;
        trk_note_on(t0, 60, 100);
        trk_note_on(t0, 55, 100);                           /* a lower key while 60 is held: the newest wins */
        if (t0->mono_note != 55) { printf("mono LAST: 60 then 55 sounds %u\n", t0->mono_note); return 1; }
        trk_note_on(t0, 67, 100);
        if (t0->mono_note != 67) { printf("mono LAST: then 67 sounds %u\n", t0->mono_note); return 1; }
        trk_note_off(t0, 67);                               /* back to the newest key still held */
        if (t0->mono_note != 55 || !t0->v[0].gate) { printf("mono LAST: 67 off sounds %u\n", t0->mono_note); return 1; }
        trk_note_off(t0, 55);
        if (t0->mono_note != 60 || !t0->v[0].gate) { printf("mono LAST: 55 off sounds %u\n", t0->mono_note); return 1; }
        trk_note_off(t0, 60);
        if (t0->v[0].gate) { printf("mono LAST: all off, still gated\n"); return 1; }
        fa_page = 0;
        fa_home();
        if (fa_page != FA_HOME_PAGE || FA_HOME_PAGE != 3u || strcmp(FA_TAB[fa_page], "FILT")) { printf("HOME: page %u\n", fa_page); return 1; }
        printf("moogi m3 (contour knobs, mono LAST, HOME = FILT): ok\n");
    }
    {   /* moogi m5: LFO held: MONO <-> POLY on CTRL with VOICES lit; a tap opens CTRL and changes nothing */
        track_t *t0 = &trk[0];
        t0->p[P_VOICE] = V_MONO;
        fa_home();
        fpress(B_LFO);
        if (fa_page != 0u || t0->p[P_VOICE] != V_MONO) { printf("LFO tap: page %u voice %d\n", fa_page, t0->p[P_VOICE]); return 1; }
        fa_home();
        fhold(B_LFO);
        if (fa_page != 0u || t0->p[P_VOICE] != V_POLY || fmg_cur[0] != 8u) { printf("LFO hold: page %u voice %d cur %u\n", fa_page, t0->p[P_VOICE], fmg_cur[0]); return 1; }
        fa_mono();
        if (t0->p[P_VOICE] != V_POLY) { printf("LFO hold: POLY not kept\n"); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 44);
        fhold(B_LFO);
        if (t0->p[P_VOICE] != V_MONO) { printf("LFO hold again: voice %d\n", t0->p[P_VOICE]); return 1; }
        {   /* held long: one flip, not one per half second */
            uint32_t k;
            fm1_in.buttons |= 1u << panel.btn[B_LFO]; host_pressed |= 1u << panel.btn[B_LFO];
            for (k = 0; k < 200u; k++) fframe();
            fm1_in.buttons &= ~(1u << panel.btn[B_LFO]); fframe();
            if (t0->p[P_VOICE] != V_POLY) { printf("LFO long hold: voice %d\n", t0->p[P_VOICE]); return 1; }
        }
        t0->p[P_VOICE] = V_MONO;
        trk_all_off(t0);
        fa_home();
        printf("moogi m5 (LFO held: MONO / POLY): ok\n");
    }
    {   /* moogi m5: the norns' clock over USB: it takes the tempo by itself; PLAY waits for its bar; its START marks the bar,
         * its STOP stops nothing; half a second without it, the FM-1's tempo again; playing when it comes in: on at its bar */
        const double pq = 60000.0 / 120.0 / 24.0;          /* 120 BPM: a pulse every 20.8 ms */
        uint32_t at;
        ck_t = (double)fm1_ms;
        ck_next_pulse = ck_t;
        if (song.playing) { transport_req = 2u; ck_run(20.0, pq, 0); }
        song.g[G_CLOCK] = 0;
        song.g[G_BPM] = 90;
        ck_run(600.0, pq, 0);
        if (song.g[G_CLOCK]) { printf("norns clock: USB with no pulses\n"); return 1; }
        ck_run(400.0, pq, 1);
        if (song.g[G_CLOCK] != 1 || song.g[G_BPM] != 120 || song.playing) { printf("norns clock: clock %d bpm %d playing %d\n", song.g[G_CLOCK], song.g[G_BPM], song.playing); return 1; }
        ck_button(B_PLAY, 1); ck_run(300.0, pq, 1);
        if (!fa_pl_show()) { printf("norns clock: tempo screen\n"); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 45);
        host_enc[panel.enc[EN_SELECT]] += 3 * panel.dir[EN_SELECT]; ck_run(50.0, pq, 1);
        if (song.g[G_BPM] != 120) { printf("norns clock: SELECT turned the tempo %d\n", song.g[G_BPM]); return 1; }
        ck_button(B_PLAY, 0); ck_run(20.0, pq, 1);
        if (song.playing || ck_wait || transport_req) { printf("norns clock: the tempo hold started it\n"); return 1; }
        midi_enqueue(0xFu | 0xFAu << 8, 1u);               /* the norns' START: the next pulse is the bar's first */
        ck_sent = 0;
        ck_run(30.0 * pq, pq, 1);                          /* into the bar */
        if (song.playing) { printf("norns clock: START started it\n"); return 1; }
        ck_button(B_PLAY, 1); ck_run(20.0, pq, 1);
        ck_button(B_PLAY, 0); ck_run(20.0, pq, 1);
        if (!ck_wait || song.playing) { printf("norns clock: PLAY mid-bar: wait %d playing %d\n", ck_wait, song.playing); return 1; }
        fa_leds();
        at = 0;
        while (!song.playing && ck_sent < 200u) { ck_run(1.0, pq, 1); at = ck_sent; }
        if (at != 97u || ck_wait) { printf("norns clock: PLAY started with pulse %u (want 97, the next bar's first)\n", at); return 1; }
        midi_enqueue(0xFu | 0xFCu << 8, 1u);               /* the norns' STOP */
        ck_run(200.0, pq, 1);
        if (!song.playing) { printf("norns clock: the norns' STOP stopped it\n"); return 1; }
        ck_button(B_PLAY, 1); ck_run(20.0, pq, 1);
        ck_button(B_PLAY, 0); ck_run(20.0, pq, 1);
        if (song.playing || ck_wait) { printf("norns clock: PLAY did not stop it\n"); return 1; }
        ck_run(700.0, pq, 0);                              /* the norns gone: the FM-1's own tempo */
        if (song.g[G_CLOCK]) { printf("norns clock: still USB without pulses\n"); return 1; }
        transport_req = 1u; ck_run(300.0, pq, 0);
        if (!song.playing) { printf("norns clock: INT PLAY\n"); return 1; }
        ck_sent = 0;
        while (ck_sent < 300u && !(song.g[G_CLOCK] && song.playing && !ck_wait)) ck_run(1.0, pq, 1);
        if (!song.g[G_CLOCK] || !song.playing || ck_sent > 100u) { printf("norns clock: playing when it came in: clock %d playing %d after %u pulses\n", song.g[G_CLOCK], song.playing, ck_sent); return 1; }
        transport_req = 2u; ck_run(20.0, pq, 1);
        ck_run(700.0, pq, 0);
        song.g[G_BPM] = 120;
        trk_all_off(&trk[0]);
        fframes(100);
        printf("moogi m5 (the norns' clock: USB, PLAY on its bar, START a marker, STOP nothing): ok\n");
    }
    {   /* moogi m6: the drum machine on track 2 (fmini_drum.c, seq.c fa_dr_*) */
        track_t *d = &trk[FA_DR_TRK];
        uint32_t i, l, k, m, got, bar[16];
        song.playing = 0;
        transport_req = 0;
        song.g[G_CLOCK] = 0;
        fa_dr_cfg_load();                                  /* (nothing saved yet: the script's starting beat) */
        fa_dr_setup();
        fframes(100);
        /* power-on: track 2 is DRUM, heard, 16 steps of 1/16, no sends; the starting beat in its steps */
        if (d->eng_req != ENGI_DRUM || d->p[P_MUTE] || d->p[P_SLEN] != 16 || d->p[P_SDIV] != 2 || d->p[P_DLY] || d->p[P_REV]) {
            printf("drums: track 2 is not set up (engine %d mute %d len %d)\n", d->eng_req, d->p[P_MUTE], d->p[P_SLEN]); return 1; }
        for (i = 0; i < 16u; i++) {
            uint32_t want = (i % 4u == 0u) << 0 | (i == 4u || i == 12u) << 1 | (i % 2u == 0u) << 3 | (i == 0u || i == 6u || i == 11u) << 6;
            if (d->step[i].time != ST_NOTE || d->step[i].hit != want) { printf("drums: step %u hits %02x, want %02x\n", i, d->step[i].hit, want); return 1; }
        }
        /* SEQ twice: the drum mode, on FILT (the knobs the filter), the keys locked; the screen */
        fa_page = 0;
        fpress(B_SEQ); fpress(B_SEQ);
        if (!fa_dr || fa_page != 3u || !kb_lock) { printf("drums: SEQ twice: mode %d page %d lock %d\n", fa_dr, fa_page, kb_lock); return 1; }
        fframes(600);
        fpress(B_SEQ); fframes(600);                       /* (once, slowly: nothing) */
        if (!fa_dr) { printf("drums: one SEQ press left the mode\n"); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 20);
        /* a key plays no note of the bass in it */
        fm1_in.notes = 1u << 7; host_notes = 1u << 7;
        ck_t = (double)fm1_ms; ck_run(20.0, 1000.0, 0);
        fm1_in.notes = 0; ck_run(20.0, 1000.0, 0);
        for (i = 0; i < NVOICE; i++)
            if (trk[0].v[i].active && trk[0].v[i].gate) { printf("drums: a key played the bass in the drum mode\n"); return 1; }
        trk_all_off(&trk[0]);
        /* moogi m7: a black key held selects its lane (no mute when let go) and, stopped, sounds it */
        fframes(30);
        fm1_in.notes = 1u << fa_black_key(1); host_notes = fm1_in.notes;
        fframes(200);
        if (fa_dr_sel == 1u) { printf("drums: a short hold selected\n"); return 1; }
        fframes(200);
        fm1_in.notes = 0;
        fframe();
        if (fa_dr_sel != 1u || fa_dr_mute) { printf("drums: black key 2 held selects SNARE (%u, mute %x)\n", fa_dr_sel, fa_dr_mute); return 1; }
        ck_t = (double)fm1_ms; ck_run(10.0, 1000.0, 0);
        for (m = 0, i = 0; i < NVOICE; i++)
            m |= d->v[i].active;
        if (!m) { printf("drums: the black key, stopped, did not sound its lane\n"); return 1; }
        {   /* .. and it is heard: the output moves (the bass silent), at a level near the bass's own */
            int32_t o[2 * CTL], pk = 0, j, b;
            int32_t mq = (int32_t)song.master_q12;
            song.master_q12 = MASTER_FULL;                 /* (the MASTER pot: main.c's, not the test's) */
            d->p[P_LEVEL] = 0;                             /* (an old track 2's: setup puts the default back) */
            fa_dr_setup();
            fa_dr_aud = 2;
            for (b = 0; b < 60; b++) {
                mix_block(o, CTL);
                for (j = 0; j < 2 * CTL; j++)
                    pk = o[j] > pk ? o[j] : -o[j] > pk ? -o[j] : pk;
            }
            song.master_q12 = (uint32_t)mq;
            if (pk < 1000) { printf("drums: the SNARE is not heard (peak %d)\n", (int)pk); return 1; }
        }
        /* SELECT HITS, ALGORITHM ROTATE, PRESETS VOLUME of the selected lane */
        fframes(30);
        fturn(EN_SELECT, 1);
        if (fa_dr_n[1] != 3u) { printf("drums: SELECT HITS %u\n", fa_dr_n[1]); return 1; }
        fturn(EN_ALGO, -1);
        if (fa_dr_r[1] != 3u) { printf("drums: ALGORITHM ROTATE %u\n", fa_dr_r[1]); return 1; }
        for (i = 0; i < 16u; i++)
            if ((uint32_t)((d->step[i].hit >> 1) & 1u) != (uint32_t)fa_dr_eu(3, 3, 16, i)) { printf("drums: SNARE 3/3 not in the steps\n"); return 1; }
        fturn(EN_PRESET, -2);
        if (fa_dr_lv[1] != 119u || d->p[P_LN1] != 119) { printf("drums: PRESETS VOLUME %u / %d\n", fa_dr_lv[1], d->p[P_LN1]); return 1; }
        /* a white key held: its quarter. SNARE 3/3 plays step 3 in Q1 (steps 0..3): HITS up locks it at 2 from there */
        fm1_in.notes = 1u << fa_white_key(1);              /* (a key of Q1) */
        fturn(EN_SELECT, 1);
        if (fa_dr_qn[1][0] != 2 || fa_dr_r[1] != 3u || fa_dr_n[1] != 3u) { printf("drums: Q1 HITS %d\n", fa_dr_qn[1][0]); return 1; }
        fturn(EN_ALGO, 1);
        if (fa_dr_qr[1][0] != (uint8_t)((fa_dr_qr[1][0]) & 3u)) { printf("drums: Q1 ROT range\n"); return 1; }
        for (i = 0; i < 4u; i++)
            if ((uint32_t)((d->step[i].hit >> 1) & 1u) != (uint32_t)fa_dr_eu(2, fa_dr_qr[1][0], 4, i)) { printf("drums: Q1 not in the steps\n"); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 21);            /* (the quarter held: its values at the foot) */
        fturn(EN_SELECT, -1); fturn(EN_SELECT, -1); fturn(EN_SELECT, -1);
        if (fa_dr_qn[1][0] != -1) { printf("drums: Q1 back to = (%d)\n", fa_dr_qn[1][0]); return 1; }
        fm1_in.notes = 0;
        fframe();
        /* a black key held: the wheels are its lane's (it is selected, at once), PRESETS its CHANCE; let go: no mute */
        fm1_in.notes = 1u << fa_black_key(3); host_notes = fm1_in.notes;
        fframe();
        fturn(EN_PRESET, -2);
        if (fa_dr_chance[3] != 90u || fa_dr_lv[3] != 127u || fa_dr_sel != 3u) { printf("drums: black held + PRESETS: CHANCE %u\n", fa_dr_chance[3]); return 1; }
        fturn(EN_SELECT, 1);
        if (fa_dr_n[3] != 9u) { printf("drums: black held + SELECT: its HITS %u\n", fa_dr_n[3]); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 22);
        fm1_in.notes = 0;
        fframes(40);
        if (fa_dr_mute) { printf("drums: a black key held and turned muted on release\n"); return 1; }
        fturn(EN_SELECT, -1);
        fa_dr_chance[3] = 100;
        /* playing: the beat as the steps hold it, 16 steps whatever the bass's length */
        fa_parts = 15u;                                    /* (the bass: 64 steps) */
        transport_req = 1;
        for (i = 0; i < 32u; i++) {
            got = dr_step();
            bar[d->seq_idx % 16u] = got;                   /* (what the step now playing played) */
        }
        if (d->p[P_SLEN] != 16 || trk[0].p[P_SLEN] != 64) { printf("drums: lengths %d / %d\n", d->p[P_SLEN], trk[0].p[P_SLEN]); return 1; }
        {   /* every hit of a bar sounded: the set of what played equals the set the steps hold */
            uint32_t want = 0, have = 0;
            for (i = 0; i < 16u; i++) { want += d->step[i].hit; have += bar[i]; if (bar[i] != d->step[i].hit) have += 1000u; }
            if (want != have) { printf("drums: a bar played %u, the steps hold %u\n", have, want);
                for (i = 0; i < 16u; i++) printf("  step %2u holds %02x played %02x (idx now %u, gate %d, sgate %d)\n", i, d->step[i].hit, bar[i], d->seq_idx, (int)d->seq_off, d->p[P_SGATE]);
                return 1; }
        }
        /* SEQ held in the drum mode: the bass's layer (its black key 1 is a part, not a lane) */
        k = fa_dr_sel;
        fm1_in.buttons |= 1u << panel.btn[B_SEQ];
        fframe();
        fkey(fa_black_key(5));
        fm1_in.buttons &= ~(1u << panel.btn[B_SEQ]);
        fframes(500);
        if (fa_dr_sel != k || !fa_dr) { printf("drums: SEQ held: the key went to the drums (%u)\n", fa_dr_sel); return 1; }
        /* a tap on a black key while playing: the mute waits for the next quarter, then the lane is silent; the selection
         * stays */
        k = fa_dr_sel;
        fkey(fa_black_key(0));
        if (fa_dr_sel != k) { printf("drums: a tap moved the selection\n"); return 1; }
        if (!(fa_dr_pend & 1u) || (fa_dr_mute & 1u)) { printf("drums: the mute is not waiting (%x %x)\n", fa_dr_pend, fa_dr_mute); return 1; }
        for (k = 0; k < 4u && (fa_dr_pend & 1u); k++) {
            if (d->seq_idx % 4u == 3u && (fa_dr_pend & 1u)) {
                got = dr_step();
                if (d->seq_idx % 4u != 0u || !(fa_dr_mute & 1u) || (got & 1u)) { printf("drums: the mute is not on the quarter\n"); return 1; }
                break;
            }
            got = dr_step();
            if (!(fa_dr_mute & 1u) && (d->step[d->seq_idx].hit & 1u) && !(got & 1u)) { printf("drums: muted before its quarter\n"); return 1; }
        }
        for (i = 0; i < 16u; i++)
            if (dr_step() & 1u) { printf("drums: a muted KICK played\n"); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 23);
        /* two keys tapped together: both wait for the same quarter */
        fm1_in.notes = 1u << fa_black_key(1) | 1u << fa_black_key(2); host_notes = fm1_in.notes;
        fframe(); fframe();
        fm1_in.notes = 0;
        fframe();
        if ((fa_dr_pend & 6u) != 6u) { printf("drums: two keys together: pending %x\n", fa_dr_pend); return 1; }
        while (fa_dr_pend)
            got = dr_step();
        if ((fa_dr_mute & 6u) != 6u || d->seq_idx % 4u) { printf("drums: two keys together: not on one quarter (%x)\n", fa_dr_mute); return 1; }
        fkey(fa_black_key(1)); fkey(fa_black_key(2));
        while (fa_dr_pend)
            got = dr_step();
        /* STOP: a waiting mute applies at once; stopped, a tap is at once */
        fkey(fa_black_key(0));
        fpress(B_PLAY);
        ck_t = (double)fm1_ms; ck_run(30.0, 1000.0, 0);   /* (the audio takes the stop) */
        if (song.playing || fa_dr_pend || (fa_dr_mute & 1u)) { printf("drums: STOP did not apply the mute (%x %x)\n", fa_dr_pend, fa_dr_mute); return 1; }
        fframes(30);
        fkey(fa_black_key(2));
        if (!(fa_dr_mute & 4u) || fa_dr_pend) { printf("drums: stopped, the mute is not at once\n"); return 1; }
        fa_dr_mute = 0;
        /* CHANCE 0: never; the other lanes as before */
        fa_dr_chance[3] = 0;
        transport_req = 1;
        for (m = 0, i = 0; i < 32u; i++)
            m |= dr_step();
        if ((m & 8u) || !(m & 1u)) { printf("drums: CHANCE 0 played (%x)\n", m); return 1; }
        fa_dr_chance[3] = 100;
        transport_req = 2;
        ck_t = (double)fm1_ms; ck_run(30.0, 1000.0, 0);
        fframes(100);
        /* saved and back: the beat, the quarters, VOLUME, CHANCE, the kit; a wrong byte is its default */
        fa_dr_qn[5][2] = 3; fa_dr_qr[5][2] = 2; fa_dr_qn[7][3] = 0; fa_dr_chance[6] = 35; fa_dr_kit = DK_PERC; fa_dr_n[4] = 16; fa_dr_r[4] = 15;
        fa_dr_cfg_save();
        {
            uint8_t n0[NLANE], r0[NLANE], lv0[NLANE], ch0[NLANE], qr0[NLANE][4];
            int8_t qn0[NLANE][4];
            memcpy(n0, fa_dr_n, sizeof n0); memcpy(r0, fa_dr_r, sizeof r0); memcpy(lv0, fa_dr_lv, sizeof lv0);
            for (l = 0; l < NLANE; l++) ch0[l] = fa_dr_chance[l];
            memcpy(qn0, fa_dr_qn, sizeof qn0); memcpy(qr0, fa_dr_qr, sizeof qr0);
            memset(fa_dr_n, 0, sizeof fa_dr_n); memset(fa_dr_qn, 9, sizeof fa_dr_qn); fa_dr_kit = 0;
            fa_dr_cfg_load();
            for (l = 0; l < NLANE; l++)
                if (fa_dr_n[l] != n0[l] || fa_dr_r[l] != r0[l] || fa_dr_lv[l] != lv0[l] || fa_dr_chance[l] != ch0[l] ||
                    memcmp(fa_dr_qn[l], qn0[l], 4) || memcmp(fa_dr_qr[l], qr0[l], 4)) { printf("drums: lane %u not as saved\n", l); return 1; }
            if (fa_dr_kit != DK_PERC) { printf("drums: the kit not as saved\n"); return 1; }
            *fa_dr_byte(18u + 5u * 2u) = 40;                /* CLAP's HITS out of range: its default */
            *fa_dr_byte(1) = 2;                            /* a retired kit value: STD */
            *fa_dr_byte(219) = 40;                         /* (m7's RELEASE, 0..100: m8 loads a beat) */
            fa_dr_cfg_load();
            if (fa_dr_n[2] != FA_DR_DEF[2][0] || fa_dr_kit != DK_STD || fa_dr_n[4] != 16u || fa_sc_rel != 4u) { printf("drums: a wrong byte not its default\n"); return 1; }
            *fa_dr_byte(0) = 0;                            /* nothing saved: the starting beat */
            fa_dr_cfg_load();
            if (fa_dr_n[0] != 4u || fa_dr_r[1] != 4u || fa_dr_qn[5][2] != -1 || fa_dr_chance[6] != 100u) { printf("drums: no record: not the start\n"); return 1; }
        }
        fa_dr_setup();
        /* a change is saved a moment later (fa_cfg_save: the settings) */
        fa_dr_dirty = 0;
        fturn(EN_SELECT, 1);
        if (!fa_dr_dirty) { printf("drums: a change is not marked\n"); return 1; }
        fframes(2100);
        if (fa_dr_dirty || *fa_dr_byte(0) != 0xD7u) { printf("drums: not saved a moment after\n"); return 1; }
        /* the screen: the step playing does not redraw it; HITS does */
        {
            uint32_t h0 = fa_dr_sig(0), h1;
            fa_dr_n[2] = 5;
            h1 = fa_dr_sig(0);
            fa_dr_n[2] = 0;
            if (h0 == h1) { printf("drums: the screen does not see HITS\n"); return 1; }
        }
        fa_gl = 1; fa_gl_row = 6; fa_pp_until = 0; ui.force = 1; fa_draw(); dump(dir, 24); fa_gl = 0;   /* (GLO > DRUMS) */
        {   /* moogi m7: the sidechain: a kick heard ducks the synth by AMOUNT and it comes back over RELEASE; muted, OFF: not */
            track_t *t0 = &trk[0];
            static int32_t pk[1400], gb[1400];
            int32_t o[2 * CTL], j, b, b0 = -1, pre = 0, post = 0, mq = (int32_t)song.master_q12, sus = t0->p[P_SUS];
            song.master_q12 = MASTER_FULL;
            t0->p[P_SUS] = 127; t0->p[P_ATK] = 0;
            esync_coef(t0);
            fa_sc_amt = 100; fa_sc_rel = 4;                /* (1/4: a beat, 120 BPM: 689 blocks) */
            fa_sc_on = 0; fa_sc_dip = 0; fa_sc_g0 = fa_sc_g1 = 4096;   /* (a pump the tests above left frozen: no audio ran) */
            fa_dr_mute = 0;
            d->p[P_MUTE] = 1;                              /* (the drums unheard here: the kick still ducks) */
            trk_note_on(t0, 45, 100);
            for (b = 0; b < 1400; b++) {
                if (b == 100)
                    transport_req = 1;
                mix_block(o, CTL);
                for (pk[b] = 0, j = 0; j < 2 * CTL; j++)
                    pk[b] = o[j] > pk[b] ? o[j] : -o[j] > pk[b] ? -o[j] : pk[b];
                gb[b] = fa_sc_g1;
                if (b0 < 0 && gb[b] < 4096)
                    b0 = b;
            }
            for (b = 60; b < 100; b++) pre = pk[b] > pre ? pk[b] : pre;
            for (b = b0 + 4; b < b0 + 160; b++) post = pk[b] > post ? pk[b] : post;   /* (3 .. 115 ms after: held down) */
            if (b0 < 100 || b0 > 104 || pre < 1000 || post * 50 > pre) { printf("sidechain: kick at block %d, before %d, held %d\n", (int)b0, (int)pre, (int)post); return 1; }
            if (gb[b0 + 160] > 0 || gb[b0 + 400] > 1500 || gb[b0 + 400] < 200 || gb[b0 + 680] < 3500) {
                printf("sidechain: the pump (1/4): %d at 116 ms, %d at 290 ms, %d at 494 ms\n", (int)gb[b0 + 160], (int)gb[b0 + 400], (int)gb[b0 + 680]); return 1; }
            for (b = b0 + 4; b < b0 + 680; b++)
                if (gb[b] < gb[b - 1]) { printf("sidechain: the synth dips again on its way back (block %d)\n", (int)(b - b0)); return 1; }
            for (b = b0 + 300; b < b0 + 700; b++)
                if (gb[b] < 4096 && gb[b - 1] == 4096) break;
            if (b >= b0 + 700 || b < b0 + 670) { printf("sidechain: the next kick (block %d)\n", (int)(b - b0)); return 1; }
            fa_dr_mute = 1;                                /* a muted kick: no duck */
            for (b = 0; b < 1400; b++) { mix_block(o, CTL); if (fa_sc_g1 < 4000 && b > 200) break; }
            if (b < 1400) { printf("sidechain: a muted kick ducked\n"); return 1; }
            fa_dr_mute = 0; fa_sc_amt = 0;                 /* OFF */
            for (b = 0; b < 1400; b++) { mix_block(o, CTL); if (fa_sc_g1 != 4096) break; }
            if (b < 1400) { printf("sidechain: OFF ducked\n"); return 1; }
            transport_req = 2;
            mix_block(o, CTL);
            trk_all_off(t0);
            t0->p[P_SUS] = (int16_t)sus;
            esync_coef(t0);
            d->p[P_MUTE] = 0;
            song.master_q12 = (uint32_t)mq;
            fa_sc_rel = 4;
        }
        /* REC does nothing in the drum mode */
        fpress(B_REC); fframes(30);
        if (fa_sq_rec) { printf("drums: REC armed in the drum mode\n"); return 1; }
        /* HOME leaves (the drums still there); SEQ twice again, then a page button leaves too */
        fpress(B_HOME); fframes(30);
        if (fa_dr || kb_lock || d->eng_req != ENGI_DRUM) { printf("drums: HOME did not leave\n"); return 1; }
        fpress(B_SEQ); fpress(B_SEQ);
        if (!fa_dr) { printf("drums: SEQ twice again\n"); return 1; }
        fpress(B_FX); fframes(30);
        if (fa_dr || fa_page != 6u) { printf("drums: FX did not leave (%d %d)\n", fa_dr, fa_page); return 1; }
        fa_page = 3;
        fmg_fx = 0;
        fa_parts = 1u;
        fframes(100);
        printf("moogi m6, m7 (the drum machine: track 2, the mode, the wheels, quarters, mutes on a tap and the quarter, hold to select, chance, saved; the sidechain): ok\n");
    }
    {   /* the loudness contour is a Minimoog's ADS: the DECAY knob is the decay and the release; SUSTAIN clamps it */
        track_t *t0 = &trk[0];
        voice_t vv;
        int32_t rel[2], dec_ticks[2], dq, rr;
        for (dq = 0; dq < 2; dq++) {
            for (rr = 0; rr < 2; rr++) {
                int n = 0, nd = 0;
                memset(&vv, 0, sizeof vv);
                t0->p[P_ATK] = 40;
                t0->p[P_DEC] = dq ? 90 : 60;
                t0->p[P_SUS] = 64;
                t0->p[P_REL] = rr ? 127 : 0;                 /* no longer matters */
                esync_coef(t0);
                vv.stage = 1;
                while (vv.stage == 1 && n++ < 100000) env_tick(t0, &vv);
                if (vv.stage != 2) { printf("adsr: attack never ends\n"); return 1; }
                while (vv.env > ((int32_t)t0->p[P_SUS] << 17) + (1 << 18) && nd++ < 100000) env_tick(t0, &vv);
                if (nd >= 100000) { printf("adsr: decay does not land on the sustain (%d)\n", (int)vv.env); return 1; }
                { int hold; for (hold = 0; hold < 50; hold++) env_tick(t0, &vv);
                  if (vv.env < ((int32_t)t0->p[P_SUS] << 17) || vv.env > ((int32_t)t0->p[P_SUS] << 17) + (1 << 18)) { printf("adsr: sustain drifts\n"); return 1; } }
                vv.stage = 3;
                n = 0;
                vv.active = 1;
                while (vv.active && n < 200000) { env_tick(t0, &vv); n++; }
                if (rr == 0) { rel[dq] = n; dec_ticks[dq] = nd; }
                else if (n != rel[dq]) { printf("adsr: the REL param still changes the release (%d vs %d)\n", n, (int)rel[dq]); return 1; }
            }
        }
        if (!(rel[1] > rel[0] * 2) || !(dec_ticks[1] > dec_ticks[0] * 2)) {
            printf("adsr: DECAY must set both: release %d/%d decay %d/%d\n", (int)rel[0], (int)rel[1], (int)dec_ticks[0], (int)dec_ticks[1]);
            return 1;
        }
    }
    {   /* both contours reach 12 s at the top of ATTACK and DECAY (the loudness one here; the filter's uses the same tables) */
        track_t *t0 = &trk[0];
        voice_t vv;
        int32_t n = 0, nd = 0;
        double sec;
        memset(&vv, 0, sizeof vv);
        t0->p[P_ATK] = 127; t0->p[P_DEC] = 127; t0->p[P_SUS] = 0; t0->p[P_REL] = 0;
        esync_coef(t0);
        vv.stage = 1;
        while (vv.stage == 1 && n < 2000000) { env_tick(t0, &vv); n++; }
        sec = (double)n * CTL / FS;
        if (sec < 11.0 || sec > 13.0) { printf("attack at max: %.2f s\n", sec); return 1; }
        while (vv.env > (1 << 20) && nd < 4000000) { env_tick(t0, &vv); nd++; }   /* -24 dB: heard until then */
        sec = (double)nd * CTL / FS;
        if (sec < 11.0 || sec > 13.5) { printf("decay at max: %.2f s\n", sec); return 1; }
        vv.stage = 3; vv.env = 1 << 24; vv.active = 1; nd = 0;
        while (vv.env > (1 << 20) && nd < 4000000) { env_tick(t0, &vv); nd++; }
        sec = (double)nd * CTL / FS;
        if (sec < 11.0 || sec > 13.5) { printf("release at max DECAY: %.2f s\n", sec); return 1; }
    }
    {   /* the filter contour: 12 s at the top of ATTACK, and DECAY is its decay and release */
        track_t *t0 = &trk[0];
        voice_t vv;
        int32_t n = 0, e = 0;
        double sec;
        memset(&vv, 0, sizeof vv);
        t0->p[P_E3] = 127; t0->p[P_E4] = 127; t0->p[P_E5] = 0;
        vv.gate = 1;
        vv.s[7] = 1 << 24;                                   /* stage 1 from 0 */
        do { e = fmini_contour(t0->p, &vv); n++; } while (((uint32_t)vv.s[7] >> 24) == 1u && n < 2000000);
        sec = (double)n * CTL / FS;
        if (sec < 11.0 || sec > 13.0) { printf("filter attack at max: %.2f s\n", sec); return 1; }
        vv.gate = 0;
        n = 0;
        do { e = fmini_contour(t0->p, &vv); n++; } while (e > 2048 && n < 4000000);   /* -24 dB (Q15) */
        sec = (double)n * CTL / FS;
        if (((uint32_t)vv.s[7] >> 24) != 3u || sec < 11.0 || sec > 14.5) { printf("filter release at max DECAY: %.2f s (st %u)\n", sec, (unsigned)((uint32_t)vv.s[7] >> 24)); return 1; }
    }
    {   /* the DECAY time is the drop to the SUSTAIN level, whatever it is: the same ~12 s at the top for sustain 0 and 80 % */
        track_t *t0 = &trk[0];
        voice_t vv;
        int32_t sus, nd[2], k;
        for (k = 0; k < 2; k++) {
            int32_t n = 0;
            memset(&vv, 0, sizeof vv);
            t0->p[P_ATK] = 0; t0->p[P_DEC] = 127; t0->p[P_SUS] = k ? 100 : 0; t0->p[P_REL] = 0;
            esync_coef(t0);
            sus = (int32_t)t0->p[P_SUS] << 17;
            vv.stage = 1;
            while (vv.stage == 1) env_tick(t0, &vv);
            while (vv.env - sus > (((1 << 24) - sus) / 16) && n < 4000000) { env_tick(t0, &vv); n++; }
            nd[k] = n;
            if ((double)n * CTL / FS < 11.0 || (double)n * CTL / FS > 13.5) { printf("decay to sustain %d: %.2f s\n", (int)t0->p[P_SUS], (double)n * CTL / FS); return 1; }
        }
    }
    return 0;
}
