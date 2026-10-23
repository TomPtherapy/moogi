# CLAUDE.md

**Acid Moogi** (moogi): Tom's personal firmware for the M-VAVE FM-1. A fork of FMini-1 (Minimoog-style, by Guilherme Ruppert),
itself a fork of [Felucca](https://github.com/hugelton/Felucca) (GPL-3.0-only, by Leo Kuroshita). Repository:
<https://github.com/TomPtherapy/moogi>. For Tom's own use, not for publishing. The credits to Ruppert and Kuroshita stay, as the
licence requires (`CREDITS.md`, `LICENSE`, `LICENSING.md`).

The long technical guide is [DEVELOPING.md](DEVELOPING.md); its sections 3, 7a and 12 are the ones that matter for most work. Do not
read it whole at the start of a session. [BUILDING.md](BUILDING.md) is the toolchain setup. DEVELOPING.md and the site text still
speak of FMini-1 and Ruppert's site: that is history, not an instruction.

## Working with Tom

- **Language.** Tom writes Hebrew: answer in Hebrew, addressing him in the masculine; Claude refers to herself in the feminine.
  No em dashes in Hebrew text. Code, comments, commit messages and these documents are English.
- **Plain language.** Tom does not program. Explain what a change does on the instrument (what he will hear, see, press), not how
  the code does it. No jargon unless he asks.
- **Nothing is built until Tom says so explicitly.** A question, an idea, "what if" or "let's try" about a design is not a go-ahead.
  When he only asks a question, answer it and change nothing.
- **Plan first, in one batch.** Talk through all the changes of a round, record his answers, then build them together when he says
  to build.
- **One topic at a time on the device.** After a build Tom installs it and plays it. Do not run ahead to the next topic before he
  has tested the last one.
- **While building:** one sentence before each change on what changes. At the end: what came out, in a few lines.
- **No check-ins about pacing** ("continue now or stop for today?"). Tom knows his pace.
- **Be honest about uncertainty.** Nothing has been tried on a real FM-1 until Tom says so: say it in the closing message of any
  firmware change. Say what was tested and what was not.

## Project notes (outside the repository)

Tom's claude.ai Project for this firmware holds two documents to read at the start of a session, before opening code:

- `claude/decisions.md` (Hebrew): every decision Tom made, what was built and approved on the device, what is deferred.
  Update it when he decides or approves something.
- `claude/code-map.md`: which file and function holds each feature, which test checks it, where the time goes. Open only the
  files it names for the topic at hand. Update it when code moves.

If the Project is not available, `grep -rn moogi firmware/src` lists every place changed for Acid Moogi, and the header comment
of `firmware/src/fmini_seq.c` is the manual of the sequencer, the tempo and the acid line.

## Keeping it fast

- **Do not run the whole test suite for a change in one area.** It takes 20 to 30 minutes. Build, then run only the tests of that
  area (their `cc` lines are in `tests/run_tests.sh`; code-map.md says which). Run the whole suite only for a change in shared
  code (`voice.c`, `seq.c`, `fx.c`, `audio.c`, storage), and then in the background.
- The toolchain and the SDK stay installed on Tom's computer; in a cloud sandbox they must be fetched again each session
  (several minutes). Prefer working on Tom's computer when the session is linked to it.
- Installing without waiting for the site (not yet tried with Tom): `python3 tools/fm1_install.py build/felucca.fwsc` over USB MIDI
  (`pip3 install mido python-rtmidi`). Offer it; do not use it without Tom.

## Commands (from the repository root)

```
export JIELI_TOOLCHAIN=<dir with pi32v2/bin/clang>   AC79_SDK=<AC79 SDK checkout with cpu/wl82/tools/uboot.boot>
./build.sh                                  # build/felucca.bin, build/loader/ota.bin, build/felucca.fwsc (~30 s)
sh tests/run_tests.sh > /tmp/tests.log 2>&1 # the whole host suite, 20-30 minutes; only when needed, in the background
python3 tools/fmini_filter.py                # regenerate firmware/src/eng_fmini_filter.h (filter laws and tables)
python3 tools/gen_fmini_presets.py           # regenerate firmware/src/eng_fmini_presets.h from tools/fmini_bank.py
python3 tools/gen_splash.py                  # regenerate firmware/src/fmini_splash.h from assets/fmini-splash.png
WASI_SDK=<wasi-sdk-25.0 dir> sh web/emu/build_wasi.sh                    # docs/emu (the browser emulator)
python3 web/make_fmini_installer.py build/felucca.fwsc docs/index.html X.Y.Z  # the installer page (docs/index.html)
```

The Acid Moogi tests: `tests/fmini_app_test.c` (screens, buttons, knob smoothing, mono, sequencer, settings), `tests/fmini_acid_test.c` (the acid line
against the norns mod, `tests/fmini_acid_ref.txt`), `tests/fmini_filter_test.c` (filter, the resonance curve, accent, slide),
`tests/fmini_fx_test.c` (delay and the other FX), `tests/motion_test.c` (knob recording). In a sandbox the toolchain comes from
`tools/get_toolchain.sh` and the SDK clone is in BUILDING.md; if a download is refused by the network allowlist, say so plainly.

## Git and the site

- One branch, `main`. `git fetch origin` before committing (Tom may edit files on the GitHub site), commit on `main`, push `main`.
- Commit trailers: the ones the session's system reminder gives (`Co-Authored-By: ...` and `Claude-Session: ...`).
- A push to `main` that touches `firmware/`, `web/`, `tools/`, `assets/`, `docs/emu/`, `docs/fmini-ad*`, `build.sh` or the workflow
  runs the "Installer" GitHub Actions workflow, which builds the firmware and publishes the install page on the fork's GitHub
  Pages a few minutes later. Documents at the root do not trigger it. Tell Tom when the page is expected to be ready.
- The version is `FMINI_VERSION` in `firmware/src/fmini_app.c` (`v1.8.0-mN`: N goes up with each round Tom installs). The workflow,
  the installer and the splash read it from there. After a version bump rebuild `docs/emu` and `docs/index.html`.
- Give Tom the `.fwsc` file at the end of a firmware change, with where to install it from.
- A session's GitHub token cannot create Releases (403). Not needed: Tom installs from the page or the file.

## Rules that are easy to break

- The firmware is integer-only and one compilation unit (`firmware/src/felucca.c` includes the rest; **the order matters**;
  `fmini_app.c` near the end includes `fmini_seq.c`). No floating point, no 64-bit multiplies in a per-sample loop (int64 only per
  block). Floating point lives in `tools/`.
- `firmware/engines/` is a git submodule (MIT, another repository): never edit it. Value formats the engine headers do not
  have go in `firmware/src/core.h`.
- Engine numbers (`ENGINES[]` in `engines.c`) are stored in projects, presets and the editor protocol: append, never reorder.
- Generated files are checked in and are not edited by hand: `eng_fmini_filter.h`, `eng_fmini_presets.h`, `fmini_splash.h` (and
  `build/gen`, which is not checked in). Change the generator, rerun it, commit both.
- The factory bank (`tools/fmini_bank.py`) is written in physical units (Hz, ms, cents). A change of a law (the filter's) needs
  a conversion of the bank (`tools/fmini_retune.py` is the pattern) and new levels (`tools/fmini_levels.json`). The presets must
  keep sounding as they did (Tom approved that for the resonance change in m2).
- Accent: only the acid line writes accents, and an accented step opens the filter (`seq.c` `t->acc_note`, `eng_fmini.c`). A key
  struck hard stays loudness only: `voice.c`'s velocity accent skips `ENG_FMINI`. `tests/fmini_filter_test.c` section 3a holds it.
- The acid line writes the steps from the main loop (`fa_acid_poll`), never from the audio interrupt. Its build must stay identical
  to the norns mod for the same seed (`fmini_acid_test.c`).
- Settings saved at power-off live in bytes 67..211 of the settings record (`fa_cfg_byte`); the list is above
  `fa_seq_cfg_load` in `fmini_seq.c`. A new saved setting takes the next free byte and goes on that list.
- Mark every change made for Acid Moogi with a `moogi:` comment, so `grep -rn moogi` finds it.
- Any change of a stored value's meaning needs a note in `web/EDITOR_PROTOCOL.md` and, if possible, a migration.
- New source files carry `SPDX-License-Identifier: GPL-3.0-only`. Credit outside work in `CREDITS.md`.
- New tests are registered in `tests/run_tests.sh`. `tests/target_budget.txt` (static loop-instruction cost of the render
  functions) and `tests/golden.txt` (render hashes) change only on purpose: `BUDGET_UPDATE=1 python3 tests/target_budget.py
  build/felucca.dis tests/target_budget.txt`; for goldens run `build/host/regress` against a scratch copy with `GOLDEN_UPDATE=1`
  and merge only the lines you mean to change.

## The test suite: what fails and is not ours

Seven sections of the whole suite fail at 1.8.0, as they did before the fork's changes: `fm4` (the DIGITAL conversion), the three
`regress` sections (golden hashes of Felucca's other engines: 76 differ; the `FELUCCA_CPU2=0` build also differs for 41 FMINI
patches, cause not investigated), `union_test` (720 engine-switch hashes), `ui_render` (126 lint findings on the CTRL and FILT
screens), `web pages` (3 FAIL lines in the mock tables). Do not chase them unless asked. The runner's `run()` only sets a flag and
does not name the failing section: to find one, copy the script, make `run()` print the section on a non-zero exit, read the log.
A test that fails and prints nothing is a `return 1` with no message: run the binary alone. `web/test_editor_fmini.py` can fail
once when the machine is busy; run it alone before believing it. A new failure outside this list is ours.

## Before saying a change is done

Build; run the tests of the area changed (the whole suite only for shared code); regenerate the emulator and the installer if the
firmware or the site changed; commit with the trailers; push `main`; give Tom the `.fwsc` and say that nothing was tried on the
device yet. Then wait for him to play it.
