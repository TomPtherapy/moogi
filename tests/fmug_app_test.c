/* SPDX-License-Identifier: GPL-3.0-only
 * FMug-1 screen: renders every page of fmug_app.c with the real drawing code to PPMs (OUT/fa_<page>.ppm) and
 * checks that the screen draws and that SELECT / ALGORITHM / PRESETS reach every control. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#undef FELUCCA_FMUG_ONLY
#define FELUCCA_FMUG_ONLY 1
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
    printf("fmug_app_test: ok\n");
    return 0;
}
