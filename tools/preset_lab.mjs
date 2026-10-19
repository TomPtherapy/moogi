// SPDX-License-Identifier: GPL-3.0-only
// FMini-1: renders factory patches through the real firmware (the browser build, felucca.wasm) for tools/preset_lab.py.
//   node tools/preset_lab.mjs felucca.wasm scenarios.json outdir
// scenarios.json: [{ "id": "00_mini", "preset": 0, "dur": 6.0, "events": [[t, "on", note, vel], [t, "off", note], [t, "cc", n, v], [t, "bend", v]] }]
// Each scenario boots a fresh device (no tail of the one before), selects the patch with a MIDI Program Change (the
// way a patch loads from the PRESETS list: its arpeggiator, FX settings and octave come with it), plays the events
// and writes outdir/<id>.wav (stereo, float32, the device's output, 1.0 = full scale).
import fs from 'fs';
const [, , wasmPath, scenPath, outDir] = process.argv;
const wasm = fs.readFileSync(wasmPath);
const scen = JSON.parse(fs.readFileSync(scenPath, 'utf8'));
fs.mkdirSync(outDir, { recursive: true });
const mod = await WebAssembly.compile(wasm);

function wavFloat(path, l, r, fs_) {
  const n = l.length, buf = Buffer.alloc(44 + n * 8);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + n * 8, 4); buf.write('WAVEfmt ', 8);
  buf.writeUInt32LE(16, 16); buf.writeUInt16LE(3, 20); buf.writeUInt16LE(2, 22); buf.writeUInt32LE(fs_, 24);
  buf.writeUInt32LE(fs_ * 8, 28); buf.writeUInt16LE(8, 32); buf.writeUInt16LE(32, 34); buf.write('data', 36); buf.writeUInt32LE(n * 8, 40);
  for (let i = 0; i < n; i++) { buf.writeFloatLE(l[i], 44 + i * 8); buf.writeFloatLE(r[i], 48 + i * 8); }
  fs.writeFileSync(path, buf);
}

for (const sc of scen) {
  const { exports: ex } = await WebAssembly.instantiate(mod, {});
  ex._initialize(); ex.web_boot();
  const FS = ex.web_sample_rate();
  ex.web_master(1023);
  const L = [], R = [];
  let nan = 0;
  const render = (sec, keep) => {
    let left = Math.round(sec * FS);
    while (left > 0) {
      const k = Math.min(256, left); ex.web_render(k);
      const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), k), r = new Float32Array(ex.memory.buffer, ex.web_out_r(), k);
      for (let i = 0; i < k; i++) { if (!Number.isFinite(l[i]) || !Number.isFinite(r[i])) nan++; if (keep) { L.push(l[i]); R.push(r[i]); } }
      left -= k;
    }
  };
  render(3.4, false);                                // the splash
  if (sc.preset >= 0) ex.web_midi(0xC0, sc.preset, 0);
  render(0.5, false);
  const ev = (sc.events || []).slice().sort((a, b) => a[0] - b[0]);
  let t = 0;
  for (const e of ev) {
    if (e[0] > t) { render(e[0] - t, true); t = e[0]; }
    if (e[1] === 'on') ex.web_midi(0x90, e[2], e[3] ?? 100);
    else if (e[1] === 'off') ex.web_midi(0x80, e[2], 0);
    else if (e[1] === 'cc') ex.web_midi(0xB0, e[2], e[3]);
    else if (e[1] === 'bend') ex.web_midi(0xE0, e[2] & 127, (e[2] >> 7) & 127);
  }
  if (sc.dur > t) render(sc.dur - t, true);
  wavFloat(`${outDir}/${sc.id}.wav`, L, R, FS);
  if (nan) console.log(`${sc.id}: ${nan} NaN / inf samples`);
}
console.log('rendered', scen.length);
