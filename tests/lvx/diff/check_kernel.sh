#!/usr/bin/env bash
#
# A kernel of real size, under the pipeline model.
#
# This is the gap three timing bugs walked through in one week while twenty-odd
# timing assertions passed over them: every other test here is either a micro
# test or runs the atomic CPU, so nothing exercised MinorCPU on code of any
# size.  One of the three was an outright deadlock, found by somebody else's
# PolyBench run after 1h19m rather than by this suite.
#
# Two kernels, because they stress different halves of the model and neither
# subsumes the other -- see kernel.c for the shape of each and why each part
# of it is there.  Briefly: `stencil' puts a long latency (fsqrtw, fdivw)
# inside a loop-carried dependence, and `seidel' puts a tight chain of
# short-latency double-precision work there instead, Gauss-Seidel updated in
# place so each element waits on its west neighbour.
#
# Note the second is NOT the harder case by divergence from the functional
# model -- measured 2.25x against the first's 3.92x -- which is rather the
# point: a single number does not tell you which shapes a model gets wrong.
# It is in because it is the shape PolyBench's seidel-2d has (where the same
# measurement gives 2.51x), and because it is the only thing here that brings
# double-precision FP and fdivd under the pipeline model at all.
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

"$BIN/lvx-mbr-as" -march="$MARCH" "$here/crt0_mbr.S" -o "$work/crt0.o" || exit 2

report() { if [ "$2" = "$3" ]; then printf '%-44s PASS (%s)\n' "$1" "$2"
           else printf '%-44s FAIL (got %s, want %s)\n' "$1" "${2:-<none>}" "$3"; rc=1; fi; }

run() {     # run <tag> <cpu> -- echoes "<exit> <cycles> <bundles>", nothing on a hang
    local tag="$1" cpu="$2"
    LVX_CPU="$cpu" timeout 600 "$GEM5" --outdir="$work/m5-$tag-$cpu" "$RUNCFG" \
        "$work/$tag.elf" </dev/null >"$work/out-$tag-$cpu" 2>&1 || return 1
    local code cyc ins
    code="$(sed -n 's/.*code=\([0-9]*\).*/\1/p' "$work/out-$tag-$cpu" | head -1)"
    cyc="$(sed -n 's/.*(\([0-9]*\) cycles).*/\1/p' "$work/out-$tag-$cpu" | head -1)"
    ins="$(awk '/^simInsts/ {print $2; exit}' "$work/m5-$tag-$cpu/stats.txt")"
    echo "${code:-?} ${cyc:-0} ${ins:-0}"
}

one_kernel() {  # one_kernel <tag> <extra -D flags...>
    local tag="$1"; shift

    # The expected answer, derived from a host build rather than written down.
    # Every FP operation in the kernels is exactly rounded, so host and LVX
    # agree bit for bit provided nothing contracts into an FMA -- hence
    # -ffp-contract=off on both sides.
    "$HOSTCC" -O2 -ffp-contract=off -DKERNEL_HOST "$@" "$here/kernel.c" \
        -o "$work/host-$tag" -lm || { echo "$tag: host build failed"; return 2; }
    local expected; expected="$("$work/host-$tag")"
    case "$expected" in ''|*[!0-9]*) echo "$tag: host build gave no answer"; return 2 ;; esac

    for src in kernel libmin; do
        "$BIN/lvx-mbr-gcc" -march="$MARCH" -c -O2 -ffp-contract=off -ffreestanding \
            -fno-builtin -nostdlib -DKERNEL_EXPECTED=${expected}UL "$@" \
            "$here/$src.c" -o "$work/$tag-$src.o" || return 2
    done
    "$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/crt0.o" \
        "$work/$tag-kernel.o" "$work/$tag-libmin.o" -o "$work/$tag.elf" || return 2

    local atomic minor a_code m_code m_cyc m_ins hundredths
    atomic="$(run "$tag" atomic)" || { echo "$tag: atomic did not finish"; rc=1; return 0; }
    set -- $atomic; a_code=$1
    report "$tag: atomic computes the host's answer" "$a_code" 0

    if ! minor="$(run "$tag" minor)"; then
        printf '%-44s FAIL (did not finish -- a hang is the\n' "$tag: minor completes"
        printf '%44s  bug this test exists for)\n' ""
        rc=1; return 0
    fi
    printf '%-44s PASS\n' "$tag: minor completes"
    set -- $minor; m_code=$1; m_cyc=$2; m_ins=$3
    report "$tag: minor computes the host's answer" "$m_code" 0
    report "$tag: atomic and minor agree"           "$m_code" "$a_code"

    if [ "${m_ins:-0}" -gt 0 ] 2>/dev/null; then
        hundredths=$(( m_cyc * 100 / m_ins ))
        if [ "$hundredths" -lt $(( CEILING * 100 )) ]; then
            printf '%-44s PASS (%s.%02d)\n' "$tag: cycles per bundle under $CEILING" \
                $(( hundredths / 100 )) $(( hundredths % 100 ))
        else
            printf '%-44s FAIL (%s.%02d)\n' "$tag: cycles per bundle under $CEILING" \
                $(( hundredths / 100 )) $(( hundredths % 100 )); rc=1
        fi
    else
        echo "$tag: could not read simInsts"; rc=1
    fi

    printf '  %s: answer %s  atomic %s cyc  minor %s cyc over %s bundles\n' \
        "$tag" "$expected" "$(set -- $atomic; echo $2)" "$m_cyc" "$m_ins"
}

one_kernel stencil || exit 2
one_kernel seidel -DKERNEL_SEIDEL || exit 2

echo "----"
[ $rc -eq 0 ] && echo "check_kernel: PASS" || echo "check_kernel: FAIL"
exit $rc
