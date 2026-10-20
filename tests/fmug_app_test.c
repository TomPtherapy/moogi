/* SPDX-License-Identifier: GPL-3.0-only
 * FMini-1 screen: renders every page of fmug_app.c with the real drawing code to PPMs (OUT/fa_<page>.ppm) and
 * checks that the screen draws and that SELECT / ALGORITHM / PRESETS reach every control. */
#define UI_TEST_NO_MAIN 1
#define FELUCCA_FMUG_ONLY 1
#include "ui_test.c"
#include "../firmware/src/fmug_app.c"
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
            for (i = 0; i < FG_SIZE; i++) before = before * 31u + fmug_patch[0][i];
            for (i = 0; i < P_COUNT; i++) before = before * 31u + (uint32_t)TSEL->p[i];
            fmg_force_page = pg2;
            fmg_knob((uint32_t)kn, 3);
            for (i = 0; i < FG_SIZE; i++) after = after * 31u + fmug_patch[0][i];
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
    fmug_load_slot(0, 99);
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
        fmug_poll();
        fa_sv_open();
        if (!fa_sv || fa_sv_slot != 0 || before != 0) { printf("save: open\n"); return 1; }
        str_cpy(fa_sv_name, "MY PATCH", 13);
        fa_sv_do();
        if (fa_sv || fa_nuser() != 1 || !fa_on_user() || TSEL->user != 1) { printf("save: not stored\n"); return 1; }
        up_name(0, nm);
        if (!str_eq(nm, "MY PATCH")) { printf("save: name %s\n", nm); return 1; }
        apply_preset_to(TSEL, 7);
        TSEL->user = 0;
        TSEL->p[P_E7] = (int16_t)(FMUG_NFACTORY - 1u);
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
            if (FMUG_NFACTORY != 100u || strlen(FMUG_CATS) != 100u || NELEM(FMUG_PRESETS) != 100u) { printf("bank size %u\n", (uint32_t)FMUG_NFACTORY); return 1; }
            for (k = 0; k < FMUG_NFACTORY; k++) {
                apply_preset_to(TSEL, k);
                fa_mono();
                if (TSEL->p[P_VOICE] != (ENGINES[ENGI_FMUG]->presets[k].mono ? V_MONO : V_POLY)) { printf("preset %u: voice mode %d\n", k, TSEL->p[P_VOICE]); return 1; }
                if (strlen(ENGINES[ENGI_FMUG]->presets[k].name) > 12u) { printf("preset %u: name too long\n", k); return 1; }
                npoly += !ENGINES[ENGI_FMUG]->presets[k].mono;
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
         * a factory patch brings its own (the FMUG_X table) */
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
        if (fa_on_user() || TSEL->p[P_E7] != 5 || song.g[G_RSIZE] != FMUG_X[5].fx[1] || song.g[G_DRLVL] != FMUG_X[5].fx[10] ||
            song.g[G_RTYPE] != FMUG_X[5].fx[0] || song.g[G_DTIME] != FMUG_X[5].fx[3] || song.octave != FMUG_X[5].oct) {
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
        for (k = 0; k < FMUG_NFACTORY; k++) {
            const fmug_x_t *x = &FMUG_X[k];
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
            if ((k == 0u || k == 2u || k == 10u) && (fmug_patch[0][FG_SW1] & FS_MOSC)) { printf("patch %u: MOD OSC is off\n", k); bad++; }
            if (k == 0u && (song.octave != 1 || TSEL->p[P_E0] != 83 || fmug_patch[0][FG_GLID] != 50)) {      /* CUTOFF 6.5 (+10 %), GLIDE 3.9 (-10 %), OCT+ */
                printf("patch 0: octave %d cutoff %d glide %d\n", song.octave, TSEL->p[P_E0], fmug_patch[0][FG_GLID]); bad++;
            }
            if (k == 3u && (TSEL->p[P_E0] != 105 || TSEL->p[P_REV] != 64 || song.g[G_RTYPE] != 1)) {          /* CUTOFF 8.2 (-0.2), REVERB 5.0, HALL */
                printf("patch 3: cutoff %d reverb %d type %d\n", TSEL->p[P_E0], TSEL->p[P_REV], song.g[G_RTYPE]); bad++;
            }
        }
        if (on < 22u || on > 28u) { printf("bank: ARP on in %u of 100 patches\n", on); bad++; }
        if (song.g[G_BPM] != 117) { printf("bank: a patch changed the tempo (%d)\n", song.g[G_BPM]); bad++; }
        if (bad) return 1;
        song.g[G_BPM] = bpm0;                            /* (a Program Change brings the same: ui.c midi_pc_load) */
        ui.mpc[0] = (uint8_t)(0x80u | 14u);              /* 14 RAVE ARP */
        midi_pc_poll();
        if (TSEL->p[P_E7] != 14 || TSEL->p[P_AMODE] != FMUG_X[14].arp[1] || song.g[G_RSIZE] != FMUG_X[14].fx[1] || song.octave != 0) {
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
    {   /* a new preset always goes HOME (CTRL), from any page, with the pop-ups closed: PRESETS, a Program Change, a slot change */
        uint32_t pgi;
        panel_init();
        fa_init();
        fa_gl = 0; fa_ar = 0; fa_sv = 0; fa_sq = 0;
        fframes(160);
        for (pgi = 1; pgi < 7u; pgi++) {
            fa_page = (uint8_t)pgi;
            fmg_force_page = (int8_t)pgi;
            fa_ar = pgi == 3u;                           /* (the ARP pop-up open on one of them) */
            fturn(EN_PRESET, 1);
            if (fa_page != 0 || fa_ar) { printf("HOME: PRESETS on page %u -> page %d, arp pop-up %d\n", pgi, fa_page, fa_ar); return 1; }
        }
        fa_page = 4;
        fmg_force_page = 4;
        ui.mpc[0] = (uint8_t)(0x80u | 33u);
        fframes(48);
        if (TSEL->p[P_E7] != 33 || fa_page != 0) { printf("HOME: Program Change -> slot %d page %d\n", TSEL->p[P_E7], fa_page); return 1; }
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
        TSEL->eng_req = ENGI_FMUG;
        song.octave = 0;
        fa_init();
        if (song.octave != FMUG_X[0].oct || song.octave != 1) { printf("power-on: patch 00 octave %d\n", song.octave); return 1; }
        TSEL->p[P_E7] = 35;
        fa_init();
        if (song.octave != FMUG_X[35].oct) { printf("power-on: patch 35 octave %d\n", song.octave); return 1; }
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
            fmug_poll();
            fa_page = CASE[c][0];
            fmg_force_page = (int8_t)fa_page;
            fframes(48);
            fturn(EN_K1 + CASE[c][1], 3);
            fframes(48);
            if (TSEL->p[P_E7] != (int16_t)FMUG_OWN) { printf("edit %u: the slot is %d, not OWN\n", c, TSEL->p[P_E7]); return 1; }
            fturn(EN_PRESET, 1);
            fframes(48);
            if (TSEL->p[P_E7] != 14 || fa_page != 0) { printf("edit %u: PRESETS from 13 -> %d (page %d), not 14\n", c, TSEL->p[P_E7], fa_page); return 1; }
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
        fmug_poll();
    }
    {   /* GLIDE: the knob alone makes the track glide; ARP pop-up changes the arp; HOME */
        int i;
        fa_page = 0;
        fmg_force_page = 0;
        fmug_load_slot(0, 0);
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
        fmug_load_slot(0, 0);
        for (n = 0; n < FA_NCC; n++) {                  /* a CC of each control moves it */
            uint32_t h0 = 0, h1 = 0, i;
            fa_ctl_apply(n, 0);
            for (i = 0; i < FG_SIZE; i++) h0 = h0 * 31u + fmug_patch[0][i];
            for (i = 0; i < P_COUNT; i++) h0 = h0 * 31u + (uint32_t)TSEL->p[i];
            for (i = 0; i < G_COUNT; i++) h0 = h0 * 31u + (uint32_t)song.g[i];
            fa_ctl_apply(n, 127);
            for (i = 0; i < FG_SIZE; i++) h1 = h1 * 31u + fmug_patch[0][i];
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
        fmug_poll();
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
        for (k = 0; k < FMUG_NFACTORY; k++) {            /* every number 00..99 */
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
        /* the sequencer */
        fpress(B_SEQ);
        if (!fa_sq) { printf("seq pop-up\n"); return 1; }
        fpress(B_REC);
        if (!fa_sq_rec) { printf("rec\n"); return 1; }
        input_on(&trk[0], 60, 100); input_on(&trk[0], 64, 100); input_on(&trk[0], 67, 100);
        trk_all_off(&trk[0]);
        if (fa_sq_cur != 3 || trk[0].step[0].note[0] != 60 || trk[0].step[2].note[0] != 67 || trk[0].p[P_SLEN] != 3) { printf("seq record %d\n", fa_sq_cur); return 1; }
        fpress(B_REC);
        if (fa_sq_rec) { printf("rec off\n"); return 1; }
        fpress(B_PLAY);
        if (transport_req != 1) { printf("play request\n"); return 1; }
        for (i = 0; i < 400; i++) { events_block(CTL); mix_block((int32_t[2 * CTL]){0}, CTL); }
        if (!song.playing) { printf("not playing\n"); return 1; }
        {   uint32_t seen = 0, lit = 0;
            for (i = 0; i < 1200; i++) {
                int32_t o[2 * CTL];
                mix_block(o, CTL);
                seen |= 1u << (trk[0].seq_idx % 3u);
                lit |= fa_sound_keys();
            }
            if (seen != 7u) { printf("seq steps seen %u\n", seen); return 1; }
            if (!lit) { printf("no key lit while playing\n"); return 1; }
        }
        ui.force = 1; fa_draw(); dump(dir, 18);
        fpress(B_PLAY);
        events_block(CTL);
        if (song.playing) { printf("stop\n"); return 1; }
        fpress(B_REC);                                   /* a take of 16 ends by itself */
        for (k = 0; k < 20u; k++) input_on(&trk[0], 50u + k, 100);
        trk_all_off(&trk[0]);
        if (fa_sq_rec || trk[0].p[P_SLEN] != 16 || trk[0].step[15].note[0] != 65) { printf("take of 16 (%d)\n", trk[0].p[P_SLEN]); return 1; }
        ui.force = 1; fa_draw(); dump(dir, 19);
        fpress(B_SEQ);
        if (fa_sq) { printf("seq close\n"); return 1; }
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
        if (fa_gl_row != 4) { printf("rows wrap\n"); return 1; }
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
        fa_gl = 1; fa_gl_row = 4; fturn(EN_ALGO, 1);
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
        fpress(B_EDIT);                                  /* EDIT: FILT */
        if (fa_page != 3) { printf("EDIT -> FILT (%d)\n", fa_page); return 1; }
        fpress(B_LFO);                                   /* LFO and HOME: CTRL, both lit */
        fa_leds();
        if (fa_page != 0) { printf("LFO -> CTRL (%d)\n", fa_page); return 1; }
        fpress(B_SCL);
        fpress(B_HOME);
        if (fa_page != 0) { printf("HOME -> CTRL (%d)\n", fa_page); return 1; }
    }
    printf("fmug_app_test: ok\n");
    {   /* the CUTOFF / EMPH / CNTR knobs glide (200 ms): a click is a ramp of tiny steps, silence snaps */
        track_t *t0 = &trk[0];
        int32_t last, d, n, kk, big;
        uint32_t vi;
        static const uint8_t KN3[3] = {P_E0, P_E1, P_E2};
        for (kk = 0; kk < 3; kk++) {
            int32_t at275 = 0;
            for (vi = 0; vi < NVOICE; vi++)             /* (the patches played before may be polyphonic: their released voices are silent) */
                t0->v[vi].active = 0;
            t0->p[KN3[kk]] = 64;
            fmug_block(t0);
            if (fmug_sm[0][kk] != 64 << 16) { printf("knob glide: silent block does not snap\n"); return 1; }
            t0->v[0].active = 1;
            fmug_block(t0);                        /* the first block of a note follows the knobs at once */
            t0->p[KN3[kk]] = 65;
            last = fmug_sm[0][kk];
            for (n = 0, big = 0; fmug_sm[0][kk] != 65 << 16 && n < 20000; ) {
                fmug_block(t0);
                n++;
                d = fmug_sm[0][kk] - last;
                if (d <= 0 || fmug_sm[0][kk] > 65 << 16) { printf("knob glide: not monotonic\n"); return 1; }
                if (d > 300) big++;
                last = fmug_sm[0][kk];
                if (n == 275) at275 = fmug_sm[0][kk] - (64 << 16);
            }
            /* one time constant (275 blocks = 200 ms) = 63 % of the click */
            if (n >= 20000 || big || at275 < 65536 * 55 / 100 || at275 > 65536 * 72 / 100) {
                printf("knob glide %d: steps %d big %d at 200 ms %d/65536\n", (int)kk, (int)n, (int)big, (int)at275);
                return 1;
            }
            t0->v[0].active = 0;
            fmug_block(t0);
        }
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
        do { e = fmug_contour(t0->p, &vv); n++; } while (((uint32_t)vv.s[7] >> 24) == 1u && n < 2000000);
        sec = (double)n * CTL / FS;
        if (sec < 11.0 || sec > 13.0) { printf("filter attack at max: %.2f s\n", sec); return 1; }
        vv.gate = 0;
        n = 0;
        do { e = fmug_contour(t0->p, &vv); n++; } while (e > 2048 && n < 4000000);   /* -24 dB (Q15) */
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
