#!/usr/bin/env bash
#
# CCB fused-compare-and-branch ISS check, run against BOTH LVX cores.
#
# ccb/ccbx exist in lvx_v1 and lvx_v2, and both ISS executables link the same
# hand-written Behavior_ccbcomp shim helper, so the one self-checking program
# (ccb_cmp.s) must pass on each.  We assemble it with the matching -march, link
# with the shared crt0, run under the per-core gem5 binary, and require exit
# code 0.  A nonzero code is the index (1..10) of the first ccbcomp case whose
# branch decision was wrong -- see ccb_cmp.s.
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
    "$BIN/lvx-mbr-as" -march="$march" "$here/ccb_cmp.s" -o "$work/ccb.o" \
        || { echo "$core  FAIL (ccb_cmp assemble)"; return 1; }
    "$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/ccb.o" -o "$work/ccb.elf" \
        || { echo "$core  FAIL (link)"; return 1; }
    local out code
    out="$(timeout 120 "$gem5" --outdir="$work/m5" "$RUNCFG" "$work/ccb.elf" 2>&1)"
    code="$(printf '%s' "$out" | grep -oE 'code=-?[0-9]+' | grep -oE '\-?[0-9]+' | head -1)"
    if [ "${code:-x}" = "0" ]; then
        echo "$core  PASS (all 10 ccbcomp cases)"
        return 0
    elif [ -z "${code:-}" ]; then
        echo "$core  FAIL (no exit code from gem5)"
        printf '%s\n' "$out" | grep -oiE 'panic|fatal|illegal instruction|timed out' | head -1
        return 1
    else
        echo "$core  FAIL (case $code branched wrong)"
        return 1
    fi
}

run_core lvx_v1 lvx-1 "${GEM5_LVX1:-$gem5root/build/gem5-lvx1.opt}" || rc=1
run_core lvx_v2 lvx-2 "${GEM5_LVX2:-$gem5root/build/gem5-lvx2.opt}" || rc=1
exit $rc
