#!/usr/bin/env bash
#
# Targeted $cs exception-flag check for the FP families that used to drop their
# exceptions (standalone; complements run_diff.sh and check_cs_flags.sh).
#
# The seeds (fsrec/fsrsr), the compares (fcomp) and the widening conversions
# (fwiden/fextl) each computed an IEEE result and discarded the exceptions it
# raised, because the MDS helper signature had no flag outputs to carry them.
# Every other FP check in this suite compares computed *values*, and the values
# are identical whether or not the flag is threaded -- so nothing here would
# notice if they went back to being dropped. This reads $cs and requires the
# exact bits.
#
# Half the cases expect no flag at all: fclass is non-computational and raises
# nothing by IEEE 754, a quiet NaN compare raises nothing, and widening is exact
# so an ordinary value raises nothing. Those pin the other direction -- that a
# later "add the missing flags" pass does not raise flags that must not be.
#
# Also runs a negative control, because a test whose expectations cannot fail is
# worth nothing: the same program with case 1 expecting IO instead of DZ must be
# rejected.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5  LVX_CPU(atomic|timing|minor)
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-/home/bd3/lvx-csw/lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
export LVX_CPU="${LVX_CPU:-atomic}"

for tool in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT
"$BIN/lvx-mbr-as" "$here/crt0_mbr.S" -o "$work/crt0.o" || { echo "crt0 assemble failed"; exit 2; }

run() {   # run <name> <source>  -- echoes the exit code, or empty
    "$BIN/lvx-mbr-as" "$2" -o "$work/$1.o"                          || return 2
    "$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/$1.o" -o "$work/$1.elf" || return 2
    timeout 120 "$GEM5" --outdir="$work/m5-$1" "$RUNCFG" "$work/$1.elf" 2>&1 |
      grep -oE 'exited \(code=[0-9]+\)' | grep -oE '[0-9]+' | head -1
}

rc=0

code="$(run fp_noflags "$here/fp_noflags.s")" || { echo "fp_noflags     FAIL (build)"; exit 2; }
names=(. "1: fsrecd(+0.0) DZ"     "2: fsrsrd(+0.0) DZ"   "3: fsrsrd(-1.0) IO" \
         "4: fcompd(sNaN) IO"     "5: fcompd(qNaN) none"  "6: fwidenwd(sNaN) IO" \
         "7: fwidenwd(1.0f) none" "8: fclassd(sNaN) none")
case "${code:-x}" in
  0) echo "fp_noflags     PASS (seeds, compares and widening report their exceptions;" \
          "fclass and the quiet cases still raise nothing)" ;;
  [1-8]) echo "fp_noflags     FAIL (case ${names[$code]} gave the wrong \$cs)"; rc=1 ;;
  x) echo "fp_noflags     FAIL (no exit code from gem5)"; rc=1 ;;
  *) echo "fp_noflags     FAIL (unexpected exit code $code)"; rc=1 ;;
esac

# Negative control: expect IO where case 1 must raise DZ, and require a failure.
sed '0,/maked \$r5 = 8\t\t\t## expect DZ only, bit 3/s//maked $r5 = 16/' \
    "$here/fp_noflags.s" > "$work/fp_noflags_neg.s"
cmp -s "$here/fp_noflags.s" "$work/fp_noflags_neg.s" &&
    { echo "negative-ctrl  FAIL (the mutation did not apply)"; exit 2; }
code="$(run fp_noflags_neg "$work/fp_noflags_neg.s")" || { echo "negative-ctrl  FAIL (build)"; exit 2; }
if [ "${code:-x}" = "1" ]; then
    echo "negative-ctrl  PASS (a wrong expectation for case 1 is rejected)"
else
    echo "negative-ctrl  FAIL (expectations are not being checked: code=${code:-none})"; rc=1
fi

exit $rc
