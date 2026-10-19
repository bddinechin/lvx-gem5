#!/usr/bin/env bash
#
# Run the whole ISS check suite and fail if anything in it fails.
#
# This exists because nothing ran these.  There is no CI in this tree and no
# make target reached them, so run_diff.sh and the check_*.sh scripts were a
# thing you had to know about -- and in the week of 2026-10-13 three timing
# bugs reached the tree with twenty-odd assertions in those scripts passing
# over them, two of them found by another session's benchmark run rather than
# by this directory.  A suite nobody runs is documentation.
#
# Reached from the top level as `make check-iss`.
#
# The check_*.sh are discovered by glob rather than listed, so a new one is
# picked up without editing anything -- the alternative is a list that silently
# stops covering what it names.
#
# On which core each check runs.  Most check_*.sh default GEM5 to
# build/LVX/gem5.opt, which holds whichever core was built LAST -- the
# silent-wrong-core trap validation/run.sh warns about.  So this names the
# binary explicitly: the superset core by default, whose decoder runs the
# lvx-1 subset correctly, and additionally the lvx-1 build for the two checks
# where the core genuinely changes the answer (the timing model, and the
# compiled kernels).  run_diff.sh does both cores itself.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5_LVX1  GEM5_LVX2  HOSTCC  SKIP_DIFF=1
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

G1="${GEM5_LVX1:-$gem5root/build/gem5-lvx1.opt}"
G2="${GEM5_LVX2:-$gem5root/build/gem5-lvx2.opt}"

for f in "$G1" "$G2"; do
    [ -x "$f" ] || { echo "missing ISS binary: $f"; echo "  build it with: cd $gem5root && ./build-cores.sh"; exit 2; }
done

# The two whose result depends on which core built the binary.
BOTH_CORES="check_timing.sh check_kernel.sh"

pass=0; fail=0; failed=""
started=$(date +%s)

note() { printf '\n=== %s\n' "$*"; }

run_one() {   # run_one <script> <binary> <march> <label>
    local s="$1" bin="$2" march="$3" label="$4" log
    log="$(mktemp)"
    printf '%-34s ' "$label"
    if GEM5="$bin" MARCH="$march" "$here/$s" >"$log" 2>&1; then
        echo "PASS"; pass=$((pass+1))
    else
        echo "FAIL"; fail=$((fail+1)); failed="$failed $label"
        sed 's/^/    | /' "$log" | tail -20
    fi
    rm -f "$log"
}

if [ "${SKIP_DIFF:-0}" != 1 ]; then
    note "run_diff.sh -- 12 programs x 2 cores x 4 optimisation levels"
    if GEM5_LVX1="$G1" GEM5_LVX2="$G2" "$here/run_diff.sh" 2>&1 | tail -8; then
        pass=$((pass+1))
    else
        fail=$((fail+1)); failed="$failed run_diff.sh"
    fi
fi

note "check_*.sh"
for path in "$here"/check_*.sh; do
    s="$(basename "$path")"
    run_one "$s" "$G2" lvx-2 "$s (lvx-2)"
    case " $BOTH_CORES " in
      *" $s "*) run_one "$s" "$G1" lvx-1 "$s (lvx-1)" ;;
    esac
done

printf '\n----\n%d passed, %d failed, %ds\n' "$pass" "$fail" "$(( $(date +%s) - started ))"
if [ "$fail" -ne 0 ]; then
    echo "failed:$failed"
    echo "run_checks: FAIL"
    exit 1
fi
echo "run_checks: PASS"
