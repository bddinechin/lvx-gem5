#!/usr/bin/env bash
#
# Targeted $cs IEEE 754 exception-flag layout check (standalone; complements
# run_diff.sh).
#
# The FP checks beside this one compare computed values, so they are blind to
# where a raised flag lands in $cs: the whole flag layout could be permuted and
# every one of them would still pass. cs_flags.s pins the positions instead --
# RISC-V's fflags positions, IN/UN/OV/DZ/IO at bits 0..4 -- by clearing $cs,
# performing one operation that raises exactly one flag, and requiring $cs to
# equal exactly that bit.
#
# Also runs a negative control, because a test whose expectations cannot fail
# is worth nothing: the same program with case 1 expecting DZ at its *old*
# position must be rejected.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5  LVX_CPU(atomic|timing|minor)
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
# The ISS to run on. These build with the toolchain's default -march, which
# is lvx-1, so the lvx-1 simulator is the one that matches. The default used
# to be build/LVX/gem5.opt -- a single unnamed executable that is in fact the
# lvx-2 build, so these ran lvx-1 programs on the lvx-2 ISS and nothing said
# so. GEM5 still overrides, for chasing one core deliberately.
GEM5="${GEM5:-$gem5root/build/gem5-lvx1.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
export LVX_CPU="${LVX_CPU:-atomic}"

for tool in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
"$BIN/lvx-mbr-as" "$here/crt0_mbr.S" -o "$work/crt0.o" || { echo "crt0 assemble failed"; exit 2; }

run() {   # run <name> <source>  -- echoes the exit code, or empty
    "$BIN/lvx-mbr-as" "$2" -o "$work/$1.o"                      || return 2
    "$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/$1.o" -o "$work/$1.elf" || return 2
    timeout 120 "$GEM5" --outdir="$work/m5-$1" "$RUNCFG" "$work/$1.elf" 2>&1 |
      grep -oE 'exited \(code=[0-9]+\)' | grep -oE '[0-9]+' | head -1
}

rc=0

code="$(run cs_flags "$here/cs_flags.s")" || { echo "cs_flags       FAIL (build)"; exit 2; }
names=(. "1: DZ (1.0/0.0)" "2: IN (1.0/3.0)" "3: IO (0.0/0.0)")
case "${code:-x}" in
  0) echo "cs_flags       PASS (IN/UN/OV/DZ/IO at bits 0..4, RISC-V fflags order)" ;;
  1|2|3) echo "cs_flags       FAIL (case ${names[$code]} raised the wrong \$cs bit)"; rc=1 ;;
  x) echo "cs_flags       FAIL (no exit code from gem5)"; rc=1 ;;
  *) echo "cs_flags       FAIL (unexpected exit code $code)"; rc=1 ;;
esac

# Negative control: expect DZ where it used to be (bit 2) and require a failure.
sed 's/maked \$r5 = 8\t\t\t## expect DZ only, bit 3/maked $r5 = 4/' \
    "$here/cs_flags.s" > "$work/cs_flags_neg.s"
code="$(run cs_flags_neg "$work/cs_flags_neg.s")" || { echo "negative-ctrl  FAIL (build)"; exit 2; }
if [ "${code:-x}" = "1" ]; then
    echo "negative-ctrl  PASS (the old DZ position is rejected)"
else
    echo "negative-ctrl  FAIL (expectations are not being checked: code=${code:-none})"; rc=1
fi

exit $rc
