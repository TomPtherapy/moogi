#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# web/editor_fmini.html against the firmware's real editor handler: headless Chromium with a fake Web MIDI port whose
# SysEx goes to tests/editor_bridge.c (the C protocol code of the FMini-1 firmware, host build).
#   web/test_editor_fmini.py [bridge-binary] [screenshot-dir]
import subprocess, sys, time
from pathlib import Path
from playwright.sync_api import sync_playwright

ROOT = Path(__file__).resolve().parent.parent
BRIDGE = sys.argv[1] if len(sys.argv) > 1 else None
SHOTS = Path(sys.argv[2]) if len(sys.argv) > 2 else None
if not BRIDGE:
    BRIDGE = "/tmp/editor_bridge"
    subprocess.run(["cc", "-O1", "-w", "-Ibuild/gen", "-Ifirmware/src", "-o", BRIDGE, "tests/editor_bridge.c", "-lm"], cwd=ROOT, check=True)
proc = subprocess.Popen([BRIDGE], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, cwd=ROOT)
log = []

def fw(cmd, args=()):
    """a request to the firmware: the reply's bytes after F0 7D 46 4C cmd, without F7 (None: no reply)"""
    line = bytes([0xF0, 0x7D, 0x46, 0x4C, cmd, *args, 0xF7]).hex()
    proc.stdin.write(line + "\n"); proc.stdin.flush()
    r = proc.stdout.readline().strip()
    return list(bytes.fromhex(r)) if r else None

def fa(cmd, args=()):
    """the reply's arguments (after the command byte, without F7)"""
    r = fw(cmd, args)
    return r[5:-1] if r else None

def on_tx(data):
    log.append(list(data))
    r = fw(data[4], data[5:-1])
    if data[4] == 19 and r and r[6] == 2:        # UP_STORE: the host build has no flash ("kept in RAM"): a real device answers 0
        r[6] = 0
    return r

bad = 0
def check(name, ok):
    global bad
    print(f"editor_fmini: {name:<84}{'ok' if ok else 'FAIL'}")
    bad += 0 if ok else 1

MOCK = """
(() => {
  const mk = (kind) => ({ name: 'FM-1 Midi', state: 'connected', type: kind, open: async () => {}, onmidimessage: null });
  const inp = mk('input'), out = mk('output');
  out.send = (d) => { window.__tx(Array.from(d)).then((r) => { if (r && inp.onmidimessage) inp.onmidimessage({ data: new Uint8Array(r) }); }); };
  const acc = { inputs: new Map([['i', inp]]), outputs: new Map([['o', out]]), onstatechange: null };
  window.__acc = acc; window.__inp = inp;
  navigator.requestMIDIAccess = async () => acc;
})();
"""

with sync_playwright() as pw:
    br = pw.chromium.launch()
    pg = br.new_page(viewport={"width": 1100, "height": 820})
    errs = []
    pg.on("pageerror", lambda e: errs.append(str(e)))
    pg.on("console", lambda m: errs.append(m.text) if m.type == "error" else None)
    pg.expose_function("__tx", on_tx)
    pg.add_init_script(MOCK)
    pg.goto((ROOT / "web" / "editor_fmini.html").as_uri())
    check("loads off-line: panel greyed, Connect offered", pg.evaluate("document.body.classList.contains('off')") and pg.inner_text("#connect") in ("Conectar", "Connect"))

    pg.click("#connect")
    pg.wait_for_function("!document.body.classList.contains('off')", timeout=15000)
    check("Connect: handshake, descriptors, names; status shows connected", "FMini-1" in pg.inner_text("#stxt") and not errs)
    check("INFO told the page the patch commands exist", pg.evaluate("__fmini.S.canPatch") is True)
    check("the preset list has the 100 factory patches", pg.evaluate("document.querySelectorAll('#preset option').length") >= 100)
    pg.wait_for_timeout(300)

    # a track parameter through a knob (FILT > CUTOFF = P_E0)
    pg.click("#tabs button:has-text('FILT')")
    pe0 = pg.evaluate("__fmini.S.pe0")
    before = fa(31, [0, pe0])
    knob = pg.locator(".knob", has_text="CUTOFF")
    knob.focus()
    v0 = pg.evaluate("__fmini.S.p[__fmini.S.pe0]")
    for _ in range(3):
        pg.keyboard.press("ArrowDown")
    pg.wait_for_timeout(400)
    after = fa(31, [0, pe0])
    d0 = (before[2] | before[3] << 7) - 8192; d1 = (after[2] | after[3] << 7) - 8192
    check("CUTOFF knob (keys): the firmware's P_E0 moved down", d1 < d0 and d1 == pg.evaluate("__fmini.S.p[__fmini.S.pe0]"))
    knob.dblclick()
    pg.wait_for_timeout(300)
    after = fa(31, [0, pe0]); d2 = (after[2] | after[3] << 7) - 8192
    check("double click: back to the parameter's default", d2 == pg.evaluate("__fmini.S.desc['0:' + (__fmini.S.pe0)].def"))

    # drag
    b = knob.bounding_box()
    pg.mouse.move(b["x"] + b["width"] / 2, b["y"] + 30); pg.mouse.down(); pg.mouse.move(b["x"] + b["width"] / 2, b["y"] - 60, steps=8); pg.mouse.up()
    pg.wait_for_timeout(400)
    after = fa(31, [0, pe0]); d3 = (after[2] | after[3] << 7) - 8192
    check("dragging a knob up raises it on the device", d3 > d2)

    # patch bytes: OSC > OSC 1 WAVE = PLS N, MOD OSC switch, GLIDE
    pg.click("#tabs button:has-text('OSC')")
    pg.locator(".seg", has_text="OSC 1 WAVE").locator("button", has_text="PLS N").click()
    pg.wait_for_timeout(300)
    g = fa(82, [0, 0])
    check("OSC 1 WAVE: the firmware's patch byte (FMINI_GET) is PLS N", g[2] == 0 and g[3 + 3] == 5)
    check("  .. SLOT is OWN now (100)", g[3 + 32] == 100)
    pg.click("#tabs button:has-text('CTRL')")
    pg.locator(".sw", has_text="MOD OSC").click()
    pg.wait_for_timeout(300)
    g = fa(82, [0, 0])
    check("MOD OSC switch: bit 32 of the patch's SW1 on, the rest untouched", g[3 + 12] & 32 and g[3 + 3] == 5)
    pg.locator(".sw", has_text="GLIDE").click()                 # (patch 00 has it on, P_GLIDE not synced: the click turns it off)
    pg.wait_for_timeout(300)
    glide = fa(31, [0, 38]); gl = (glide[2] | glide[3] << 7) - 8192
    g = fa(82, [0, 0])
    check("GLIDE switch off: SW2 bit clear, P_GLIDE 0", not (g[3 + 13] & 1) and gl == 0)
    pg.locator(".sw", has_text="GLIDE").click()
    pg.wait_for_timeout(300)
    glide = fa(31, [0, 38]); gl = (glide[2] | glide[3] << 7) - 8192
    g = fa(82, [0, 0])
    check("GLIDE switch on: SW2 bit set, P_GLIDE = the kept time", (g[3 + 13] & 1) and gl == g[3 + 17] and gl > 0)

    # the keys' octave (extras: carried by FMINI_PUT, read back by FMINI_GET)
    pg.locator(".seg", has_text="OCTAVE").locator("button", has_text="+1").click()
    pg.wait_for_timeout(300)
    g = fa(82, [0, 0])
    check("OCTAVE +1: the device's octave (last byte of FMINI_GET) is 5 = +1 + 4", g[36] == 5 and pg.evaluate("__fmini.S.oct") == 1)
    pg.locator(".seg", has_text="OCTAVE").locator("button", has_text="+1").click()   # (stays: it is a choice, not a toggle)

    # FX enum
    pg.click("#tabs button:has-text('FX')")
    sel = pg.locator(".sel", has_text="TYPE").first
    sel.locator("select").select_option(index=2)
    pg.wait_for_timeout(300)
    r = fa(2, [1, 24]); check("REVERB TYPE select: the global G_RTYPE = 2 (PLATE)", ((r[2] | r[3] << 7) - 8192) == 2)

    # presets
    pg.select_option("#preset", "7")
    pg.wait_for_timeout(700)
    g = fa(82, [0, 0]); f = fa(82, [2, 7])
    check("PRESET 07 from the list: the track plays factory patch 7", g[3:35] == f[3:35] and g[35] == 7)
    check("  .. the page shows it (select + knob values reflect the firmware)", pg.input_value("#preset") == "7")
    pg.click("#next"); pg.wait_for_timeout(700)
    check("next ▶: preset 08", pg.input_value("#preset") == "8" and fa(82, [0, 0])[35] == 8)
    pg.click("#prev"); pg.click("#prev"); pg.wait_for_timeout(900)
    check("◀ ◀: preset 06", pg.input_value("#preset") == "6")

    # the panel follows the device (a change made on the panel of the FM-1)
    fa(31, [0, 1, (50 + 8192) & 127, (50 + 8192) >> 7])
    pg.click("#tabs button:has-text('A.ENV')")
    pg.wait_for_timeout(1700)
    check("a change on the FM-1 shows up on the page (polling)", pg.locator(".knob", has_text="ATTACK").get_attribute("aria-valuenow") == "50")

    # sound file round trip
    with pg.expect_download() as dl:
        pg.click("#save")
    p = Path("/tmp/claude_fmini_sound.json"); dl.value.save_as(p)
    saved = fa(82, [0, 0])[3:35]
    fa(83, [0, 0, *[0] * 32])  # a blank patch on the device
    pg.wait_for_timeout(1500)
    pg.set_input_files("#file", str(p))
    pg.wait_for_timeout(700)
    check("Save / Open sound: the file puts the saved patch back on the device", fa(82, [0, 0])[3:35] == saved)

    # ---- phase 3: library, files, A/B, randomize
    pg.on("dialog", lambda d: d.accept())
    pg.click("#tabs button:text-is('LIBRARY'), #tabs button:text-is('BIBLIOTECA')")
    pg.wait_for_selector(".slot")
    check("LIBRARY: 32 slots, all empty on a fresh device", pg.locator(".slot").count() == 32 and pg.locator(".slot.empty").count() == 32)
    cur_patch = fa(82, [0, 0])[3:35]; cur_oct = fa(82, [0, 0])[36] - 4
    pg.fill("#libname", "test one")
    pg.locator(".slot").nth(2).locator("button", has_text="Save here").click()
    pg.wait_for_selector(".slot:not(.empty)")
    lst = fa(16, [0, 16])
    check("Save here: the firmware has U03 'TEST ONE' (UP_STORE)", bytes(lst).find(b"TEST ONE") > 0)
    check("  .. the card shows it with Load / Export / Erase", pg.locator(".slot").nth(2).locator(".nm").inner_text() == "TEST ONE" and pg.locator(".slot").nth(2).locator("button").count() == 4)
    fa(83, [0, 0, *[1] * 32])
    pg.locator(".slot").nth(2).locator("button", has_text="Load").click()
    pg.wait_for_timeout(900)
    check("Load: the stored patch is back on the track", fa(82, [0, 0])[3:35] == cur_patch)
    with pg.expect_download() as dl:
        pg.locator(".slot").nth(2).locator("button", has_text="Export").click()
    import json
    f1 = Path("/tmp/claude_fmini_slot.json"); dl.value.save_as(f1)
    o = json.loads(f1.read_text())
    check("  .. and an octave (the host build keeps none in a record; the FMini-1 build does: tests/editor_test.c)", isinstance(o["oct"], int))
    check("Export: a sound file with the patch, all parameters and the name", o["fmini1"] == 1 and o["patch"] == cur_patch and len(o["p"]) == pg.evaluate("__fmini.S.pCount") and o["name"] == "TEST ONE")
    # a second slot, then the whole library out and back in
    pg.fill("#libname", "second")
    fa(83, [0, 0, *[5] * 32])
    p2 = fa(82, [0, 0])[3:35]
    pg.wait_for_timeout(300)
    pg.locator(".slot").nth(7).locator("button", has_text="Save here").click()
    pg.wait_for_timeout(600)
    with pg.expect_download() as dl:
        pg.click("button:has-text('Export library')")
    f2 = Path("/tmp/claude_fmini_lib.json"); dl.value.save_as(f2)
    lib = json.loads(f2.read_text())
    check("Export library: both presets, with their patches", lib["fmini1lib"] == 1 and [x["slot"] for x in lib["slots"]] == [2, 7] and lib["slots"][1]["sound"]["patch"] == p2)
    for sl in (2, 7):
        pg.locator(".slot").nth(sl).locator("button", has_text="Erase").click()
        pg.wait_for_timeout(300)
    check("Erase (confirmed): both slots empty on the device", pg.locator(".slot.empty").count() == 32)
    pg.set_input_files("#file", str(f2))
    pg.wait_for_selector(".slot:not(.empty)", timeout=20000)
    pg.wait_for_timeout(500)
    lst = bytes(fa(16, [0, 16]))
    check("Import library: U03 and U08 are back", b"TEST ONE" in lst and b"second" in lst.replace(b"SECOND", b"second").lower().replace(b"second", b"second"))
    fa(21, [2]); fa(21, [7])
    pg.click("#tabs button:text-is('CTRL')")
    # A/B
    pg.click("#tabs button:text-is('FILT')")
    kn = pg.locator(".knob", has_text="CUTOFF"); base = kn.get_attribute("aria-valuenow")
    pg.select_option("#preset", "3"); pg.wait_for_timeout(800)
    base = pg.evaluate("__fmini.S.p[__fmini.S.pe0]")
    kn.focus()
    for _ in range(4): pg.keyboard.press("ArrowDown")
    pg.wait_for_timeout(400)
    moved = pg.evaluate("__fmini.S.p[__fmini.S.pe0]")
    pg.click("#ab"); pg.wait_for_timeout(500)
    a_val = (lambda r: (r[2] | r[3] << 7) - 8192)(fa(31, [0, pe0]))
    check("A/B: switching to B gives back the sound as the preset was loaded", moved != base and a_val == base)
    pg.click("#ab"); pg.wait_for_timeout(500)
    b_val = (lambda r: (r[2] | r[3] << 7) - 8192)(fa(31, [0, pe0]))
    check("  .. and back to A: the edit is there again", b_val == moved)
    # randomize
    before = fa(82, [0, 0])[3:35]
    pg.fill("#amt", "100") if False else pg.evaluate("document.getElementById('amt').value = 100; document.getElementById('amt').dispatchEvent(new Event('input'))")
    pg.click("#rnd"); pg.wait_for_timeout(600)
    after = fa(82, [0, 0])[3:35]
    check("Randomize: the patch on the device changed and OSC 1 is still on", after != before and after[12] & 1 and all(0 <= x <= 127 for x in after))
    # the factory bank
    with pg.expect_download(timeout=60000) as dl:
        pg.click("#tabs button:text-is('LIBRARY')"); pg.wait_for_selector(".slot")
        pg.click("button:has-text('Export factory bank')")
    f3 = Path("/tmp/claude_fmini_factory.json"); dl.value.save_as(f3)
    fb = json.loads(f3.read_text())
    check("Export factory bank: 100 patches, each with the factory patch bytes", len(fb["slots"]) == 100 and fb["slots"][9]["sound"]["patch"] == fa(82, [2, 9])[3:35])
    pg.wait_for_timeout(1200)
    check("  .. the sound that was playing is restored", fa(82, [0, 0])[3:35] == after)
    check("  .. and the page is usable again (not busy)", not pg.evaluate("document.body.classList.contains('busy')"))

    if SHOTS:
        SHOTS.mkdir(parents=True, exist_ok=True)
        for i, name in enumerate(["CTRL", "OSC", "MIX", "FILT", "F.ENV", "A.ENV", "FX", "ARP", "LIBRARY"]):
            pg.click(f"#tabs button:text-is('{name}')"); pg.wait_for_timeout(200)
            pg.screenshot(path=str(SHOTS / f"ed_{i}_{name.replace('.', '')}.png"))
        pg.set_viewport_size({"width": 390, "height": 800}); pg.click("#tabs button:text-is('OSC')")
        pg.screenshot(path=str(SHOTS / "ed_mobile.png"))
    check("no errors in the page's console", not errs)
    if errs: print(errs)
    br.close()

proc.stdin.close(); proc.wait(timeout=5)
print("editor_fmini test FAILED" if bad else "editor_fmini test passed")
sys.exit(1 if bad else 0)
