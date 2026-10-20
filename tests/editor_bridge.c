/* SPDX-License-Identifier: GPL-3.0-only
 * The firmware's real editor handler behind stdin / stdout, for web/test_editor_fmini.py: each input line is the hex of a
 * SysEx request (F0 .. F7); the answer is the hex of the reply frame (or an empty line when there is none).
 * Track 1 is an FMINI track on its first factory patch, as on an FMini-1. */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"
#include <stdio.h>
int main(void)
{
    char line[2048];
    reset();
    set_engine_of(&trk[0], ENGI_FMINI);
    apply_preset_to(&trk[0], 0);
    trk[0].engine = trk[0].eng_req;
    fmini_track_loaded(&trk[0]);
    song.sel = 0;
    setvbuf(stdout, 0, _IOLBF, 0);
    while (fgets(line, sizeof line, stdin)) {
        uint8_t a[1024];
        uint32_t n = 0, i, got;
        char *p = line;
        unsigned v;
        int k;
        while (sscanf(p, "%2x%n", &v, &k) == 1 && n < sizeof a) { a[n++] = (uint8_t)v; p += k; }
        if (n < 6u) { puts(""); continue; }
        got = request(a[4], a + 5, n - 6u);                  /* (F0 7D 46 4C cmd args F7) */
        for (i = 0; i < got; i++)
            printf("%02x", host_wire[i]);
        puts("");
    }
    return 0;
}
