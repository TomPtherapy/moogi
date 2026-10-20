/* SPDX-License-Identifier: GPL-3.0-only
 * FMini-1: the eight screens of the README, drawn by the real code of fmini_app.c (as tests/fmini_app_test.c does) into
 * DIR/shot_<name>.ppm: splash, osc, mix, filter, fx, fxrev and fxdly (the FX EDIT pop-ups), preset (the pop-up of patch 00), arp, seq,
 * glo (the GLOBAL list: assets/site/glo.webp is this one, 960 x 960).
 *   cc -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o /tmp/shots tools/readme_shots.c -lm && /tmp/shots /tmp/fa
 *   python3 tools/gen_readme_art.py /tmp/fa
 * (after the generation step of tools/build.py has made build/gen) */
#define UI_TEST_NO_MAIN 1
#define FELUCCA_FMINI_ONLY 1
#include "ui_test.c"
#include "../firmware/src/fmini_app.c"
#include <stdio.h>
static const char *dir;
static void shot(const char *name)
{
    char p[256];
    uint32_t i;
    FILE *f;
    snprintf(p, sizeof p, "%s/shot_%s.ppm", dir, name);
    f = fopen(p, "wb");
    if (!f) { perror(p); exit(1); }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = swap16(host_screen[i]);
        uint8_t b[3] = {(uint8_t)((c >> 11) * 255u / 31u), (uint8_t)(((c >> 5) & 63u) * 255u / 63u), (uint8_t)((c & 31u) * 255u / 31u)};
        fwrite(b, 1, 3, f);
    }
    fclose(f);
}
static void fframe(void) { host_ticks += 16000u; fm1_ms += 16u; fa_input(); }
static void fpress(uint32_t label)
{
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    fframe();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    fframe();
}
static void redraw(void) { ui.force = 1; fa_draw(); }
static void page(uint8_t pg, const char *name)
{
    fa_page = pg;
    fmg_cur[pg] = 0;                                   /* the first control highlighted */
    redraw();
    shot(name);
}
int main(int argc, char **argv)
{
    static const uint8_t RIFF[8] = {36, 48, 43, 46, 36, 48, 51, 50};   /* a bass line for the SEQ */
    uint32_t i;
    dir = argc > 1 ? argv[1] : ".";
    panel_init();
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;   /* main.c's power-on globals */
    fa_init();
    song.g[G_BPM] = 120;                               /* the tempo the ARP and the SEQ show */
    fa_splash();
    shot("splash");
    page(1, "osc");
    page(2, "mix");
    fa_page = 3; fmg_cur[3] = 1; redraw(); shot("filter");   /* the cursor on EMPHASIS */
    page(6, "fx");
    fmg_fx = 1; fmg_cur[7] = 0; redraw(); shot("fxrev");   /* EDIT: the reverb's pop-up, then FX again: the delay's */
    fmg_fx = 2; fmg_cur[8] = 0; redraw(); shot("fxdly");
    fmg_fx = 0; redraw();
    fa_page = 0;                                       /* PRESETS onto patch 00: its pop-up */
    apply_preset_to(TSEL, 1); TSEL->p[P_E7] = 1; fmini_poll(); fa_track_preset();
    fm1_ms += 2000; redraw();
    apply_preset_to(TSEL, 0); TSEL->p[P_E7] = 0; fmini_poll(); fa_track_preset();
    redraw();
    if (!fa_pp_on()) { printf("no preset pop-up\n"); return 1; }
    shot("preset");
    fm1_ms += 2000; redraw();
    fa_ar = 1;                                         /* the ARP pop-up, the arpeggiator on, UP */
    trk[0].p[P_AMODE] = 1; trk[0].p[P_AOCT] = 1; trk[0].p[P_AGATE] = 50;
    redraw();
    shot("arp");
    fa_ar = 0; trk[0].p[P_AMODE] = 0;
    fa_page = 1; fa_gl = 1; fa_gl_row = 0; redraw();       /* GLO: the list (MIDI IN, CHANNEL, MIDI CC, PITCH BEND, ABOUT) */
    shot("glo");
    fa_gl = 0; fa_page = 0; redraw();
    fpress(B_SEQ);                                     /* SEQ: a take of eight notes, playing */
    fpress(B_REC);
    for (i = 0; i < 8u; i++) { input_on(&trk[0], RIFF[i], 100); trk_all_off(&trk[0]); }
    fpress(B_REC);
    fpress(B_PLAY);
    for (i = 0; i < 400; i++) { events_block(CTL); mix_block((int32_t[2 * CTL]){0}, CTL); }
    for (i = 0; i < 700; i++) { int32_t o[2 * CTL]; mix_block(o, CTL); }
    redraw();
    if (!fa_sq || !song.playing) { printf("seq not open / not playing\n"); return 1; }
    shot("seq");
    printf("11 shots in %s\n", dir);
    return 0;
}
