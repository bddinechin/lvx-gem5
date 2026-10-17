#!/usr/bin/env bash
#
# The decoder caches decoded bundles by their syllables (2026-10-17), so one
# LvxStaticInst is shared by every address those bytes appear at.  Decoding a
# bundle is not cheap -- split by steering, reassemble IMMX, run the generated
# operand decode per instruction, build the bundle's register lists -- and it
# used to be redone on every fetch, so a loop paid for its body's decode every
# trip.  Caching it is worth 1.14-1.28x of host simulation speed.
#
# It is sound only because nothing in a decoded bundle is absolute: each
# sub-instruction holds its byte offset *within* the bundle, and the PC comes
# from the execution context, so a PC-relative branch resolves against
# wherever it is executing.  decode_cache.s is two byte-identical `goto'
# bundles at different addresses, each of which must reach its own target:
#
#   10004:  02 00 00 10   goto 1000c
#   10010:  02 00 00 10   goto 10018
#
# $r0 counts the landings, so the exit code is 2 when both are right.  Were a
# decoded bundle ever to bake in an address, the second goto would jump to the
# first's target: one increment, or a loop.  The decode cache cannot be
# checked by the differential harness, which compares against x86 and so
# cannot say *which* LVX address a branch reached.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5  LVX_CPU(atomic|timing|minor)
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
export LVX_CPU="${LVX_CPU:-atomic}"

for f in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$f" ] || { echo "missing: $f"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
"$BIN/lvx-mbr-as" "$here/decode_cache.s" -o "$work/dc.o"            || exit 2
"$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib \
    "$work/dc.o" -o "$work/dc.elf"                                  || exit 2

rc=0

# The premise: if the assembler ever stopped emitting the same bytes for the
# two branches, the test would still pass while checking nothing.
same="$("$BIN/lvx-mbr-objdump" -d "$work/dc.elf" \
        | sed -n 's/^ *1000[48]:\t\([0-9a-f ]*\)\t.*goto.*/\1/p;
                  s/^ *10010:\t\([0-9a-f ]*\)\t.*goto.*/\1/p' \
        | sort -u | wc -l)"
if [ "$same" = 1 ]; then
    echo "the two goto bundles are byte-identical     PASS"
else
    echo "the two goto bundles are byte-identical     FAIL (they differ;"
    echo "  the test no longer exercises a shared decode, fix the fixture)"
    rc=1
fi

out="$(timeout 120 "$GEM5" --outdir="$work/m5" "$RUNCFG" "$work/dc.elf" 2>&1)"
code="$(printf '%s' "$out" | sed -n 's/.*code=\([0-9]*\).*/\1/p' | head -1)"
if [ "$code" = 2 ]; then
    echo "each reaches its own target                 PASS"
else
    echo "each reaches its own target                 FAIL (exit ${code:-<none>}, want 2)"
    rc=1
fi

[ $rc -eq 0 ] && echo "check_decode_cache: PASS" || echo "check_decode_cache: FAIL"
exit $rc
