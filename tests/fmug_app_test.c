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
    {   /* the ARP settings and the tempo are part of a saved preset: SAVE keeps them, loading the preset sets them,
         * a factory patch turns the arpeggiator off */
        int16_t bpm0 = song.g[G_BPM];
        apply_preset_to(TSEL, 2);
        TSEL->p[P_AMODE] = 3; TSEL->p[P_ARATE] = 4; TSEL->p[P_AOCT] = 3; TSEL->p[P_AGATE] = 90; TSEL->p[P_AHOLD] = 1;
        song.g[G_BPM] = 93;
        fa_sv_open();
        fa_sv_slot = 1;
        str_cpy(fa_sv_name, "ARP PATCH", 13);
        fa_sv_do();
        apply_preset_to(TSEL, 4);
        song.g[G_BPM] = 150;
        if (TSEL->p[P_AMODE] != 0 || TSEL->p[P_AHOLD] != 0) { printf("arp: a factory patch leaves the ARP on\n"); return 1; }
        up_load(1);
        if (TSEL->p[P_AMODE] != 3 || TSEL->p[P_ARATE] != 4 || TSEL->p[P_AOCT] != 3 || TSEL->p[P_AGATE] != 90 || TSEL->p[P_AHOLD] != 1 ||
            song.g[G_BPM] != 93) {
            printf("arp: preset load: mode %d rate %d oct %d gate %d hold %d bpm %d\n", TSEL->p[P_AMODE], TSEL->p[P_ARATE],
                   TSEL->p[P_AOCT], TSEL->p[P_AGATE], TSEL->p[P_AHOLD], song.g[G_BPM]);
            return 1;
        }
        up_load(0);                                     /* the first saved patch has its own (ARP off, its tempo) */
        TSEL->p[P_AMODE] = 0; TSEL->p[P_AHOLD] = 0; TSEL->p[P_ARATE] = TP[P_ARATE].def; TSEL->p[P_AOCT] = 1; TSEL->p[P_AGATE] = TP[P_AGATE].def;
        song.g[G_BPM] = bpm0;
        apply_preset_to(TSEL, 0);
        TSEL->user = 0;
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
        fa_gl = 1; fa_gl_row = 2; fm_bend_mode = 0;
        sig = fa_signature();
        fturn(EN_ALGO, 1);
        if (fm_bend_mode != 1 || fa_signature() == sig) { printf("bend redraw\n"); return 1; }
        fa_gl = 3; fturn(EN_ALGO, 1);
        if (fa_gl != 1) { printf("about ALGO\n"); return 1; }
        fa_gl = 3; fturn(EN_SELECT, 1);
        if (fa_gl != 1) { printf("about SELECT\n"); return 1; }
        fa_gl = 0;
        {   /* FX: the cursor walks onto DECAY / TIME / RATE / TONE and ALGORITHM changes each global */
            static const uint8_t GID[4] = {G_RSIZE, G_DRREV, G_CRATE, G_DRLVL};
            uint32_t q;
            fa_ar = 0; fa_page = 6; fmg_cur[6] = 3;
            for (q = 0; q < 4u; q++) {
                int16_t b4;
                fturn(EN_SELECT, 1); fframes(64);
                if (fmg_cur[6] != 4u + q) { printf("FX cursor %u -> %u\n", q, fmg_cur[6]); return 1; }
                song.g[GID[q]] = GP[GID[q]].def; b4 = song.g[GID[q]];
                fturn(EN_ALGO, 1); fframes(64);
                fturn(EN_ALGO, 1); fframes(64);
                if (song.g[GID[q]] == b4) { printf("FX knob %u does not move (%d)\n", q, b4); return 1; }
                { uint32_t g0 = fa_signature(); song.g[GID[q]] ^= 8; if (fa_signature() == g0) { printf("FX fader %u: no redraw\n", q); return 1; } song.g[GID[q]] ^= 8; }
            }
        }
        fpress(B_SCL);                                   /* SCL: MIX, then FILT */
        if (fa_page != 2) { printf("SCL -> MIX (%d)\n", fa_page); return 1; }
        fpress(B_SCL);
        if (fa_page != 3) { printf("SCL -> FILT (%d)\n", fa_page); return 1; }
    }
    printf("fmug_app_test: ok\n");
    return 0;
}
