#!/usr/bin/env bash
#
# Targeted system-register ownership check (standalone; complements run_diff.sh).
#
# The ISS gates every GET/SET/WFX of a system register on the running protection
# ring against the ring that owns each of that register's bit-fields
# (US11995218; the table comes from MDS, see generated/ownership.inc).  SE-mode
# programs run at PL0 and so never exercise it -- which is the whole reason this
# test exists: both programs leave PL0 deliberately.
#
#   ownership.s       the refusals that are not traps: $men reads back at PL0 and
#                     reads as zero at PL1 (rerror READ0).  Must exit 0.
#   ownership_trap.s  the refusal that is one: writing $men from PL1 is a
#                     privilege trap, which SE mode cannot deliver.  Must panic.
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

build() {   # build <name>
    "$BIN/lvx-mbr-as" "$here/$1.s" -o "$work/$1.o"                  || return 2
    "$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/$1.o" -o "$work/$1.elf" || return 2
}

run() {     # run <name>  -- echoes gem5's combined output
    timeout 120 "$GEM5" --outdir="$work/m5-$1" "$RUNCFG" "$work/$1.elf" 2>&1
}

rc=0

# --- the non-trapping refusals -------------------------------------------------
if ! build ownership; then
    echo "ownership          FAIL (build)"; exit 2
fi
out="$(run ownership)"
code="$(printf '%s' "$out" | grep -oE 'exited \(code=[0-9]+\)' | grep -oE '[0-9]+' | head -1)"
case "${code:-x}" in
  0) echo "ownership          PASS (PL0 reads through, PL1 reads as zero)" ;;
  1) echo "ownership          FAIL (case 1: \$men did not read back at PL0)"; rc=1 ;;
  2) echo "ownership          FAIL (case 2: \$men did not read as zero at PL1 --"
     echo "                         the ownership check did not engage)"; rc=1 ;;
  x) echo "ownership          FAIL (no exit code from gem5)"
     printf '%s\n' "$out" | grep -oiE 'panic|fatal|Illegal instruction|timed out' | head -1; rc=1 ;;
  *) echo "ownership          FAIL (unexpected exit code $code)"; rc=1 ;;
esac

# --- the refusal that is a trap ------------------------------------------------
if ! build ownership_trap; then
    echo "ownership_trap     FAIL (build)"; exit 2
fi
out="$(run ownership_trap)"
if printf '%s' "$out" | grep -q 'privilege trap on SET of system register'; then
    echo "ownership_trap     PASS (privilege trap on the PL0-owned write)"
elif printf '%s' "$out" | grep -q 'exited (code='; then
    echo "ownership_trap     FAIL (the PL1 write to \$men was allowed through)"; rc=1
else
    echo "ownership_trap     FAIL (no privilege trap and no exit)"; rc=1
fi

exit $rc
