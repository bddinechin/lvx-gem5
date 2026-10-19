#!/usr/bin/env bash
#
# Memory ordering within one bundle: a load issued beside a store must read
# memory as it was before the bundle, whatever slot each landed in.
#
# The ISS ran MEM_load and MEM_store both from the behaviour *execute* phase
# and ran the non-BCU syllables in slot order, so the answer depended on which
# slot the assembler picked: `sd ... ; ld ...' let the load see the stored
# value, `ld ... ; sd ...' did not.  execute() now runs every syllable that
# only reads memory before every syllable that modifies it -- the same rule
# the fetch pass gives the registers (check_parallel.sh), one level down.
#
# Each of the five cases is built so the wrong order gives a different,
# specific value, and the exit code says which failed; verified by mutating
# each in turn.  Run under both CPU models, since each drives execute()
# differently.
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
"$BIN/lvx-mbr-as" -march lvx-2 "$here/memorder.s" -o "$work/mo.o" || exit 2
"$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib "$work/mo.o" -o "$work/mo.elf" || exit 2

names=(. "the load must read the pre-bundle value" \
       "the store must still land" \
       "the other slot order must give the same answer" \
       "the store must still land, other slot order" \
       "disjoint addresses must be unaffected")

rc=0
for cpu in atomic minor; do
    out="$(LVX_CPU="$cpu" timeout 120 "$GEM5" --outdir="$work/m5-$cpu" \
            "$RUNCFG" "$work/mo.elf" 2>&1)"
    code="$(printf '%s' "$out" | sed -n 's/.*code=\([0-9]*\).*/\1/p' | head -1)"
    if [ "${code:-x}" = 0 ]; then
        printf '%-40s PASS\n' "$cpu: all five cases"
    else
        printf '%-40s FAIL (case %s: %s)\n' "$cpu: all five cases" \
            "${code:-<none>}" "${names[${code:-0}]:-unknown}"
        rc=1
    fi
done

[ $rc -eq 0 ] && echo "check_memorder: PASS" || echo "check_parallel: FAIL"
exit $rc
