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
int main(int argc, char **argv)
{
    int pg;
    const char *dir = argc > 1 ? argv[1] : ".";
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
    {   /* SAVE: a user preset in the first free slot, found again by PRESETS; every patch is mono */
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
        TSEL->p[P_VOICE] = V_POLY;
        fa_mono();
        if (TSEL->p[P_VOICE] != V_MONO) { printf("not mono\n"); return 1; }
        {
            uint32_t k;
            for (k = 0; k < FMUG_NFACTORY; k++)
                if (!ENGINES[ENGI_FMUG]->presets[k].mono) { printf("preset %u is poly\n", k); return 1; }
        }
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
            h0 = h0 * 31u + (uint32_t)song.g[G_BPM];
            fa_ctl_apply(n, 127);
            for (i = 0; i < FG_SIZE; i++) h1 = h1 * 31u + fmug_patch[0][i];
            for (i = 0; i < P_COUNT; i++) h1 = h1 * 31u + (uint32_t)TSEL->p[i];
            h1 = h1 * 31u + (uint32_t)song.g[G_BPM];
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
        if (!fm_midi_ok(1, 5) || fm_midi_ok(2, 5) || fm_midi_ok(3, 5)) { printf("src USB\n"); return 1; }
        fm_midi_src = 1;
        if (fm_midi_ok(1, 5) || !fm_midi_ok(2, 5)) { printf("src MIDI\n"); return 1; }
        fm_midi_src = 2;
        if (fm_midi_ok(1, 0) || fm_midi_ok(2, 0) || !fm_midi_ok(3, 0)) { printf("src BT\n"); return 1; }
        fm_midi_src = 3;
        if (!fm_midi_ok(1, 0) || !fm_midi_ok(2, 0) || !fm_midi_ok(3, 0)) { printf("src ALL\n"); return 1; }
        fm_midi_ch = 3;
        if (!fm_midi_ok(1, 2) || fm_midi_ok(1, 3) || fm_midi_ok(1, 0)) { printf("channel 3\n"); return 1; }
        fm_midi_ch = 0;
        /* the pop-ups */
        fa_page = 1;
        fa_gl = 1; fa_gl_row = 0; fm_midi_src = 2; ui.force = 1; fa_draw(); dump(dir, 13);
        fa_gl = 2; fa_cc_i = 20; ui.force = 1; fa_draw(); dump(dir, 14);
        fa_gl = 3; ui.force = 1; fa_draw(); dump(dir, 15);
        fa_gl = 0; fm_midi_src = 3;
        fa_ar = 1; ui.force = 1; fa_draw(); dump(dir, 16);
        fa_ar = 0;
    }
    printf("fmug_app_test: ok\n");
    return 0;
}
