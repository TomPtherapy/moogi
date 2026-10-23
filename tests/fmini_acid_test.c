/* SPDX-License-Identifier: GPL-3.0-only
 * Acid Moogi: the acid line (fmini_seq.c) against the norns acid mod it ports. tests/fmini_acid_ref.txt holds 300 lines
 * the mod's own Lua built (seed scale notes high-range density movement tension accents slides | the 16 steps as
 * on voice interval.octave accent slide); the C port must build the same 16 steps for each. */
#define UI_TEST_NO_MAIN 1
#define FELUCCA_FMINI_ONLY 1
#include "ui_test.c"
#include "../firmware/src/fmini_app.c"
#include <stdio.h>
int main(int argc, char **argv)
{
    char line[512];
    FILE *f = fopen(argc > 1 ? argv[1] : "tests/fmini_acid_ref.txt", "r");
    int bad = 0, n = 0;
    if (!f) { printf("no reference file\n"); return 1; }
    { uint32_t gi; for (gi = 0; gi < G_COUNT; gi++) song.g[gi] = GP[gi].def; }
    fa_init();
    fa_parts = 1;
    while (fgets(line, sizeof line, f)) {
        unsigned c[9]; char out[400] = "", *exp = strchr(line, '|') + 1; uint32_t i;
        sscanf(line, "%u %u %u %u %u %u %u %u %u", &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &c[6], &c[7], &c[8]);
        ac_v[AV_SCALE] = c[1] - 1; ac_v[AV_NOTES] = c[2]; ac_v[AV_HIGH] = c[3]; ac_v[AV_DENS] = c[4]; ac_v[AV_MOVE] = c[5];
        ac_v[AV_TENS] = c[6]; ac_v[AV_ACC] = c[7]; ac_v[AV_SLD] = c[8];
        memset(ac_off, 0, sizeof ac_off);
        ac_draw(c[0]); ac_select_pool(); ac_build();
        for (i = 0; i < 16; i++) { ac_step_t *p = &ac_p[i]; sprintf(out + strlen(out), "%s%d%s%d.%d%d%d", i ? " " : "", p->on, p->hi ? "h" : "l", p->iv, p->oct, p->acc, p->sld); }
        exp[strcspn(exp, "\n")] = 0;
        if (strcmp(out, exp)) { if (bad < 5) printf("seed %u:\n C   %s\n lua %s\n", c[0], out, exp); bad++; }
        n++;
    }
    {   /* breathe's way back: ac_born gives every die of a 64-step line as drawn */
        uint32_t i, k;
        ac_draw(4242u);
        for (i = 0; i < AC_NS; i++)
            for (k = 0; k < AC_NF; k++)
                if (ac_born(i, k) != ac_f[i][k]) { printf("ac_born %u %u\n", i, k); return 1; }
    }
    if (bad || n != 300) { printf("acid: %d of %d lines differ from the norns mod\n", bad, n); return 1; }
    printf("fmini_acid_test: ok (%d lines as the norns mod builds them)\n", n);
    return 0;
}
