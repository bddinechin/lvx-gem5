#!/usr/bin/env bash
#
# XVR vector-register-file ISS check for lvx_v2 (standalone).
#
# gcc has no lvx-2 codegen, so this cannot go through the C native<->ISS diff
# harness. Instead xvr_move.s is a self-checking lvx-2 assembly program: it moves
# known values into the XVR/XCR vector registers and back (xmovetd/xputqo ->
# xmovefo/xmovefd) and returns 0 iff every lane round-tripped. We assemble it
# with -march=lvx-2, link with the shared crt0, run under gem5-lvx2, and require
# exit code 0 -- which exercises the vector register model (arch/lvx/regs/vec.hh)
# and the shim's XVR helpers end to end.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5 (must be an lvx_v2 build)  LVX_CPU(atomic|timing|minor)
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/gem5-lvx2.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
export LVX_CPU="${LVX_CPU:-atomic}"

for tool in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

"$BIN/lvx-mbr-as" -march=lvx-2 "$here/crt0_mbr.S"  -o "$work/crt0.o"     || { echo "crt0 assemble failed"; exit 2; }
"$BIN/lvx-mbr-as" -march=lvx-2 "$here/xvr_move.s"  -o "$work/xvr.o"      || { echo "xvr_move assemble failed"; exit 2; }
"$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/xvr.o"     -o "$work/xvr.elf"    || { echo "link failed"; exit 2; }

out="$(timeout 120 "$GEM5" --outdir="$work/m5" "$RUNCFG" "$work/xvr.elf" 2>&1)"
code="$(printf '%s' "$out" | grep -oE 'code=-?[0-9]+' | grep -oE '\-?[0-9]+' | head -1)"

if [ "${code:-x}" = "0" ]; then
    echo "XVR ISS check  PASS (xmovetd/xputqo/xmovefo/xmovefd round-trip)"
    exit 0
elif [ -z "${code:-}" ]; then
    echo "XVR ISS check  FAIL (no exit code from gem5)"
    printf '%s\n' "$out" | grep -oiE 'panic|fatal|Illegal instruction|timed out' | head -1
    exit 1
else
    echo "XVR ISS check  FAIL (code=$code: a lane did not round-trip)"
    exit 1
fi
