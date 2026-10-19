import fs from 'fs';
const wasm = fs.readFileSync('/home/claude/hugelton/felucca/docs/emu/felucca.wasm');
const { instance } = await WebAssembly.instantiate(wasm, {});
const ex = instance.exports;
ex._initialize(); ex.web_boot();
const mem = () => new Float32Array(ex.memory.buffer);
const FS = ex.web_sample_rate();
function render(sec) {            // returns rms dB of L+R
  let acc = 0, n = 0, left = Math.round(sec * FS);
  while (left > 0) {
    const k = Math.min(1024, left); ex.web_render(k);
    const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), k), r = new Float32Array(ex.memory.buffer, ex.web_out_r(), k);
    for (let i = 0; i < k; i++) { acc += l[i]*l[i] + r[i]*r[i]; n += 2; }
    left -= k;
  }
  return 10 * Math.log10(acc / n + 1e-12);
}
const press = (b) => { ex.web_buttons(1 << b); render(0.1); ex.web_buttons(0); render(0.1); };
const turn = (role, d) => { for (let i = 0; i < Math.abs(d); i += 20) { ex.web_enc(role, Math.sign(d) * Math.min(20, Math.abs(d) - i)); render(0.05); } };
console.log('FS', FS);
ex.web_master(1023);
render(1.5);
const B_ENV = 2, K1 = 3, K2 = 4, K3 = 5;
press(B_ENV);                       // F.ENV
turn(K1, -200); turn(K2, +200); turn(K3, -200);
press(B_ENV);                       // A.ENV
turn(K1, -200); turn(K2, +200); turn(K3, -200);
render(0.3);
const mode = process.argv[2] || 'held';
ex.web_keys(1 << 9);               // a key
let base = null;
for (let t = 0; t < 30; t++) {
  if (mode === 'rel' && t === 2) ex.web_keys(0);
  const d = render(0.5); if (base === null) base = d;
  console.log(`t=${((t + 1) * 0.5).toFixed(1)}s  ${(d - base).toFixed(1)} dB rel   (abs ${d.toFixed(1)})`);
}
