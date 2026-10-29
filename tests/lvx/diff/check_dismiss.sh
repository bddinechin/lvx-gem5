#!/usr/bin/env bash
#
# The Dismissible load variants: `.s' and `.us' do not fault, and a fault
# becomes all-zero loaded bits.
#
# `Modifier@properties' gives `%0.US: MemoryLevel=2;Dismissible;LoadedAt=SR'.
# The ISS modelled LoadedAt as the functional unit's latency and MemoryLevel
# as no-allocation in the L1 (check_noalloc.sh); this is the third half.
# Dismissible is what lets a compiler hoist a load above the test that guards
# it, the value being consumed only when the guard passes.
#
# The half that makes it mean anything is the opposite one, so it is asserted
# too: a *non*-Dismissible load must still fault. It did not. The port
# returned NoFault from initiateAcc unconditionally, which is Minor's signal
# that the instruction suppressed a translation fault on purpose
# (Execute::executeMemRefInst -- the hook exists for architectures with
# non-faulting loads), so every page-table fault in the ISS was swallowed and
# a load from an unmapped address merely left its destination alone. `ld' and
# `ld.s' behaved identically, and both behaved wrongly.
#
# Both CPU models, because the two reach memory by different routes -- the
# pipeline through the LSQ, the functional models through a port proxy whose
# readBlob is fatal -- and Dismissible has to hold on both.
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
rc=0

report() {
    if [ "$2" = "$3" ]; then printf '%-46s PASS (%s)\n' "$1" "$2"
    else printf '%-46s FAIL (got %s, want %s)\n' "$1" "$2" "$3"; rc=1; fi
}

# $1 = tag, $2 = CPU model, rest = extra as(1) arguments.
# Prints the program's exit code, or `faulted' if the run died instead.
run() {
    local tag="$1" cpu="$2"; shift 2
    "$BIN/lvx-mbr-as" -march lvx-2 "$@" "$here/dismiss.s" -o "$work/$tag.o" \
        || return 2
    "$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/$tag.o" \
        -o "$work/$tag.elf" || return 2
    local out code
    out="$(LVX_CPU="$cpu" timeout 120 "$GEM5" --outdir="$work/m5-$tag" \
            "$RUNCFG" "$work/$tag.elf" 2>&1)"
    code="$(printf '%s' "$out" | sed -n 's/.*code=\([0-9]*\).*/\1/p' | head -1)"
    if [ -n "$code" ]; then
        echo "$code"
    elif printf '%s' "$out" | grep -qiE 'page table fault|readBlob.*failed'; then
        echo faulted
    else
        echo other
    fi
}

for cpu in atomic minor; do
    # Exit codes: 0 all five checks passed; 1 `.s' from the unmapped address
    # was not zero; 2 the same for `.us'; 3/4 a mapped address disagreed with
    # a plain `ld'; 5 the mapped value was itself zero, which would have let
    # 3 and 4 pass vacuously.
    ok="$(run ok-$cpu "$cpu")" \
        || { echo "dismiss: the $cpu run failed to build"; exit 2; }
    report "$cpu: .s/.us dismiss a fault, and read zero" "$ok" "0"

    # And the discrimination. The fixture's FAULT arm exits 7 if the fault
    # vanished, so `faulted' is the pass and any exit code is the failure.
    bad="$(run bad-$cpu "$cpu" --defsym FAULT=1)" \
        || { echo "dismiss: the $cpu FAULT run failed to build"; exit 2; }
    report "$cpu: a plain ld still faults" "$bad" "faulted"
done

[ $rc -eq 0 ] && echo "check_dismiss: PASS" || echo "check_dismiss: FAIL"
exit $rc
