#!/usr/bin/env bash
#
# The MemoryLevel=2 load variants do not allocate in the L1, but do read a
# line that is already there.
#
# `Modifier@properties' gives `%0.U: MemoryLevel=2;LoadedAt=SR' and until
# 2026-10-29 only the LoadedAt half reached the ISS, as the functional unit's
# latency. So a `.u' load was charged fifteen cycles for a round trip past the
# first level and then served by it -- and it allocated, evicting the lines it
# exists to spare. The handoff's issue 2.
#
# Two things are asserted, and the second is why this is not just a counter
# test. `.u' is not uncacheable: the bypass path snoops the L1, so an uncached
# load must see an earlier cached store to the same line (Modifier.yml -- the
# PCR_L1CE bit is coherency against other agents, not against the core's own
# stores). Marking the request Request::UNCACHEABLE, or declining the lookup
# rather than only the allocation, returns memory's stale value and would pass
# every allocation assertion while doing it. noalloc.s checks the values
# itself and reports through its exit code; this script checks the counters.
#
# The allocation half is stated as differences between four arms of one
# fixture rather than as absolutes, so the prologue's own accesses and part
# 1's cancel. It takes two passes over the lines, because allocation is only
# observable on a miss: a hit does not fill, so it never asks whether to
# allocate, and a loop hammering one resident line counts the same for every
# variant. The first pass misses LINES times whatever the variant; the second
# pass is the measurement -- zero for a variant that allocates, LINES again
# for one that does not.
#
# Only the pipeline model routes accesses through the load/store queue, so
# only `minor' reaches the hierarchy at all -- but the value checks are
# functional, so they are worth running on both.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
LINES=64            # must match .equ LINES in noalloc.s

for f in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$f" ] || { echo "missing: $f"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
rc=0

report() {
    if [ "$2" = "$3" ]; then printf '%-46s PASS (%s)\n' "$1" "$2"
    else printf '%-46s FAIL (got %s, want %s)\n' "$1" "$2" "$3"; rc=1; fi
}

# $1 = tag, $2 = CPU model, rest = extra as(1) arguments.
# Prints "<exit code> <dcache misses> <l2 hits>".
run() {
    local tag="$1" cpu="$2"; shift 2
    "$BIN/lvx-mbr-as" -march lvx-2 "$@" "$here/noalloc.s" -o "$work/$tag.o" \
        || return 2
    "$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/$tag.o" \
        -o "$work/$tag.elf" || return 2
    local out code
    out="$(LVX_CPU="$cpu" timeout 300 "$GEM5" --outdir="$work/m5-$tag" \
            "$RUNCFG" "$work/$tag.elf" 2>&1)" || return 2
    code="$(printf '%s' "$out" | sed -n 's/.*code=\([0-9]*\).*/\1/p' | head -1)"
    local stats="$work/m5-$tag/stats.txt" m l
    m="$(awk '/^system\.cpu\.dcache\.overallMisses::total/   {print $2}' "$stats")"
    l="$(awk '/^system\.l2cache\.overallAccesses::total/     {print $2}' "$stats")"
    echo "${code:-x} ${m:-0} ${l:-0}"
}

base_="$(run base minor --defsym NOLOOP=1)" \
                              || { echo "noalloc: the baseline run failed"; exit 2; }
cached="$(run cached minor)"  || { echo "noalloc: the plain-ld run failed"; exit 2; }
unc="$(run unc minor --defsym UNCACHED=1)" \
                              || { echo "noalloc: the .u run failed"; exit 2; }
spec="$(run spec minor --defsym SPECUL=1)" \
                              || { echo "noalloc: the .us run failed"; exit 2; }
atom="$(run atom atomic --defsym UNCACHED=1)" \
                              || { echo "noalloc: the atomic run failed"; exit 2; }

set -- $base_;  bcode=$1 base=$2
set -- $cached; ccode=$1 cmiss=$2
set -- $unc;    ucode=$1 umiss=$2 ul2=$3
set -- $spec;   scode=$1 smiss=$2
set -- $atom;   acode=$1

# The correctness half: every variant read back what had just been stored to
# the same line.  1 = .u missed the cached store, 2 = .us did, 3 = two .u
# loads of one address disagreed, 4 = .u read a stale value.
report "a .u/.us load still reads a cached store" \
       "$ucode,$scode" "0,0"
report "and does on the functional path too" "$acode" "0"
report "the cached arm agrees (same values, plain ld)" "$ccode" "0"

# The baseline arm skips the loop entirely, so `cmiss - base' is the plain
# variant's two passes and `umiss - cmiss' is what the bypass adds.
report "the baseline arm agrees too" "$bcode" "0"
report "a plain ld allocates, so its second pass hits" \
       "$(( cmiss - base ))" "$LINES"
report "a .u load allocates nothing, so both passes miss" \
       "$(( umiss - cmiss ))" "$LINES"
report ".us behaves as .u does" "$smiss" "$umiss"
report "and those accesses reached the L2, not DRAM" \
       "$([ "${ul2:-0}" -ge "$(( 2 * LINES ))" ] && echo yes || echo no)" "yes"

printf -- '----\nD-cache misses: no loop %s, ld %s, ld.u %s, ld.us %s (LINES %s); L2 accesses %s\n' \
    "$base" "$cmiss" "$umiss" "$smiss" "$LINES" "$ul2"
[ $rc -eq 0 ] && echo "check_noalloc: PASS" || echo "check_noalloc: FAIL"
exit $rc
