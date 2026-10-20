#!/usr/bin/env bash
#
# The data cache must see the program's accesses.
#
# It saw none until 2026-10-20: the shim served every load and store
# functionally inside the behaviour, through a port proxy, so no request
# reached the hierarchy and every counter was zero however many accesses the
# program made. That is the failure this pins, and it is one a counter cannot
# report -- zero accesses and no plumbing look identical.
#
# Asserted as the *difference* between two runs of the same fixture, one doing
# twice the trips, which cancels whatever the startup and exit paths access.
# Each trip is one load and one store, so the difference is 2 * 500.
#
# Only the pipeline model has a load/store queue; the functional CPUs access
# memory through the proxy by design, so this is a `minor' test only.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
TRIPS=500           # must match .equ TRIPS in dcache.s (the un-doubled arm)

for f in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$f" ] || { echo "missing: $f"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
rc=0

report() {
    if [ "$2" = "$3" ]; then printf '%-46s PASS (%s)\n' "$1" "$2"
    else printf '%-46s FAIL (got %s, want %s)\n' "$1" "$2" "$3"; rc=1; fi
}

# $1 = tag, rest = extra as(1) arguments.  Prints "<code> <accesses> <hits> <misses>".
run() {
    local tag="$1"; shift
    "$BIN/lvx-mbr-as" -march lvx-2 "$@" "$here/dcache.s" -o "$work/$tag.o" \
        || return 2
    "$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/$tag.o" \
        -o "$work/$tag.elf" || return 2
    local out code
    out="$(LVX_CPU=minor timeout 300 "$GEM5" --outdir="$work/m5-$tag" \
            "$RUNCFG" "$work/$tag.elf" 2>&1)" || return 2
    code="$(printf '%s' "$out" | sed -n 's/.*code=\([0-9]*\).*/\1/p' | head -1)"
    local stats="$work/m5-$tag/stats.txt" a h m
    a="$(awk '/^system\.cpu\.dcache\.overallAccesses::total/ {print $2}' "$stats")"
    h="$(awk '/^system\.cpu\.dcache\.overallHits::total/     {print $2}' "$stats")"
    m="$(awk '/^system\.cpu\.dcache\.overallMisses::total/   {print $2}' "$stats")"
    echo "${code:-x} ${a:-0} ${h:-0} ${m:-0}"
}

one="$(run one)"            || { echo "dcache: the single run failed"; exit 2; }
two="$(run two --defsym DOUBLE=1)" \
                            || { echo "dcache: the doubled run failed"; exit 2; }
set -- $one; code1=$1 acc1=$2 hit1=$3 miss1=$4
set -- $two; code2=$1 acc2=$2

report "the kernel computes its own answer" "$code1,$code2" "0,0"
report "the cache sees the accesses at all" \
       "$([ "${acc1:-0}" -gt 0 ] && echo yes || echo no)" "yes"
report "twice the trips is twice the accesses" \
       "$(( acc2 - acc1 ))" "$(( 2 * TRIPS ))"
report "every access hit or missed, nothing else" \
       "$(( hit1 + miss1 ))" "$acc1"

printf -- '----\naccesses %s (hits %s, misses %s); doubled run %s\n' \
    "$acc1" "$hit1" "$miss1" "$acc2"
[ $rc -eq 0 ] && echo "check_dcache: PASS" || echo "check_dcache: FAIL"
exit $rc
