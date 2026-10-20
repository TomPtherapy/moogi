#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# FMini-1: regenerate the bank, build the browser firmware and run the lab.
#   WASI_SDK=/path/to/wasi-sdk tools/preset_lab.sh [lab args, e.g. --only 0-9 --pics]
set -e
cd "$(dirname "$0")/.."
LAB=${LAB:-/tmp/fmini_lab}
python3 tools/gen_fmini_presets.py
mkdir -p "$LAB/emu"
OUT="$LAB/emu" web/emu/build_wasi.sh >/dev/null
python3 tools/preset_lab.py --wasm "$LAB/emu/felucca.wasm" --out "$LAB" "$@"
