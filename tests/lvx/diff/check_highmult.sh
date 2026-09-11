#!/usr/bin/env bash
#
# Targeted highmult ISS check (standalone; complements run_diff.sh).
#
# muld / muld.h / muld.hu / muld.hsu have no x86 equivalent, so they cannot go
# through the native<->ISS diff harness. Instead highmult.c is self-checking: it
# runs the four variants on known operands and compares against precomputed
# 128-bit products, returning 0 on success. We build it freestanding, link with
# the shared crt0, run it under gem5 SE-mode, and require exit code 0.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5  LVX_CPU(atomic|timing|minor)
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
# The ISS to run on. These build with the toolchain's default -march, which
# is lvx-1, so the lvx-1 simulator is the one that matches. The default used
# to be build/LVX/gem5.opt -- a single unnamed executable that is in fact the
# lvx-2 build, so these ran lvx-1 programs on the lvx-2 ISS and nothing said
# so. GEM5 still overrides, for chasing one core deliberately.
GEM5="${GEM5:-$gem5root/build/gem5-lvx1.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
export LVX_CPU="${LVX_CPU:-atomic}"

for tool in "$BIN/lvx-mbr-gcc" "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

"$BIN/lvx-mbr-as" "$here/crt0_mbr.S" -o "$work/crt0.o"                       || { echo "crt0 assemble failed"; exit 2; }
"$BIN/lvx-mbr-gcc" -c -O2 -ffreestanding -fno-builtin -nostdlib \
    "$here/highmult.c" -o "$work/hm.o"                                       || { echo "highmult build failed"; exit 2; }
"$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/hm.o" -o "$work/hm.elf"              || { echo "link failed"; exit 2; }

out="$(timeout 120 "$GEM5" --outdir="$work/m5" "$RUNCFG" "$work/hm.elf" 2>&1)"
code="$(printf '%s' "$out" | grep -oE 'exited \(code=[0-9]+\)' | grep -oE '[0-9]+' | head -1)"

names=(muld muld.h muld.hu muld.hsu)
if [ "${code:-x}" = "0" ]; then
    echo "highmult ISS check  PASS (all 4 muld variants correct)"
    exit 0
elif [ -z "${code:-}" ]; then
    echo "highmult ISS check  FAIL (no exit code from gem5)"
    printf '%s\n' "$out" | grep -oiE 'panic|fatal|Illegal instruction|timed out' | head -1
    exit 1
else
    echo "highmult ISS check  FAIL (code=$code, bitmask of wrong variants):"
    for i in 0 1 2 3; do (( code & (1<<i) )) && echo "  - ${names[$i]} mismatched"; done
    exit 1
fi
