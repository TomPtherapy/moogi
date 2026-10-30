/* SPDX-License-Identifier: GPL-3.0-only */
/* moogi m9: host test of USB AUDIO 10 CH in src/usb.c (FELUCCA_UAC10, uac10 = 1):
 *   descriptors  the 10 CH configuration parsed as a host does (with and without CDC: -DT_CDC=0/1): lengths, the
 *                input terminal and the type I format with 10 channels of 16 bits at 44100 Hz only, the isochronous
 *                endpoint (920 bytes <= 1023), another bcdDevice than MIX's; get_desc gives it with uac10 and MIX's without
 *   rate         SET_CUR 48000 keeps 44.1 (GET_CUR, GET_MAX answer 44100)
 *   ring         renders of HALF_FRAMES frames at the I2S rate (fa_ul_out: a counter on every channel, each its own
 *                offset), packets at 1 kHz: 43..46 frames, every frame once and in order with its 10 channels, then an
 *                underrun (the last frame repeated), an overrun (the ring never overfills) and a restart
 *   mix          uac10 = 0: the stereo stream as before (the master, not fa_ul_out), the ring's words as before
 * The SIE is never touched (uac_service and usb_poll are not called). */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef HALF_FRAMES
#error "-DHALF_FRAMES=n (src/core.h; run_tests.sh passes it)"
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define FELUCCA_OTA 0
#define FELUCCA_CDC T_CDC
#define FELUCCA_UAC 1
#define FELUCCA_UAC10 1
#define CTL 32u
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static volatile uint8_t fa_ul_want;                    /* (eng_drum.c's, set by uac_render_start) */
static uint32_t fa_ul_out[CTL * 5u];                   /* (fx.c's: the block's frames) */
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/src/usb.c"

static int fails;
static int t_bunch;                                     /* m11: every render hands its 4 blocks over at 98 % of the half (full load) */
static int check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
    return ok;
}
static uint32_t le16(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8; }

static void test_descriptors(void)
{
    const uint8_t *d0, *c, *m0, *mc;
    uint16_t l0, lc, lm0, lmc;
    uint32_t off, n, total;
    int it = 0, fmt = 0, iso = 0, csep = 0, lens = 1, mono_ep = 0;
    uac10 = 1;
    check("get_desc: device and configuration with 10 CH", get_desc(0x0100, &d0, &l0) && get_desc(0x0200, &c, &lc));
    uac10 = 0;
    get_desc(0x0100, &m0, &lm0);
    get_desc(0x0200, &mc, &lmc);
    uac10 = 1;
    check("  another descriptor than MIX's", c != mc && d0 != m0);
    check("  bcdDevice: MIX's + 0.20 (a host learns it afresh)", l0 == 18 && d0[13] == m0[13] && d0[12] == m0[12] + 0x20u);
    check("  the device class as MIX's (CDC or not)", d0[4] == m0[4] && d0[5] == m0[5] && d0[6] == m0[6]);
    total = le16(c + 2);
    n = lc;
    check("configuration: wTotalLength = the bytes sent", c[1] == 2 && total == n);
    check("  the same interfaces as MIX's", c[4] == mc[4] && lc == lmc - 3u);   /* (one rate: 3 bytes fewer) */
    for (off = 0; off < n;) {
        const uint8_t *e = c + off;
        if (e[0] < 2 || off + e[0] > n) {
            lens = 0;
            break;
        }
        if (e[1] == 0x24 && e[2] == 2 && e[0] == 12)   /* input terminal */
            it = e[7] == 10 && le16(e + 8) == 3u;
        if (e[1] == 0x24 && e[2] == 2 && e[0] == 11)   /* type I format */
            fmt = e[3] == 1 && e[4] == 10 && e[5] == 2 && e[6] == 16 && e[7] == 1 && e[8] == 0x44 && e[9] == 0xAC && e[10] == 0;
        if (e[1] == 5 && e[2] == 0x84)
            iso = e[3] == 0x05 && le16(e + 4) == 46u * 20u && le16(e + 4) <= 1023u && e[6] == 1;
        if (e[1] == 5 && (e[2] == 0x01 || e[2] == 0x81))
            mono_ep++;
        if (e[1] == 0x25 && e[0] == 7)
            csep = e[3] == 1;
        off += e[0];
    }
    check("  every descriptor's length fits", lens && off == n);
    check("  input terminal: 10 channels, L R first", it);
    check("  type I: 10 channels, 16 bits, 44100 Hz only", fmt);
    check("  EP4 isochronous async, 920 bytes (46 frames x 20), <= 1023", iso);
    check("  sampling frequency control; MIDI's endpoints as MIX's", csep && mono_ep == 2);
}

static void test_rate(void)
{
    uac10 = 1;
    uac.r48 = 0;
    uac_rate_set(48000);
    check("10 CH: SET_CUR 48000 keeps 44.1", uac.r48 == 0);
    uac10 = 0;
    uac_rate_set(48000);
    check("MIX: SET_CUR 48000 goes to 48 (as before)", uac.r48 == 1);
    uac_rate_set(44100);
    uac10 = 1;
}

/* ------------------------------------------------------------------------------- the ring --- */
static double fs_dev = 44117.6;
static uint32_t prod_n, cons_n, cons_bad, cons_rep, sizes[64], npk, last_frame, maxbytes;
static int32_t blk[2 * CTL];
static void render_block(void)
{
    uint32_t i, k;
    if (uac10) {
        for (i = 0; i < CTL; i++, prod_n++)            /* channel c: the frame's count + c * 1000 */
            for (k = 0; k < 5u; k++)
                fa_ul_out[i * 5u + k] = (uint16_t)(prod_n + 2u * k * 1000u) | (uint32_t)(uint16_t)(prod_n + (2u * k + 1u) * 1000u) << 16;
    } else {
        for (i = 0; i < CTL; i++, prod_n++) {
            blk[2 * i] = (int16_t)prod_n;
            blk[2 * i + 1] = (int16_t)~prod_n;
        }
    }
    uac_tap(blk, CTL);
}
static void check_frames(const uint32_t *d, uint32_t n);
static void take_packet(void)
{
    uint32_t d[46u * 5u + 10u], n;
    n = uac_packet(d);
    check_frames(d, n);
}
static void check_frames(const uint32_t *d, uint32_t n)
{
    uint32_t i, k, w = uac10 ? 5u : 1u;
    sizes[n < 64u ? n : 63u]++;
    npk++;
    if (n * w * 4u > maxbytes)
        maxbytes = n * w * 4u;
    for (i = 0; i < n; i++) {
        uint16_t f = (uint16_t)d[i * w];
        int ok = 1;
        if (uac10) {
            int zero = 1;
            for (k = 0; k < 10u; k++) {
                uint16_t v = (uint16_t)(d[i * 5u + k / 2u] >> (k & 1u ? 16 : 0));
                if (v)
                    zero = 0;
                if (v != (uint16_t)(f + k * 1000u))
                    ok = 0;
            }
            if (zero)
                continue;                              /* primed silence */
        } else {
            if (d[i] == 0)
                continue;
            ok = (uint16_t)~f == (uint16_t)(d[i] >> 16);
        }
        if (!ok) {
            cons_bad++;
            continue;
        }
        if (cons_n && f == (uint16_t)last_frame)
            cons_rep++;
        else if (cons_n && f != (uint16_t)(last_frame + 1u))
            cons_bad++;
        last_frame = f;
        cons_n++;
    }
}
static uint64_t now_ns, next_half, next_pkt, next_block, rend_len;
static uint32_t blocks_left, lcg = 777;
#define REND_MIN ((uint64_t)(HALF_FRAMES * 1e9 / fs_dev * 0.07))
#define REND_MAX ((uint64_t)(HALF_FRAMES * 1e9 / fs_dev * 0.85))
static void run(uint32_t ms, int produce, int consume)
{
    uint64_t end = now_ns + (uint64_t)ms * 1000000u;
    while (now_ns < end) {
        uint64_t t = end;
        if (next_half < t)
            t = next_half;
        if (blocks_left && next_block < t)
            t = next_block;
        if (next_pkt < t)
            t = next_pkt;
        now_ns = t;
        if (now_ns >= end)
            break;
        if (now_ns == next_half) {
            next_half += (uint64_t)((double)HALF_FRAMES / fs_dev * 1e9);
            if (produce) {
                lcg = lcg * 1103515245u + 12345u;
                rend_len = t_bunch ? (uint64_t)(HALF_FRAMES * 1e9 / fs_dev * 0.98) : REND_MIN + (lcg >> 8) % (REND_MAX - REND_MIN);
                uac_render_start();
                blocks_left = HALF_FRAMES / CTL;
                next_block = now_ns + (t_bunch ? rend_len : rend_len / blocks_left);
            }
        } else if (blocks_left && now_ns == next_block) {
            render_block();
            blocks_left--;
            next_block += t_bunch ? 0u : rend_len / (HALF_FRAMES / CTL);
        } else if (now_ns == next_pkt) {
            lcg = lcg * 1103515245u + 12345u;
            next_pkt = (now_ns / 1000000u + 1u) * 1000000u + (lcg >> 8) % 500000u;
            if (consume)
                take_packet();
        }
    }
}
static void reset_counts(void)
{
    memset(sizes, 0, sizeof sizes);
    npk = cons_n = cons_bad = cons_rep = maxbytes = 0;
}
static void test_ring(int ten)
{
    uint32_t i, sum, base, w0;
    char s[128];
    uac10 = (uint8_t)ten;
    usb.config = 1;
    memset(&uac, 0, sizeof uac);
    uac_stream(1);
    next_half = now_ns + 300000;
    next_pkt = now_ns + 1000000;
    w0 = ua_w;
    run(3, 1, 1);
    check(ten ? "10 CH: not fed before the host reads; fx.c not asked" : "MIX: not fed before the host reads",
          uac.overruns == 0 && ua_w == w0 && !fa_ul_want);
    uac.flowing = 1;
    run(2000, 1, 1);
    check(ten ? "  fed: fx.c asked for the 10 channels" : "  fed: fx.c not asked (MIX)", fa_ul_want == (uint8_t)ten);
    reset_counts();
    base = uac.underruns + uac.overruns;
    run(10000, 1, 1);
    for (i = sum = 0; i < 64u; i++)
        sum += sizes[i] * i;
    snprintf(s, sizeof s, "  10 s: packets 43..46 frames (mean %.4f), %u bytes at most", (double)sum / npk, maxbytes);
    check(s, sizes[43] + sizes[44] + sizes[45] + sizes[46] == npk && (double)sum / npk > 44.05 &&
          (double)sum / npk < 44.2 && maxbytes <= (ten ? 920u : 184u));
    check("  every frame once, in order, all its channels; no underrun / overrun", cons_bad == 0 && cons_rep == 0 &&
          cons_n == sum && uac.underruns + uac.overruns == base);
    base = uac.underruns;
    run(30, 0, 1);
    check("  the render stops 30 ms: underruns, the last frame repeated", uac.underruns > base && cons_rep > 0 && cons_bad == 0);
    base = uac.overruns;
    run(40, 1, 0);
    check("  the host stops 40 ms: overruns, the ring never overfills", uac.overruns > base && ua_w - ua_r <= UA_N);
    uac_stream(0);
    run(10, 1, 1);
    uac_stream(1);
    uac.flowing = 1;
    reset_counts();
    cons_n = 0;
    run(3000, 1, 1);
    check("  restart: primed again, in order, no glitch", cons_n > 3000u * 44u && cons_bad == 0 && cons_rep == 0);
}


/* ------------------------------------------------------------------- the timing (m10) --- */
/* The SIE as the host sees it: an IN token every 1 ms (the host's clock, 60 ppm off the device's), early in the frame
 * (0 .. 0.15 ms: isochronous first); a queued packet is read by the DMA at the IN token and occupies the bus for its
 * bytes at 12 Mbit/s plus bit stuffing (8 %) and 10 us; TxPktRdy clears when it has gone. An IN token with nothing
 * queued: the host gets no packet (missed, UnderRun set). TIMER5 ticks every 0.1 ms (the device's clock), nested in
 * the render or not: the new service (uq_read_due / uq_after_read every tick) against m9's (uac_packet into one
 * buffer every 5th tick when TxPktRdy is clear). Renders as above. Every frame the host receives is checked (order,
 * the 10 channels) */
static uint32_t t_pkt_ready, t_pkt_len, t_under, t_missed, t_got;
static const uint32_t *t_pkt_buf;
static uint64_t t_tx_end;
static void timing_run(uint32_t ms, int newsvc, uint32_t seed)
{
    uint64_t start = now_ns, end = now_ns + (uint64_t)ms * 1000000u, next_tick = now_ns + 37000u, next_in, frame = 0;
    uint32_t sub = 0, l = seed;
    double host_ms = 1e6 * (1.0 + 60e-6);
    next_in = now_ns + 200000u;
    while (now_ns < end) {
        uint64_t t = end;
        if (next_half < t) t = next_half;
        if (blocks_left && next_block < t) t = next_block;
        if (next_tick < t) t = next_tick;
        if (next_in < t) t = next_in;
        if (t_pkt_ready && t_tx_end && t_tx_end < t) t = t_tx_end;
        now_ns = t;
        if (now_ns >= end) break;
        if (t_pkt_ready && t_tx_end && now_ns == t_tx_end) {        /* the packet has gone */
            t_pkt_ready = 0;
            t_tx_end = 0;
        } else if (now_ns == next_half) {
            next_half += (uint64_t)((double)HALF_FRAMES / fs_dev * 1e9);
            lcg = lcg * 1103515245u + 12345u;
            rend_len = t_bunch ? (uint64_t)(HALF_FRAMES * 1e9 / fs_dev * 0.98) : REND_MIN + (lcg >> 8) % (REND_MAX - REND_MIN);
            uac_render_start();
            blocks_left = HALF_FRAMES / CTL;
            next_block = now_ns + (t_bunch ? rend_len : rend_len / blocks_left);
        } else if (blocks_left && now_ns == next_block) {
            render_block();
            blocks_left--;
            next_block += t_bunch ? 0u : rend_len / (HALF_FRAMES / CTL);
        } else if (now_ns == next_in) {                              /* the host's IN token */
            frame++;
            l = l * 1103515245u + 12345u;
            next_in = start + (uint64_t)(200000.0 + (double)frame * host_ms) + (l >> 8) % 150000u;
            if (t_pkt_ready && !t_tx_end) {
                check_frames(t_pkt_buf, t_pkt_len / 20u);             /* (the DMA reads it now) */
                t_got++;
                t_tx_end = now_ns + (uint64_t)(t_pkt_len * 8u * 1.08 / 12e6 * 1e9) + 10000u;
            } else if (!t_pkt_ready) {
                if (getenv("T_DEBUG")) printf("  (missed IN at frame %llu, queued %u since %u)\n", (unsigned long long)frame, uac.queued, uq.since);
                t_missed++;
                t_under = 1;
            }
        } else if (now_ns == next_tick) {                            /* TIMER5 */
            next_tick += 100000u;
            if (newsvc) {
                if (uq_read_due()) {
                    uint32_t *buf, n = uq_after_read((t_pkt_ready ? 1u : 0u) | (t_under ? 4u : 0u), &buf);
                    if (n) { t_pkt_buf = buf; t_pkt_len = n; t_pkt_ready = 1; t_under = 0; }
                }
            } else if (sub++ % 5u == 0u) {                             /* m9: every 5th tick */
                if (!t_pkt_ready) {
                    if (uac.queued) { if (t_under) uac.missed++; uac.flowing = 1; }
                    uac.queued = 1;
                    t_pkt_len = uac_packet(ep4tx) * 20u;
                    t_pkt_buf = ep4tx;
                    t_pkt_ready = 1;
                    t_under = 0;
                }
            }
        }
    }
}
static void test_timing(int newsvc)
{
    char s[128];
    uint32_t seed;
    uac10 = 1;
    usb.config = 1;
    for (seed = 1; seed <= 3; seed++) {
        memset(&uac, 0, sizeof uac);
        memset(&uq, 0, sizeof uq);
        uac_stream(1);
        t_pkt_ready = t_pkt_len = t_under = t_missed = t_got = 0;
        t_tx_end = 0;
        blocks_left = 0;
        next_half = now_ns + 300000;
        reset_counts();
        timing_run(10000, newsvc, seed * 7919u);
        snprintf(s, sizeof s, "%s, run %u: 10 s, %u packets, %u IN tokens found nothing, %u underruns, %u bad frames",
                 newsvc ? "m10's service (every tick, the next made ahead)" : "m9's service (every 5th tick)", seed, t_got,
                 t_missed, uac.underruns, cons_bad + cons_rep);
        if (newsvc)
            check(s, t_missed <= 1u && uac.underruns == 0 && cons_bad == 0 && cons_rep == 0 && t_got > 9900u &&
                     uq.late == 0);                             /* (the first IN token may come before the first packet) */
        else
            printf("%-72s %s\n", s, t_missed ? "(misses: m9's clicks)" : "(no misses)");
    }
    {   /* the SIE is read only once a packet can have gone: <= 6 reads a 1 ms frame */
        uint32_t reads = 0, i;
        memset(&uq, 0, sizeof uq);
        uac.queued = 1;
        for (i = 0; i < 10u; i++)
            reads += (uint32_t)uq_read_due();
        check("  the 10 ticks after a packet is queued read TxPktRdy on 7 (not the first 3)", reads == 7u);
    }
}

/* m11: the ring's band against renders that hand everything over only at the end of the half (the worst a render that
 * meets the DAC's deadline can do): 10 CH's band (224 .. 272) covers it; MIX's (80 .. 128) would run dry */
static void test_full_load(void)
{
    char s[140];
    uint32_t pass, under[2];
    usb.config = 1;
    for (pass = 0; pass < 2u; pass++) {
        uac10 = (uint8_t)(pass == 0u);
        memset(&uac, 0, sizeof uac);
        memset(&uq, 0, sizeof uq);
        uac_stream(1);
        t_pkt_ready = t_pkt_len = t_under = t_missed = t_got = 0;
        t_tx_end = 0;
        blocks_left = 0;
        next_half = now_ns + 300000;
        reset_counts();
        t_bunch = 1;
        if (uac10)
            timing_run(5000, 1, 4242u);
        else {                                          /* MIX's band, the same consumer at 1 kHz */
            uac.flowing = 1;
            run(5000, 1, 1);
        }
        t_bunch = 0;
        under[pass] = uac.underruns;
        snprintf(s, sizeof s, "%s at full load (blocks at the end of the half): %u underruns, %u bad frames, fill %u .. %u",
                 uac10 ? "10 CH" : "MIX's band", uac.underruns, cons_bad, uac.fill_lo, uac.fill_hi);
        if (uac10)
            check(s, uac.underruns == 0 && cons_bad == 0 && cons_rep == 0 && t_missed <= 1u);
        else
            printf("%-72s %s\n", s, uac.underruns ? "(runs dry: why 10 CH has its own)" : "");
    }
    (void)under;
}

int main(void)
{
    test_descriptors();
    test_rate();
    test_ring(1);
    test_ring(0);
    test_timing(0);
    test_timing(1);
    test_full_load();
    printf("uac10_test (CDC %d): %s\n", T_CDC, fails ? "FAIL" : "ok");
    return fails != 0;
}
