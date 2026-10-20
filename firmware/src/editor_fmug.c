/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: the FMUG patch (FMini-1; EDITOR_PROTOCOL.md "FMUG patches", cmds 82, 83). The patch is the 32
 * bytes of eng_fmug.c (FG_*: ranges, waves, tuning, mixer, switches, TUNE, MOD MIX, KBD, GLIDE), every byte 0..127.
 * Targets: 0 a track's own patch (index 0..3), 2 a factory patch (0..FMUG_NFACTORY-1, read only).
 * GET: target, index -> target, index, rc, [FG_SIZE bytes]. For a track the reply ends with its SLOT (P_E7, 0..127:
 * 0..99 = F1..F100, 100 = OWN) after the patch, which is what the panel shows as the patch number.
 * PUT: target 0, index, FG_SIZE bytes -> target, index, rc. The patch becomes the track's own (SLOT OWN, or the
 * factory patch it equals), as an edit on the panel does. rc: 0 ok, 1 arguments, 2 not an FMUG track. */
enum { ED_FMUG_GET = 82, ED_FMUG_PUT };
enum { ED_FMUG_TRACK = 0, ED_FMUG_FACTORY = 2 };

static int ed_fmug_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i, rc = 0;
    uint8_t pk[FG_SIZE];
    if (cmd != ED_FMUG_GET && cmd != ED_FMUG_PUT)
        return 0;
    if (cmd == ED_FMUG_GET)
        rc = n != 2u ? 1u : 0u;
    else
        rc = n != 2u + FG_SIZE || a[0] != ED_FMUG_TRACK ? 1u : 0u;
    if (!rc && a[1] >= (a[0] == ED_FMUG_FACTORY ? FMUG_NFACTORY : (uint32_t)NTRK))
        rc = 1;
    if (!rc && a[0] != ED_FMUG_TRACK && a[0] != ED_FMUG_FACTORY)
        rc = 1;
    if (!rc && a[0] == ED_FMUG_TRACK && trk[a[1]].eng_req != ENGI_FMUG)
        rc = 2;
    if (!rc && cmd == ED_FMUG_PUT) {
        fmug_set_patch(a[1], a + 2);                     /* (sanitized into its ranges) */
        fmug_adopt(a[1]);
        ui.force = 1;
    }
    if (!rc && cmd == ED_FMUG_GET)
        memcpy(pk, a[0] == ED_FMUG_FACTORY ? FMUG_FACTORY[a[1]] : fmug_patch[a[1]], FG_SIZE);
    ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
    if (!rc && cmd == ED_FMUG_GET) {
        for (i = 0; i < FG_SIZE; i++)
            ed_b(pk[i]);
        if (a[0] == ED_FMUG_TRACK)
            ed_b((uint32_t)clamp(trk[a[1]].p[P_E7], 0, 127));
    }
    return 1;
}
