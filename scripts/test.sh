#!/usr/bin/env bash
# Every test: native DSP tests (plain and ASan/UBSan), the UI contract against
# Schwung's real page planner, the Move build with its ABI check, the device
# benchmark binary, and the licence scan.
#
# Copyright (c) 2026 Hiro Ikezawa. MIT License (see LICENSE).
set -euo pipefail
MODULE="$(cd "$(dirname "$0")/.." && pwd)"
API="$MODULE/third_party/schwung"
SRC=("$MODULE"/dsp/*.c)
INC=(-I"$MODULE/dsp" -I"$MODULE/tests" -I"$MODULE/build/generated" -I"$API")
FLAGS=(-std=gnu11 -g -ffp-contract=off -Wall -Wextra -Wno-unused-function)

echo "== native DSP tests"
bash "$MODULE/scripts/build.sh" native
"${CC:-gcc}" "${FLAGS[@]}" -O2 "${INC[@]}" "$MODULE/tests/test_engine.c" "${SRC[@]}" -lm \
  -o "$MODULE/build/native/test-engine"
"$MODULE/build/native/test-engine" | tee "$MODULE/build/test-log.txt"

echo "== the same under AddressSanitizer + UBSan"
"${CC:-gcc}" "${FLAGS[@]}" -O1 "${INC[@]}" -fsanitize=address,undefined -fno-sanitize-recover=all \
  -fno-omit-frame-pointer "$MODULE/tests/test_engine.c" "${SRC[@]}" -lm -o "$MODULE/build/native/test-engine-asan"
ASAN_OPTIONS=detect_leaks=1 "$MODULE/build/native/test-engine-asan" | grep -E "FAIL|PASSED|FAILURE"

echo "== UI contract (Schwung's real page planner)"
ROOT="${SCHWUNG_ROOT:-$MODULE/../schwung}"
if [[ ! -f "$ROOT/src/shared/param_pages/page_plan.mjs" ]]; then
  echo "SKIP: no Schwung checkout at $ROOT (set SCHWUNG_ROOT)" >&2
elif command -v node >/dev/null 2>&1; then
  SCHWUNG_ROOT="$ROOT" node "$MODULE/tests/test_ui.mjs"
else
  NODE=node.exe; command -v node.exe >/dev/null 2>&1 || NODE="/mnt/c/Program Files/nodejs/node.exe"
  SCHWUNG_ROOT="$(wslpath -w "$ROOT")" WSLENV="SCHWUNG_ROOT" "$NODE" "$(wslpath -w "$MODULE/tests/test_ui.mjs")"
fi

echo "== Move build (arm64) + ABI"
if command -v "${CROSS_PREFIX:-aarch64-linux-gnu-}gcc" >/dev/null 2>&1; then
  bash "$MODULE/scripts/build.sh" arm64
  "${CROSS_PREFIX:-aarch64-linux-gnu-}gcc" -std=gnu11 -O2 -static -ffp-contract=off \
    -I"$MODULE/dsp" -I"$MODULE/tests" "$MODULE/tests/bench_device.c" "$MODULE/dsp/mg_engine.c" -lm \
    -o "$MODULE/build/bench-device-arm64"
  echo "built build/bench-device-arm64 (run it on the Move, see README)"
else
  echo "SKIP: no aarch64 cross compiler; ABI not checked" >&2
fi

echo "== licences"
python3 "$MODULE/scripts/check_licenses.py"
echo "ALL TESTS PASSED"
