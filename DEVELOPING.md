# Developing FMini-1

The guide for whoever (person or Claude) changes this firmware, or starts another one from it. [CLAUDE.md](CLAUDE.md) is the short
list of rules and commands; [BUILDING.md](BUILDING.md) installs the toolchain; [web/EDITOR_PROTOCOL.md](web/EDITOR_PROTOCOL.md) is the
USB editor protocol. Facts here were read from the sources at v1.7.0; when a file and this guide disagree, the file wins.

## 1. What this is

FMini-1 is firmware for the M-VAVE FM-1 that turns it into one monophonic (optionally 12-voice) synthesizer modelled after the
Minimoog Model D: three oscillators, a noise source, a mixer that overloads softly, a 24 dB/oct transistor-ladder low-pass with its
own contour, a loudness contour, an LFO, an arpeggiator, a small sequencer, three FX buses and 100 factory patches. It is a fork of
Felucca (many engines, a sequencer, projects); FMini-1 keeps Felucca's sound core, FX, MIDI, USB and storage and replaces the user
interface (`FELUCCA_FMINI_ONLY=1`: `fmini_app.c`, `ui_fmini.c`) and adds one engine (FMINI, `eng_fmini.c`). Felucca's other engines are
still built (the regression tests render them) but FMini-1's own screens do not offer them.

The hardware, as the sources use it: a JieLi AC79 chip (pi32v2 core, clang 4.0.1; an optional second core), an I2S codec, a 240 x 240
RGB565 LCD, a key matrix with 14 labelled buttons (`panel.c`: FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+), seven
encoders (SELECT, ALGORITHM, PRESETS, KNOB 1..4), the keys, a MASTER pot, USB (MIDI, audio, a serial console), TRS MIDI IN. Sample
rate 44.1 kHz; the audio interrupt renders one I2S half buffer (128 frames, 2.9 ms) in blocks of `CTL` = 32 samples. Memory at v1.7.0:
image 533 KB, RAM `.data` + `.bss` 65.9 KB of 73.7 KB, the `.pool` section 348 KB of 368 KB (`./build.sh` prints these and fails when
they do not fit).

## 2. Repository map

| Path | What |
| --- | --- |
| `firmware/src/` | the firmware, one compilation unit: `felucca.c` includes everything, in an order that matters |
| `firmware/src/eng_fmini.c` | the FMINI engine: oscillators, mixer, the ladder, the contours, the 32-byte patch |
| `firmware/src/eng_fmini_presets.h`, `eng_fmini_filter.h` | **generated**: the 100 patches; the filter's tables (do not edit) |
| `firmware/src/fmini_app.c`, `ui_fmini.c` | FMini-1's whole UI: pages, knobs, graphs, dialogs (`fa_*`, `fmg_*`) |
| `firmware/src/voice.c`, `fx.c`, `audio.c`, `mod.c` | voices and envelopes; the FX buses and the part mixer; the audio ISR; the modulation matrix |
| `firmware/src/editor*.c`, `up_fm6.c`, `upreset.c`, `project.c`, `storage.c` | USB editor commands; user presets; projects; flash |
| `firmware/engines/` | **git submodule** (MIT, Felucca-Engines): ANALOG LOFI TRIO WHEEL NOISE and the shared DSP. Never edited here |
| `firmware/hal/`, `firmware/loader/` | register access (only here); the update loader |
| `tools/` | Python generators and measuring tools (floating point lives here); `build.py` is what `build.sh` runs |
| `tests/` | host tests (`run_tests.sh`), the render harness `hostsim.c`, goldens, budgets |
| `web/` | `site_fmini.html` (the site), `editor_fmini.html` (the patch editor), `emu/` (browser emulator), `make_fmini_installer.py` |
| `docs/` | what the site serves that is checked in: `index.html` (the installer page, generated), `emu/` (generated wasm), ad video |
| `assets/` | splash, hero, the site's screenshots (`assets/site/*.webp`), README pictures (`assets/readme/`, `tools/gen_readme_art.py`) |
| `.github/workflows/installer.yml` | builds the firmware and publishes the site to GitHub Pages on a push to `main` |

## 3. How the firmware is put together

**One translation unit.** `felucca.c` includes the HAL (header-only), then the sources in dependency order. `engines.c` includes the
engines and defines `ENGINES[]`; `fmini_app.c` is included near the end, before `main.c`. A host test includes the same files
(`tests/hostsim.c`, `tests/ui_test.c`, `tools/readme_shots.c`), so what runs on the host is what runs on the chip, compiled by a
different compiler. Tables that `build.py` generates (`build/gen/*.h`: sine, envelopes, fonts, icons, samples) exist only after one
`./build.sh`; host tests need them.

**Audio.** `audio.c` (the I2S interrupt) calls `mix_block` (`fx.c`) per block: events, then each synth part (`voice.c` renders
its voices through the engine's `render`), per-part DIST / INSERT, level and pan, sends into three buses (chorus, tempo delay,
reverb), the master, USB audio. The main loop runs the UI and MIDI; the interrupt must never wait for it. Data the main loop
changes for the interrupt is published with a generation counter (see `fmini_set_patch`: copy, `RING_PUBLISH()`, bump `fmini_pgen`;
the interrupt takes `fmini_eff[]` at a block's start).

**Engines.** `engine_t` (`firmware/engines/src/felucca_engine.h`): a name, eight EDIT parameters (`P_E0..P_E7`: label, value format,
range, default), presets, and function pointers: `note_on`, `render(track, voice, out, n, vmod)`, optional `amp`, `desc`, `block`
(once per block and part, also with no voice sounding: the place for state that runs on without notes), `keys`, `done`, and `mem`
(bytes in the part's shared engine memory). `voice_t` has `s[8]` of free engine state and three phases. `keep` is a bit mask of
the `s[]` slots a retrigger of a sounding voice preserves; `voice.c` honours only slots 0, 1, 4, 5, 6, 7 (a ladder with states in
`s[2]`, `s[3]` loses them on a retrigger that re-initialises; FMINI's `note_on` does that only from silence). Engine numbers are
stored in projects and presets: **append, never reorder**; `NENGINES` is in `core.h`.

**Value formats.** An EDIT parameter or global is formatted by `param_format` (`params.c`) from its `fmt` (`F_PCT`, `F_TIME`, `F_ENUM`,
...). Formats the submodule does not have are defined in `core.h` after the include (`F_MCUT` 14 and `F_MEMP` 15, the Minimoog's
dials) and numbered in `web/EDITOR_PROTOCOL.md`; the editor's tables are dumped from the firmware.

**Number formats.** Signals are +-32768 (Q15 range as `int32_t`); `mulq15`, `mulq16` and `fmini_mulr` (rounded) are the multiplies.
Per-sample loops use 32-bit integers only; 64-bit products are for once-per-block work (the instruction cost on the chip is measured,
section 8).

## 4. The FMINI engine

Until 1.7.0 the engine, its files and its identifiers were called **FMUG** (the first name of the project). 1.7.1 renamed all of it
to FMINI: the files (`eng_fmini.c`, `fmini_app.c`, `ui_fmini.c`, ...), the `fmini_`/`FMINI_` identifiers, `FELUCCA_FMINI_ONLY`, the
name the engine reports (`ENGINES[]->name`, shown on the device and sent to the editor) and the editor's command names. Old commits,
the v1.7.0 release text and firmware up to 1.7.0 still say FMUG, and the editor accepts both names. The short forms `fmg_` /
`GR_FMG0..6` (the panel's pages) were left as they are. The numbers did not change: engine 14, commands 82-84, the patch bytes.

Signal path per voice (`fmini_render`): three oscillators (TRI, SHARK, SAW, SQR, wide and narrow pulse; polyBLEP; ranges LO and 32'..2')
and a noise source -> mixer (a squared level curve each; the sum goes through `softclip`: the original's overload) -> the ladder ->
trim -> `voice_amp` (the loudness contour). The oscillators drift a few cents (a random walk per voice).

**The patch** is 32 bytes (`FG_SIZE`; 18 used, each 0..127): ranges, waves, OSC 2/3 tuning, mixer levels, the switch bytes `FG_SW1`
(OSC 1-3, NOISE, PINK, MOD OSC, MOD FLT) and `FG_SW2` (GLIDE, LFO SQR), TUNE, MOD MIX, KBD (the cutoff's keyboard tracking), the kept
GLIDE time. The **EDIT values** are track parameters: `E0` CUT, `E1` EMPH, `E2` CNTR (the contour's amount), `E3..E5` the contour's
attack / decay / sustain, `E6` MOD (LFO depth), `E7` SLOT (the factory patch number, or OWN). The patch lives in `fmini_patch[track]`,
is stored in projects and user presets in the slot of FM6's patch (the track plays one engine at a time), and is edited with
`fmini_set_patch`, which sanitises it.

**Voices.** The instrument is MONO (the lowest key wins, a new key retriggers and glides when GLIDE is on) or POLY (12 voices,
`FMINI_POLY`); `fa_mono()` forces anything else to MONO. Track 1 is the instrument; tracks 2-4 are muted parts.

**Contours.** The loudness contour is the host's ADSR (`P_ATK..P_REL`). The filter contour (`fmini_contour`, state in `s[7]`: stage
in the top byte, level Q23 below) has attack, decay and sustain, and its release takes the DECAY time, as the original's.

**Modulation.** The track LFO or noise (MOD MIX) into pitch (MOD OSC) and cutoff (MOD FLT), depth `E6` and the mod wheel; the host's
modulation matrix adds its own routings to the cutoff through `m->cutoff`.

## 5. The ladder filter (1.7.0)

The model and its numbers are in `tools/fmini_filter.py` (read its header); the implementation is the render loop of `eng_fmini.c`
(read the comment above `fmini_render`). In short:

- Four identical one-pole stages, a tanh in front of each (a differential pair), the last output fed back to the input through a
  **lag pole** (the emphasis network and output buffer). The lag pole moves the loop's 180-degree point to 0.686 of the stage pole,
  so the filter sings at **1.578 x its -3 dB corner** and starts to sing at a loop gain of **2.92** (not the ideal ladder's 4).
  Those, the dial positions (singing pitch of the CUTOFF dial at -3..+2: 110.8, 206.6, 472.2, 1073.8, 2344, 5188 Hz), the passband
  loss and peak of EMPHASIS (e.g. -9.5 dB and +11.3 dB at 5.0) and the 1 V/oct keyboard with 0 V on F (MIDI 41) come from the
  calibration notes of CA-72 (<https://github.com/idlefoundry/ca-72>, GPL-3.0-or-later; `docs/calibration/README.md` there). Only the
  numbers were taken; the code is this project's.
- **Zero-delay feedback.** Each stage is a trapezoidal integrator (`G = g / (1 + g)`, `g = tan(pi f / FS) / W0`, so the singing pitch
  is exact at every frequency). The feedback is solved before the sample is processed: y4 is predicted linearly from the four stored
  states (coefficients `c0..c3`), passed through the lag pole, and `u = (x - k * fb) / (1 + k * Gx * G^4)` is the loop's solution;
  then the stages run with their tanh (`softclip`).
- **Units.** The knobs and the pitch work in 1/16 semitone of the singing pitch (MIDI x 16): CUTOFF -> `FMINI_CUT[128]`; plus the
  contour (up to 5 octaves), the keyboard (`(pitch - 41 x 16) x KBD x 516 >> 16`), the LFO (+-2.85 octaves), the host's routings.
  The stage and lag poles come from per-semitone tables (`FMINI_G`, `FMINI_GX`), linear between. EMPHASIS -> `FMINI_K` (the loop gain)
  and `FMINI_TRIM` (an output trim of up to 1.5 dB for the passband loss the hardware has beyond the loop's own).
- **Fixed point.** Signals +-32768; ladder and lag-pole states **Q8**; `G` Q16 (rounded multiply); prediction coefficients Q14; `k` Q12.
  What went wrong before it worked, so nobody repeats it: with Q0 states a quiet ladder at a low cutoff moves less than one least
  significant bit a sample and rounding traps it at a fixed point, so it could never start to sing (Q8 states fixed it); a plain
  floor-biased multiply made small states drift to one sign (`fmini_mulr` rounds); `mulq16` needs its factor below 65536; a ladder that
  must self-oscillate needs a starting nudge, which the hardware gets from noise (`fmini_note_on` seeds `s[0]` with +-700 x 256 when
  EMPH >= 96).
- **Singing** works from the lowest cutoff (35 Hz) to the ceiling (`FOSC_MAX` 19.5 kHz, where the stage pole's `tan` stays below
  about 5), as a sine with THD of about 0.3 to 1.3 % in the middle and 2 to 6 % at the lowest pitches.
- **CPU.** `fmini_render` is 227 instructions a sample in its loops on the chip (static count, `tests/target_budget.txt`), up from 170
  for the old filter. Not measured on hardware.

`tests/fmini_filter_test.c` checks the filter against the hardware's numbers, not against the model: singing from silence (pitch,
THD), 1 V/oct over four octaves of keys, KBD 0 fixed, the dial's marks within a quarter tone, the emphasis table, no singing below
7.5 and singing from 8.5, and boundedness over every cutoff, emphasis and key with a loud saw.

## 6. Factory presets

`tools/fmini_bank.py` (one `add()` per patch, in physical units: oscillator ranges and cents, cutoff as the -3 dB corner in Hz at middle
C with keyboard tracking included, emphasis as the 0..10 dial, ms, Hz) -> `tools/gen_fmini_presets.py` (checks the bank and turns it
into bytes; `cut(f, kbd)` and `emp(d)` call `tools/fmini_filter.py`, the same laws as the firmware) -> `eng_fmini_presets.h` (checked
in; the build does not run the generator). `tools/fmini_levels.json` holds each patch's LEVEL so every patch is about as loud as the
next. `tools/preset_lab.py` renders every patch through the real firmware (the browser build) and prints level, brightness, attack /
decay / release, stereo width and the closest pairs; `--calibrate` rewrites `tools/fmini_levels.json` for the target loudness of
each category (on the last try it moved 63 of 100 levels because the old ones were not at the target; for a law change apply only
the per-patch loudness difference you measured).

**Changing a law without changing the sound.** 1.7.0 did it by matching responses: for each patch, `tools/fmini_retune.py` computes
the old filter's magnitude response (as the patch set it, at middle C), weighted by where the patch had energy (a sawtooth's 1/f
spectrum through the old filter), and searches the new filter's cutoff and emphasis grid for the least log-magnitude distance;
output levels were then trimmed by the measured loudness change. Result: fit error up to about 0.6 dB (one patch, 1.9 dB, held back
from self-oscillating), mean loudness change 0.19 dB, centroid +0.94 semitone on average. Use the same pattern for the next change.

## 7. UI, editor, protocol

**UI** (`fmini_app.c`, `ui_fmini.c`): seven pages (CTRL OSC MIX FILT F.ENV A.ENV FX), each with four knobs; a page's controls are a
table of `fmgf_t` (`ui_fmini.c`: label, kind `FK_BYTE` / `FK_PAR` / `FK_SW` / ..., the patch byte or parameter, icon). The four
encoders KNOB 1..4 edit the page's knobs; SELECT moves a cursor over every control including the extras (switches, waveforms);
ALGORITHM changes the control under it. `fa_knob_col`, `fa_switch`, `fa_fader`, `fa_graph` draw them; `fa_scene` composes a page.
Text is anti-aliased and measured (`fa_ctext`, `text_w`); the layout tests render every screen in every palette. The FILT page's
graph is `FMINI_GRAPH` (17 emphasis rows x 73 columns of dB x 8, from `tools/fmini_filter.py`) shifted to the cutoff's singing pitch
with a dotted marker at it. Screenshots for the README and the site come from the same drawing code (`tools/readme_shots.c`).

**Editor.** The patch editor (`web/editor_fmini.html`, served at `/editor/`) talks to the firmware with the commands in
`EDITOR_PROTOCOL.md` ("FMINI patch": 82 GET, 83 PUT, 84 PRESET; 1.6.1 added the octave and the library of user presets U01-U32).
`firmware/src/editor_fmini.c` is the handler. The editor's tables are read from the device (`ED_DESC`), so a new value format needs a
row in the protocol's table and a case in the editor's `shown()`.

**Stored values.** User presets (`upreset.c`) have spare bytes at the end of the record for what FMini-1 added (tempo, octave, FX
globals: `UP_*_AT`), "0 = none, as every record before". A record cannot tell which filter it was made for: from 1.7.0 on, CUTOFF
and EMPHASIS of anything saved earlier sound different (documented in the protocol; there is no migration). If a future change
of meaning matters to users, add a marker byte there and convert on load.

## 8. Tests

`tests/run_tests.sh` after `./build.sh`, with the toolchain variables set (the package test needs `AC79_SDK`). It takes 20 to
30 minutes; run it in the background and read the log. Parts that matter for this firmware:

| Test | What it guards |
| --- | --- |
| `fmini_filter_test.c` | the ladder against the hardware numbers (section 5) |
| `fmini_app_test.c` | every page, SAVE / PRESETS over the 100 patches and user patches, each patch's ARP / LFO / FX / LEVEL / octave |
| `fmini_fx_test.c` | the FMini-1 FX: reverb, delay and chorus types, TUBE drive, bypass at 0 |
| `editor_test.c`, `web/test_editor_fmini.py` | the editor handler; the editor page against the real C handler in Chromium |
| `ui_render.c` | every screen in every palette: lint (clipped, overlapping text), alignment by ink, a PNG sheet in `build/ui_new/` |
| `regress.c` + `golden.txt`, `cpu_baseline.txt`, `target_budget.*` | render hashes of every engine and preset (the 100 FMINI ones are current in the default build; the `FELUCCA_CPU2=0` build differs for 41, see CLAUDE.md); level, DC, voices; CPU: the host's instruction counter may be missing (it was, in the sandbox: the budget is then not checked), the **static loop-instruction count** of the render functions in the chip's disassembly is always checked (+10 %) |
| the rest | storage, projects, MIDI, USB, update loader, FX, other engines (Felucca's) |

Seven sections fail at 1.7.1 (as at 1.7.0); CLAUDE.md lists them and says which were confirmed on the untouched v1.6.1. `tests/hostsim.c`
renders any engine and preset to a WAV (`build/host/hostsim ENGINE PRESET MONO OUT.wav`, with `SECS`, `CHORD`, `NOTE`,
`PSET=id:v`, `BENCH=1` and more: its header) and is the quickest way to listen to a change. For sound work, write the measurement
first (a test with the number to hit), then the code.

## 9. Site, installer, emulator, release

- `web/site_fmini.html` is the site (Portuguese, English, Japanese text in one script); `web/make_fmini_installer.py` embeds the
  firmware package, the screenshots (`assets/site/`) and the installer logic into one self-contained page: `docs/index.html`.
- The browser emulator: `web/emu/felucca_web.c` compiles the firmware's sources against a simulated FM-1;
  `web/emu/build_wasi.sh` (wasi-sdk 25, no Emscripten) writes `docs/emu/`. The preset lab uses the same wasm.
- On a push to `main` that touches `firmware/`, `web/`, `tools/`, `assets/`, `docs/emu/`, `docs/fmini-ad*`, `build.sh` or the workflow,
  `.github/workflows/installer.yml` builds the firmware and publishes the page, the package (`fmini-1-X.Y.Z.fwsc`), the editor and the
  emulator to <https://ruppert.github.io/fmini-1/> (a few minutes). Documents at the root do not trigger it.
- **Release checklist:** bump `FMINI_VERSION`; build; run the tests; regenerate `docs/emu` and `docs/index.html`; refresh the
  screenshots if screens changed (`tools/readme_shots.c`, then `tools/gen_readme_art.py` and the `assets/site/*.webp` it implies);
  update the protocol doc and the site text; commit; push `main`; give the owner the `.fwsc`. The owner creates the
  GitHub Release by hand. Unless the owner says otherwise, nothing has been tested on a real FM-1: say so.
- Installing is at the owner's risk; recovery of a dead FM-1 needs FM-1-transporter (BUILDING.md).

## 10. How the 1.7.0 filter was built (a method that worked)

1. **Find the circuit and a trusted reference**, read it, and write down what is measured and what is inferred (here: the Model D's
   ladder, and CA-72's calibration against a hardware reference).
2. **Write the model in floating point** in `tools/` with the laws (dial -> frequency, emphasis -> gain, tracking) as small functions
   and the reference measurements as constants; make it emit the firmware's tables, so the two cannot drift apart.
3. **Choose the structure for fixed point before coding it** (zero-delay feedback with a linear prediction: no unit delay in the
   loop, so the pitch and the resonance are exact) and decide the Q formats and where 64-bit is allowed.
4. **Test against the reference's numbers on the host** (`hostsim.c`), including the extremes (lowest cutoff, highest, quiet input,
   silence) where fixed point fails first.
5. **Migrate the content** (presets) by matching what you can measure, then check loudness, brightness and the closest pairs.
6. **Do the UI last** (dial text, graphs), then the editor, the docs, the budgets, the screenshots, the release.
7. Keep the bank's units physical, so the next law change is a matter of rerunning a script.

## 11. Starting another firmware from this base

For example a tonewheel-organ instrument for the FM-1. A checklist, in the order that avoids rework.

1. **A new repository seeded from this one** (clone it, set a new `origin`, keep the history and `LICENSE`: the code is GPL-3.0-only
   and Felucca's authorship stays in the headers; add the new work to `CREDITS.md`). Use a product name without a trademark.
2. **Identity**: `FELUCCA_ID` in `firmware/src/felucca.c` is the package identity the installer checks (`FM-1_900` here; the
   installer page accepts `FM-1_9xx`, `web/make_fmini_installer.py`); give the new firmware its own, then the product name, version
   (`FMINI_VERSION`), splash (`tools/gen_splash.py`, `assets/`), `web/site_fmini.html`, README and the workflow's Pages URL.
3. **The engine**: a new `eng_<name>.c` included in `engines.c`, a new entry **appended** to `ENGINES[]`, `NENGINES` and an `ENGI_`
   constant; fill `engine_t` (eight EDIT parameters, presets, `note_on`, `render`, `block` for free-running state, `mem` for state
   that belongs to a part). Start by copying `eng_fmini.c`'s skeleton: its patch bytes, `fmini_set_patch` / `fmini_eff` hand-off,
   factory table and generated header.
4. **The UI**: copy `ui_fmini.c` and `fmini_app.c` and change the tables first (pages, knobs, extras), then the drawing. Seven pages of
   four knobs and a cursor over the extras is the model; a control that does not fit a knob (nine drawbars) is an extra with its own
   `FK_` kind and its own drawing next to `fa_fader`.
5. **The bank**: copy `fmini_bank.py` / `gen_fmini_presets.py` / `preset_lab.py`, keep the physical units, and make the generator
   check its own output.
6. **Editor and protocol**: new commands take the next free numbers in `EDITOR_PROTOCOL.md`, with an INFO tag so the editor can tell
   old firmware from new; mirror `editor_fmini.c`, `editor_fmini.html`, `tests/editor_test.c` and `web/test_editor_fmini.py`.
7. **Tests with the code**, and the static budget for the new render functions (`tests/target_budget.py` FUNCS).
8. **Site, installer, emulator** last: they are generated from the same sources.

**If the instrument is an organ, read `firmware/engines/src/eng_wheel.c` first** (MIT, 297 lines): WHEEL is already a tonewheel-style
additive organ with nine sine partials at the drawbar footages (16' 5 1/3' 8' 4' 2 2/3' 2' 1 3/5' 1 1/3' 1'; levels 0..8, about
3 dB a step), 16 registrations plus SUB / BODY / TOP trims (eight knobs cannot hold nine bars), percussion (2nd or 3rd harmonic,
normal or soft, fast or slow, single trigger, the 1' bar silent as on the real circuit), key click, a per-voice soft overdrive,
and a cheap rotary speaker (two rotors split near 800 Hz, slow / fast with different ramp times, amplitude and Doppler modulation, all
folded into per-block gains: no delay line). Free-running wheels and a per-block update are the efficient design (cost per sample and partial: a table lookup and a multiply-add). What its header does not mention, and a dedicated firmware might add (check the code before assuming either way): one shared
set of generators that every key draws from (WHEEL keeps its free-running phases per part and voice), the real generators' gear-ratio tuning (slightly off equal temperament), crosstalk and leakage between keys and bars, the scanner vibrato / chorus, a preamp
stage and a rotary speaker with real delay lines. Treat the WHEEL source as a reference and a baseline to beat, with the same
method as section 10 (circuit, reference measurements, float model, fixed point, tests). Mind the licences: WHEEL is MIT (copying
it into a GPL-3.0-only tree is fine with its notice kept); setBfree (a B3 emulator in C) is GPL-2.0-or-later as recalled when this
was written: verify before reusing anything beyond ideas.

## 12. Gotchas collected

- `build/` is not checked in; `build/gen` appears after `./build.sh`. A host test fails to compile without it.
- `tests/run_tests.sh` compiles with `-w`: warnings are hidden. `./build.sh` builds with the chip's clang and shows the warnings that
  matter (shift-precedence ones in the oscillator drift code and a dangling `else` in `fmini_app.c` are old).
- A failing test that prints nothing is a `return 1` with no message (`fmini_app_test.c` did, with stale constants): run the binary
  alone and look at the last line, then at the next `return 1`.
- `tests/golden.txt` holds hashes of Felucca's other engines that no longer match (76 of them, identical on v1.6.1). Do not
  "fix" them in the middle of unrelated work; only the FMINI lines are maintained.
- The firmware is integer-only by design; the wasm build adds `-ffp-contract=off` so the emulator matches the host renders.
- The sandbox for Claude sessions is reclaimed after a while: files in `/tmp` and the toolchain directories disappear. Keep
  scratch work in the session's scratchpad directory and commit what matters.
- The GitHub token of a session cannot create Releases (403). The owner does it.
