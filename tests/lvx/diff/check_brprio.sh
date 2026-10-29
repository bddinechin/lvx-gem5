#!/usr/bin/env bash
#
# Branch priority within one bundle: BCU0 wins over BCU1.
#
# A bundle has two BCU slots, so two taken branches in one bundle is a legal
# bundle, and the architecture resolves it by slot: the first slot that
# transfers is the one that does, conditional or not.
#
# finishBundle scanned every syllable for a written NPC and kept assigning
# with no break, so the *last* writer won -- BCU1 -- and a bundle with two
# taken branches went to the wrong target.  One taken branch in either slot
# was already right, which is why nothing caught it: it needs both slots
# taken at once, and GCC's bundler puts at most one transfer in a bundle, so
# no compiled code emits one.  No reason for the ISS to disagree with the
# architecture.
#
# The five cases, and the wrong answer each one is built to catch:
#   1,2  one slot taken, the other not -- the priority must be over the slots
#        that TRANSFER, not the slots that exist.  These catch the
#        over-correction, "BCU0 wins whenever BCU0 is a branch": case 2 then
#        falls through (a not-taken branch's context holds fall-through) and
#        exits 3.
#   3    two taken conditional branches -- the bug itself, exit 6.
#   4,5  the same with one slot unconditional, the "conditional or not" half
#        of the rule, exits 8 and 10.
# Each verified by mutation: cases 3/4/5 each catch a no-break finishBundle on
# their own, case 2 catches the over-correction.
#
# Run under both CPU models: they drive execute() differently, though
# finishBundle is shared.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"

for f in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$f" ] || { echo "missing: $f"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
"$BIN/lvx-mbr-as" -march lvx-2 "$here/brprio.s" -o "$work/bp.o" || exit 2
"$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/bp.o" -o "$work/bp.elf" || exit 2

names=(. "BCU0 taken beside a not-taken BCU1: fell through" \
       "a not-taken BCU1 branch transferred" \
       "BCU1 taken beside a not-taken BCU0: fell through" \
       "a not-taken BCU0 branch transferred" \
       "two taken branches: fell through" \
       "two taken branches: BCU1 won, BCU0 has priority" \
       "taken BCU1 beside an unconditional BCU0: fell through" \
       "BCU1's conditional beat BCU0's goto" \
       "taken BCU0 beside an unconditional BCU1: fell through" \
       "BCU1's goto beat BCU0's taken branch")

rc=0
for cpu in atomic minor; do
    out="$(LVX_CPU="$cpu" timeout 120 "$GEM5" --outdir="$work/m5-$cpu" \
            "$RUNCFG" "$work/bp.elf" 2>&1)"
    code="$(printf '%s' "$out" | sed -n 's/.*code=\([0-9]*\).*/\1/p' | head -1)"
    if [ "${code:-x}" = 0 ]; then
        printf '%-40s PASS\n' "$cpu: all five cases"
    else
        printf '%-40s FAIL (exit %s: %s)\n' "$cpu: all five cases" \
            "${code:-<none>}" "${names[${code:-0}]:-unknown}"
        rc=1
    fi
done

[ $rc -eq 0 ] && echo "check_brprio: PASS" || echo "check_brprio: FAIL"
exit $rc
