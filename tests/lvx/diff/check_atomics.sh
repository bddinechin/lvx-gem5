#!/usr/bin/env bash
#
# Targeted atomic read-modify-write check (standalone; complements run_diff.sh).
#
# The ISS implemented exactly one atomic until 2026-09-11 -- MEM_atomic_cas --
# and left the other twelve helpers as lvx_behavior_unimpl() panic stubs, so the
# whole AL and AS family aborted the simulation rather than running.  This covers
# them: add, and, ior, eor, min, max, minu, maxu, dus, swap, and the plain atomic
# load and store.
#
# Each case checks BOTH halves -- the previous value returned into the operand
# register, and what is left in memory.  The return alone would miss a store of
# the wrong thing; memory alone would miss the return, which is the half the AL
# forms exist for.
#
# The signed/unsigned byte pairs (cases 6 and 7) are the ones worth having: MIN
# and MAX order their operands as signed AT THE ACCESS WIDTH, so a byte 0xff is
# -1 and not 255.  An implementation that sign-extends from the wrong width, or
# not at all, agrees with this test on every non-negative input and differs here.
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

for tool in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
"$BIN/lvx-mbr-as" "$here/crt0_mbr.S" -o "$work/crt0.o" || { echo "crt0 assemble failed"; exit 2; }

run() {   # run <name> <source>  -- echoes the exit code, or empty
    "$BIN/lvx-mbr-as" "$2" -o "$work/$1.o"                          || return 2
    "$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/$1.o" -o "$work/$1.elf" || return 2
    timeout 120 "$GEM5" --outdir="$work/m5-$1" "$RUNCFG" "$work/$1.elf" 2>&1 |
      grep -oE 'exited \(code=[0-9]+\)' | grep -oE '[0-9]+' | head -1
}

rc=0
names=(. "1: aladdd"        "2: alandd/aliord"  "3: aleord" \
         "4: almind signed" "5: alminud"        "6: almaxb signed at byte width" \
         "7: almaxub"       "8: aldusd saturating" "9: aswapd" "10: ald/asd")

code="$(run atomics "$here/atomics.s")" || { echo "atomics       FAIL (build)"; exit 2; }
case "${code:-x}" in
  0) echo "atomics        PASS (all ten operations, value returned and value stored)" ;;
  [1-9]|10) echo "atomics        FAIL (case ${names[$code]})"; rc=1 ;;
  x) echo "atomics        FAIL (no exit code -- a helper is still a panic stub?)"; rc=1 ;;
  *) echo "atomics        FAIL (unexpected exit code $code)"; rc=1 ;;
esac

# Negative control: a test whose expectations cannot fail is worth nothing.
# Expect the SIGNED minimum where the unsigned one is computed, and require a
# failure -- case 5 is alminud, whose answer is 1, so demanding -1 must be caught.
sed '0,/maked \$r7 = 1$/!b; /## case 5/,/ccb.dne \$r4, \$r7 ? fail/s/maked \$r7 = 1$/maked $r7 = -1/' \
    "$here/atomics.s" > "$work/atomics_neg.s"
cmp -s "$here/atomics.s" "$work/atomics_neg.s" &&
    { echo "negative-ctrl  FAIL (the mutation did not apply)"; exit 2; }
code="$(run atomics_neg "$work/atomics_neg.s")" || { echo "negative-ctrl  FAIL (build)"; exit 2; }
if [ "${code:-x}" = "0" ]; then
    echo "negative-ctrl  FAIL (expectations are not being checked)"; rc=1
else
    echo "negative-ctrl  PASS (a wrong expectation is rejected, at case ${code})"
fi

exit $rc
