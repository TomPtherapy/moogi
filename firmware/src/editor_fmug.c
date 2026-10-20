/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: the FMUG patch (FMini-1; EDITOR_PROTOCOL.md "FMUG patches", cmds 82, 83). The patch is the 32
 * bytes of eng_fmug.c (FG_*: ranges, waves, tuning, mixer, switches, TUNE, MOD MIX, KBD, GLIDE), every byte 0..127.
 * Targets: 0 a track's own patch (index 0..3), 2 a factory patch (0..FMUG_NFACTORY-1, read only), 3 a user preset's
 * patch (0..UP_SLOTS-1, read only; the preset must be an FMUG one: rc 2 otherwise; a preset stored without its own patch
 * gives the factory patch its SLOT names, as loading it does).
 * GET: target, index -> target, index, rc, [FG_SIZE bytes]. For a track the reply ends with its SLOT (P_E7, 0..127:
 * 0..99 = F1..F100, 100 = OWN) after the patch, which is what the panel shows as the patch number.
 * PUT: target 0, index, FG_SIZE bytes -> target, index, rc. The patch becomes the track's own (SLOT OWN, or the
 * factory patch it equals), as an edit on the panel does. rc: 0 ok, 1 arguments, 2 not an FMUG track.
 * Extras (INFO 48 01 01): the reply of GET target 0 ends with the keys' octave + 4 (1..7) after the SLOT, and PUT takes that
 * byte after the 32 (optional; 1..7, else ignored). 84 FMUG_PRESET: slot -> slot, rc, then 14 x v14: what a user preset
 * kept besides the track's values (FMini-1: the tempo, the octave, the FX pop-ups' globals), as stored: BPM (40..240),
 * OCT (octave + 4: 1..7), the FX page's G_RSIZE G_DRREV G_CRATE G_DRLVL + 1 (1..128), then G_RTYPE G_RDAMP G_DTIME G_DFDBK
 * G_ENGGO G_CDEPTH G_INFO G_MIDI + 1; 0 = the preset has none (older). rc 2: not an FMUG preset. */
enum { ED_FMUG_GET = 82, ED_FMUG_PUT, ED_FMUG_PRESET };
enum { ED_FMUG_TRACK = 0, ED_FMUG_FACTORY = 2, ED_FMUG_USER = 3 };

static int ed_fmug_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i, rc = 0;
    uint8_t pk[FG_SIZE];
    if (cmd == ED_FMUG_PRESET) {                           /* slot -> slot, rc, 14 x v14 */
        const up_rec_t *r = n == 1u && a[0] < UP_SLOTS ? up_rec(a[0]) : 0;
        rc = !r ? 1u : !up_used(a[0]) || r->engine != ENGI_FMUG ? 2u : 0u;
        ed_b(n ? a[0] : 127u); ed_b(rc);
        if (!rc) {
            uint32_t ok = r->ver >= 4u;
            ed_v(ok && r->np <= UP_BPM_AT ? r->packed[UP_BPM_AT] : 0);
            ed_v(ok && r->np <= UP_OCT_AT ? r->packed[UP_OCT_AT] : 0);
            for (i = 0; i < 4u; i++)
                ed_v(ok && r->np <= UP_FX_AT ? r->packed[UP_FX_AT + i] : 0);
            for (i = 0; i < 8u; i++)
                ed_v(ok && r->np <= UP_FX2_AT ? r->packed[UP_FX2_AT + i] : 0);
        }
        return 1;
    }
    if (cmd != ED_FMUG_GET && cmd != ED_FMUG_PUT)
        return 0;
    if (cmd == ED_FMUG_GET)
        rc = n != 2u ? 1u : 0u;
    else
        rc = (n != 2u + FG_SIZE && n != 3u + FG_SIZE) || a[0] != ED_FMUG_TRACK ? 1u : 0u;
    if (!rc && a[0] != ED_FMUG_TRACK && a[0] != ED_FMUG_FACTORY && a[0] != ED_FMUG_USER)
        rc = 1;
    if (!rc && a[1] >= (a[0] == ED_FMUG_FACTORY ? FMUG_NFACTORY : a[0] == ED_FMUG_USER ? (uint32_t)UP_SLOTS : (uint32_t)NTRK))
        rc = 1;
    if (!rc && a[0] == ED_FMUG_USER && (!up_used(a[1]) || up_rec(a[1])->engine != ENGI_FMUG))
        rc = 2;
    if (!rc && a[0] == ED_FMUG_TRACK && trk[a[1]].eng_req != ENGI_FMUG)
        rc = 2;
    if (!rc && cmd == ED_FMUG_PUT) {
        fmug_set_patch(a[1], a + 2);                     /* (sanitized into its ranges) */
        fmug_adopt(a[1]);
        if (n == 3u + FG_SIZE && a[2 + FG_SIZE] >= 1u && a[2 + FG_SIZE] <= 7u)
            song.octave = (int8_t)((int)a[2 + FG_SIZE] - 4);
        ui.force = 1;
    }
    if (!rc && cmd == ED_FMUG_GET) {
        if (a[0] == ED_FMUG_USER) {
            uint8_t w[FM6_PACKED];
            if (!upf_get(a[1], w)) {
                memcpy(pk, w, FG_SIZE);
            } else {
                int16_t v[P_COUNT];
                up_values(up_rec(a[1]), v);
                memcpy(pk, FMUG_FACTORY[v[P_E7] >= 0 && v[P_E7] < (int32_t)FMUG_NFACTORY ? v[P_E7] : 0], FG_SIZE);
            }
        } else {
            memcpy(pk, a[0] == ED_FMUG_FACTORY ? FMUG_FACTORY[a[1]] : fmug_patch[a[1]], FG_SIZE);
        }
    }
    ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
    if (!rc && cmd == ED_FMUG_GET) {
        for (i = 0; i < FG_SIZE; i++)
            ed_b(pk[i]);
        if (a[0] == ED_FMUG_TRACK) {
            ed_b((uint32_t)clamp(trk[a[1]].p[P_E7], 0, 127));
            ed_b((uint32_t)(clamp(song.octave, -3, 3) + 4));
        }
    }
    return 1;
}
