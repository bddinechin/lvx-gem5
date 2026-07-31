#!/usr/bin/env bash
#
# f32 + f16 ISS check, run against BOTH LVX cores.
#
# The f32/f16 operator helpers share the width-macro implementation in the
# core-agnostic gem5 shim (shim_fp.cc), so fp32_cmp.s and fp16_cmp.s must pass on
# each core. For each (program, core) we assemble with the matching -march, link
# with the shared crt0, run under the per-core gem5 binary, and require exit code
# 0; a nonzero code is the first failing case in that program.
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

run_one() {
    local core="$1" march="$2" gem5="$3" prog="$4"
    [ -e "$gem5" ] || { echo "$core/$prog  SKIP (missing $gem5)"; return 2; }
    "$BIN/lvx-mbr-as" -march="$march" "$here/crt0_mbr.S" -o "$work/crt0.o" \
        || { echo "$core/$prog  FAIL (crt0 assemble)"; return 1; }
    "$BIN/lvx-mbr-as" -march="$march" "$here/$prog.s" -o "$work/p.o" \
        || { echo "$core/$prog  FAIL (assemble)"; return 1; }
    "$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/p.o" -o "$work/p.elf" \
        || { echo "$core/$prog  FAIL (link)"; return 1; }
    local out code
    out="$(timeout 120 "$gem5" --outdir="$work/m5" "$RUNCFG" "$work/p.elf" 2>&1)"
    code="$(printf '%s' "$out" | grep -oE 'code=-?[0-9]+' | grep -oE '\-?[0-9]+' | head -1)"
    if [ "${code:-x}" = "0" ]; then
        echo "$core/$prog  PASS"
        return 0
    elif [ -z "${code:-}" ]; then
        echo "$core/$prog  FAIL (no exit code)"
        printf '%s\n' "$out" | grep -oiE 'panic|fatal|illegal instruction|timed out' | head -1
        return 1
    else
        echo "$core/$prog  FAIL (case $code)"
        return 1
    fi
}

for prog in fp32_cmp fp16_cmp fast_cmp; do
    run_one lvx_v1 lvx-1 "${GEM5_LVX1:-$gem5root/build/gem5-lvx1.opt}" "$prog" || rc=1
    run_one lvx_v2 lvx-2 "${GEM5_LVX2:-$gem5root/build/gem5-lvx2.opt}" "$prog" || rc=1
done
exit $rc
