#!/usr/bin/env bash
#
# A kernel of real size, under the pipeline model.
#
# This is the gap three timing bugs walked through in one week while twenty-odd
# timing assertions passed over them: every other test here is either a micro
# test or runs the atomic CPU, so nothing exercised MinorCPU on code of any
# size.  One of the three was an outright deadlock, found by somebody else's
# PolyBench run after 1h19m rather than by this suite.  See kernel.c for the
# shape and why each part of it is there.
#
# What it asserts, and deliberately only this:
#
#   1. the kernel COMPLETES under minor, within a timeout.  The deadlock class
#      needs nothing cleverer, and would have been caught on the commit.
#   2. it computes the RIGHT ANSWER under minor, against a host build of the
#      same file.  New coverage: run_diff.sh checks results against the host
#      but runs the atomic CPU, so until now nothing checked that the pipeline
#      model computes correctly at all.
#   3. atomic and minor agree on that answer -- the two models must not differ
#      on semantics, only on cycles.
#   4. a LOOSE ceiling on cycles per bundle.
#
# (4) is a smoke bound, not a precision assertion, and is loose on purpose.
# The bundle-length bug cost 6x on code whose lengths vary a lot but only 30%
# on this kernel (5.90 against 4.53 cycles a bundle), so a ceiling tight
# enough to catch it here would false-alarm the moment the model legitimately
# improves.  check_timing.sh is where exact timing belongs; this one is here
# to catch a blowup or a hang on real code.  The measured figure is printed so
# a human can watch it move.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5  HOSTCC  MARCH
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
HOSTCC="${HOSTCC:-cc}"
MARCH="${MARCH:-lvx-2}"
CEILING=12          # cycles per bundle; see above, this is a smoke bound

for f in "$BIN/lvx-mbr-gcc" "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$f" ] || { echo "missing: $f"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
rc=0

# The expected answer, derived from a host build rather than written down.
# Every FP operation in the kernel is exactly rounded, so host and LVX agree
# bit for bit provided nothing contracts into an FMA -- hence -ffp-contract=off
# on both sides.
"$HOSTCC" -O2 -ffp-contract=off -DKERNEL_HOST "$here/kernel.c" -o "$work/host" -lm \
    || { echo "host build failed"; exit 2; }
expected="$("$work/host")"
case "$expected" in ''|*[!0-9]*) echo "host build produced no answer"; exit 2 ;; esac

"$BIN/lvx-mbr-as" -march="$MARCH" "$here/crt0_mbr.S" -o "$work/crt0.o" || exit 2
for src in kernel libmin; do
    "$BIN/lvx-mbr-gcc" -march="$MARCH" -c -O2 -ffp-contract=off -ffreestanding \
        -fno-builtin -nostdlib -DKERNEL_EXPECTED=${expected}UL \
        "$here/$src.c" -o "$work/$src.o" || exit 2
done
"$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib \
    "$work/crt0.o" "$work/kernel.o" "$work/libmin.o" -o "$work/k.elf" || exit 2

run() {     # run <cpu> -- echoes "<exit> <cycles> <bundles>", or nothing on a hang
    local cpu="$1"
    LVX_CPU="$cpu" timeout 600 "$GEM5" --outdir="$work/m5-$cpu" "$RUNCFG" \
        "$work/k.elf" </dev/null >"$work/out-$cpu" 2>&1 || return 1
    local code cyc ins
    code="$(sed -n 's/.*code=\([0-9]*\).*/\1/p' "$work/out-$cpu" | head -1)"
    cyc="$(sed -n 's/.*(\([0-9]*\) cycles).*/\1/p' "$work/out-$cpu" | head -1)"
    ins="$(awk '/^simInsts/ {print $2; exit}' "$work/m5-$cpu/stats.txt")"
    echo "${code:-?} ${cyc:-0} ${ins:-0}"
}

report() { if [ "$2" = "$3" ]; then printf '%-44s PASS (%s)\n' "$1" "$2"
           else printf '%-44s FAIL (got %s, want %s)\n' "$1" "${2:-<none>}" "$3"; rc=1; fi; }

atomic="$(run atomic)" || { echo "atomic run did not finish"; exit 1; }
set -- $atomic; a_code=$1
report "atomic computes the host's answer" "$a_code" 0

if ! minor="$(run minor)"; then
    printf '%-44s FAIL (did not finish -- a hang is the bug\n' "minor completes"
    printf '%44s  this test exists for)\n' ""
    echo "check_kernel: FAIL"; exit 1
fi
printf '%-44s PASS\n' "minor completes"
set -- $minor; m_code=$1; m_cyc=$2; m_ins=$3
report "minor computes the host's answer" "$m_code" 0
report "atomic and minor agree"           "$m_code" "$a_code"

if [ "$m_ins" -gt 0 ] 2>/dev/null; then
    hundredths=$(( m_cyc * 100 / m_ins ))
    if [ "$hundredths" -lt $(( CEILING * 100 )) ]; then
        printf '%-44s PASS (%s.%02d)\n' "cycles per bundle under $CEILING" \
            $(( hundredths / 100 )) $(( hundredths % 100 ))
    else
        printf '%-44s FAIL (%s.%02d)\n' "cycles per bundle under $CEILING" \
            $(( hundredths / 100 )) $(( hundredths % 100 )); rc=1
    fi
else
    echo "could not read simInsts"; rc=1
fi

echo "----"
printf 'answer %s   atomic %s cycles   minor %s cycles over %s bundles\n' \
    "$expected" "$(set -- $atomic; echo $2)" "$m_cyc" "$m_ins"
[ $rc -eq 0 ] && echo "check_kernel: PASS" || echo "check_kernel: FAIL"
exit $rc
