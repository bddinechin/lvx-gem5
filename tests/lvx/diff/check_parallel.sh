#!/usr/bin/env bash
#
# VLIW parallel semantics within one bundle: a reader beside a writer sees the
# value being replaced, and two instructions can exchange registers in one
# bundle with no temporary.
#
# The ISS gets this structurally -- LvxStaticInst::execute runs every
# sub-instruction's fetch phase, then every execute phase, then every commit
# phase -- and until 2026-10-19 nothing tested it.  It is worth a test because
# it is what the micro-op split would break: gem5's micro-ops execute in
# sequence, so one StaticInst per syllable makes syllable k+1 see k's writes,
# and the failure is a wrong answer rather than a crash.  See parallel.s; each
# case is built so sequential execution gives a different, specific result,
# and the exit code says which case failed.
#
# Run under both CPU models, because the split changes how the pipeline model
# drives execution and not how the functional one does.
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
"$BIN/lvx-mbr-as" -march lvx-2 "$here/parallel.s" -o "$work/p.o" || exit 2
"$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/p.o" -o "$work/p.elf" || exit 2

names=(. "the parallel swap" "a reader sees the old value" \
       "a three-way rotate" "disjoint predicated writes" \
       "disjoint predicated writes, other order")

rc=0
for cpu in atomic minor; do
    out="$(LVX_CPU="$cpu" timeout 120 "$GEM5" --outdir="$work/m5-$cpu" \
            "$RUNCFG" "$work/p.elf" 2>&1)"
    code="$(printf '%s' "$out" | sed -n 's/.*code=\([0-9]*\).*/\1/p' | head -1)"
    if [ "${code:-x}" = 0 ]; then
        printf '%-40s PASS\n' "$cpu: all five cases"
    else
        printf '%-40s FAIL (case %s: %s)\n' "$cpu: all five cases" \
            "${code:-<none>}" "${names[${code:-0}]:-unknown}"
        rc=1
    fi
done

[ $rc -eq 0 ] && echo "check_parallel: PASS" || echo "check_parallel: FAIL"
exit $rc
