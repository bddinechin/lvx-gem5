#!/usr/bin/env bash
#
# Each level of the memory hierarchy costs what the description says it does.
#
# The description gives two load latencies and they describe two levels: `ld'
# writes at CR, 3 cycles after RR, which is an L1 hit; `ld.u' carries
# MemoryLevel=2 and writes at SR, 15 cycles, which is the next level reached
# without touching L1. Until 2026-10-21 the configuration had an L1 and then
# DRAM, so an L1 miss cost about 48 cycles where the architecture has
# something at 15 -- and the ISS asserted the 15 out of the description
# instead of producing it.
#
# With an L2 the architectural number falls out of the configuration, and this
# keeps the two in step. The assertion that matters is the middle one: a
# configuration whose second level has drifted away from SR - RR is no longer
# describing this machine, and every cycle count taken on it is answering a
# different question.
#
# Bounds rather than equalities. The exact figure depends on the cache
# parameters, which are a modelling choice; what is not a choice is the order
# of magnitude of each level and that the second one is near SR - RR.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
STAGES="$gem5root/src/arch/lvx/generated/lvx_stages.py"

for f in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG" "$STAGES"; do
    [ -e "$f" ] || { echo "missing: $f"; exit 2; }
done

val() { awk -v k="$1" '$1==k {print $3}' "$STAGES"; }
STAGE_RR=$(val LVX_STAGE_RR); STAGE_CR=$(val LVX_STAGE_CR)
STAGE_SR=$(val LVX_STAGE_SR)
L1_LAT=$(( STAGE_CR - STAGE_RR ))     # 3: the description's cached load
L2_LAT=$(( STAGE_SR - STAGE_RR ))     # 15: MemoryLevel=2

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
rc=0

# $1 = working-set bytes.  Prints
#   "<cycles an L1 miss costs when it hits L2> <L1 misses> <L2 hits> <L2 miss cycles>"
#
# The first is derived, not the raw dcache figure: every arm takes one cold
# pass over its working set, and those misses go all the way to DRAM, so the
# raw average is a blend of two levels. Taking the L2's own misses back out
# leaves the steady-state cost, which is the thing being compared with the
# description.
level() {
    local set=$1 tag="s$1"
    "$BIN/lvx-mbr-as" -march lvx-2 --defsym SET=$set "$here/timing_hierarchy.s" \
        -o "$work/$tag.o" || return 2
    "$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/$tag.o" \
        -o "$work/$tag.elf" || return 2
    LVX_CPU=minor timeout 600 "$GEM5" --outdir="$work/m5-$tag" "$RUNCFG" \
        "$work/$tag.elf" > /dev/null 2>&1 || return 2
    local s="$work/m5-$tag/stats.txt"
    awk '/^system\.cpu\.dcache\.demandAvgMissLatency::total/ {l=$2}
         /^system\.cpu\.dcache\.overallMisses::total/        {m=$2}
         /^system\.l2cache\.overallHits::total/              {h=$2}
         /^system\.l2cache\.overallMisses::total/            {lm=$2}
         /^system\.l2cache\.demandAvgMissLatency::total/     {ll=$2}
         END {
             hit = (h > 0) ? (l * (h + lm) - lm * ll) / h : l;
             printf "%d %d %d %d\n",
                 (hit + 500) / 1000, m, h, (ll + 500) / 1000;
         }' "$s"
}

report_between() {
    if [ "$2" -ge "$3" ] && [ "$2" -le "$4" ]; then
        printf '%-50s PASS (%s)\n' "$1" "$2"
    else
        printf '%-50s FAIL (got %s, want %s..%s)\n' "$1" "$2" "$3" "$4"; rc=1
    fi
}

# 1. A working set inside the L1 does not miss it at all.
set -- $(level 4096) || { echo "hierarchy: FAIL (L1-resident run)"; exit 2; }
report_between "an L1-resident set barely misses L1" "$2" 0 200

# 2. The one that matters: missing L1 and hitting L2 costs about what the
#    description gives a MemoryLevel=2 access.  +-4 cycles of the 15.
set -- $(level 65536) || { echo "hierarchy: FAIL (L2-resident run)"; exit 2; }
l2cost=$1
report_between "an L1 miss that hits L2 costs about SR-RR ($L2_LAT)" \
               "$l2cost" "$(( L2_LAT - 4 ))" "$(( L2_LAT + 4 ))"
report_between "and it really is hitting L2" "$3" 4000 8000

# 3. Past the L2 it costs a great deal more -- at least twice the L2, which is
#    what says the two levels are distinguishable at all.  Read from the L2's
#    own miss latency, which is that path exactly.
report_between "missing both costs at least twice that" "$4" "$(( 2 * l2cost ))" 1000

printf -- '----\nRR=%s CR=%s SR=%s; the description gives a cached load %s cycles and MemoryLevel=2 %s\n' \
    "$STAGE_RR" "$STAGE_CR" "$STAGE_SR" "$L1_LAT" "$L2_LAT"
[ $rc -eq 0 ] && echo "check_hierarchy: PASS" || echo "check_hierarchy: FAIL"
exit $rc
