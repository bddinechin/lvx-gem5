#!/usr/bin/env bash
#
# A hardware loop's trip costs its bundles, whatever its length and wherever
# the body sits.
#
# LOOPDO's back edge is the fetch engine's, and the architecture says more than
# that: LOOPDO invalidates a prefetch buffer ((EFFECT.ID.invalpfb) in
# Instruction.yml), so the body is replayed from it and the back edge costs no
# fetch. An ISS that re-fetches the body every trip pays for that twice, and on
# 2026-10-21 it did:
#
#   * bodies of 2, 3 and 4 bundles all cost 4.09 cycles a trip, 5 and 6 cost
#     6.12, and only from 7 up did a trip cost its bundles -- a floor set by
#     the pipeline between Fetch2 deciding the redirect and Execute issuing
#     the target;
#   * the same 4-bundle body cost 4.07 inside one 64-byte line and 6.09
#     across two.
#
# Together those cost lvx-mlir's software-pipelined VEC=4 `mymma' its entire
# gain: 6.03 cycles a trip against its scheduler's 4, so pipelining it looked
# worthless (docs/handoff-iss-lvx-mlir-findings.md, issues 1 and 4 -- they are
# one bug). Aligning the body by hand recovered it exactly, 3.97.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
TRIPS=2000      # must match .equ TRIPS in timing_hwloop_fetch.s

for f in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$f" ] || { echo "missing: $f"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
rc=0

# hundredths of a cycle per trip, and the lines the body spans.
# $1 = bundles, $2 = pad.  Prints "<hundredths> <lines>".
run() {
    local n=$1 pad=$2 tag="b$1p$2"
    "$BIN/lvx-mbr-as" -march lvx-2 --defsym BUNDLES=$n --defsym PAD=$pad \
        "$here/timing_hwloop_fetch.s" -o "$work/$tag.o" || return 2
    "$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/$tag.o" \
        -o "$work/$tag.elf" || return 2
    local a e c
    a=$("$BIN/lvx-mbr-objdump" -t "$work/$tag.elf" | awk '$NF=="Lstart"{print "0x"$1}')
    e=$("$BIN/lvx-mbr-objdump" -t "$work/$tag.elf" | awk '$NF=="Lend"{print "0x"$1}')
    c=$(LVX_CPU=minor timeout 300 "$GEM5" --outdir="$work/m5-$tag" "$RUNCFG" \
            "$work/$tag.elf" 2>&1 | sed -n 's/.*(\([0-9]*\) cycles).*/\1/p')
    [ -n "$c" ] || return 2
    # The setup before the loop is a handful of bundles; it is the same for
    # every arm and divided by 2000 trips it cannot move a hundredth.
    echo "$(( c * 100 / TRIPS )) $(( (e - 1) / 64 - a / 64 + 1 ))"
}

report() {
    if [ "$2" = "$3" ]; then printf '%-48s PASS (%s)\n' "$1" "$2"
    else printf '%-48s FAIL (got %s, want %s)\n' "$1" "$2" "$3"; rc=1; fi
}

# 1. a trip costs its bundles, at every length.  Rounded to whole cycles: the
#    loop setup is a few bundles spread over 2000 trips, worth a hundredth or
#    two, and every effect this guards against is whole cycles.
for n in 2 3 4 5 6 7 8; do
    set -- $(run $n 0) || { echo "hwloop-fetch: FAIL ($n bundles did not run)"; exit 2; }
    report "a $n-bundle body costs $n cycles a trip" "$(( ($1 + 50) / 100 ))" "$n"
done

# 2. and it costs the same wherever the body sits.  PAD=52 pushes a 4-bundle
#    body (16 bytes) across a 64-byte boundary; PAD=0 leaves it inside one.
set -- $(run 4 0);  inside=$1  l_in=$2
set -- $(run 4 52); across=$1  l_ac=$2
report "PAD=0 keeps the body in one line" "$l_in" 1
report "PAD=52 spans two lines" "$l_ac" 2
report "the body costs the same either way" \
       "$(( (across + 50) / 100 ))" "$(( (inside + 50) / 100 ))"

printf -- '----\n4-bundle body: %d.%02d cycles a trip in one line, %d.%02d across two\n' \
    $((inside/100)) $((inside%100)) $((across/100)) $((across%100))
[ $rc -eq 0 ] && echo "check_hwloop_fetch: PASS" || echo "check_hwloop_fetch: FAIL"
exit $rc
