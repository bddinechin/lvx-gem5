#!/usr/bin/env bash
#
# f64 conversion ISS check, run against BOTH LVX cores.
#
# The f64<->integer and f64<->f32 conversion helpers are implemented in the
# core-agnostic gem5 shim (shim_fp.cc, over Berkeley SoftFloat with the RISC-V
# specialization), so the one self-checking program (conv_cmp.s) must pass on
# each. Assemble with the matching -march, link with the shared crt0, run under
# the per-core gem5 binary, and require exit code 0. A nonzero code is the index
# (1..13) of the first conversion whose result was wrong -- see conv_cmp.s.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5_LVX1  GEM5_LVX2  LVX_CPU(atomic|timing|minor)
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
export LVX_CPU="${LVX_CPU:-atomic}"

for tool in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$RUNCFG"; do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
rc=0

run_core() {
    local core="$1" march="$2" gem5="$3"
    [ -e "$gem5" ] || { echo "$core  SKIP (missing $gem5)"; return 2; }
    "$BIN/lvx-mbr-as" -march="$march" "$here/crt0_mbr.S" -o "$work/crt0.o" \
        || { echo "$core  FAIL (crt0 assemble)"; return 1; }
    "$BIN/lvx-mbr-as" -march="$march" "$here/conv_cmp.s" -o "$work/conv.o" \
        || { echo "$core  FAIL (conv_cmp assemble)"; return 1; }
    "$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/conv.o" -o "$work/conv.elf" \
        || { echo "$core  FAIL (link)"; return 1; }
    local out code
    out="$(timeout 120 "$gem5" --outdir="$work/m5" "$RUNCFG" "$work/conv.elf" 2>&1)"
    code="$(printf '%s' "$out" | grep -oE 'code=-?[0-9]+' | grep -oE '\-?[0-9]+' | head -1)"
    if [ "${code:-x}" = "0" ]; then
        echo "$core  PASS (all 13 f64 conversions)"
        return 0
    elif [ -z "${code:-}" ]; then
        echo "$core  FAIL (no exit code from gem5)"
        printf '%s\n' "$out" | grep -oiE 'panic|fatal|illegal instruction|timed out' | head -1
        return 1
    else
        echo "$core  FAIL (case $code result mismatch)"
        return 1
    fi
}

run_core lvx_v1 lvx-1 "${GEM5_LVX1:-$gem5root/build/gem5-lvx1.opt}" || rc=1
run_core lvx_v2 lvx-2 "${GEM5_LVX2:-$gem5root/build/gem5-lvx2.opt}" || rc=1
exit $rc
