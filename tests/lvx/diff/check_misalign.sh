#!/usr/bin/env bash
#
# Targeted atomic-alignment check (standalone; complements run_diff.sh).
#
# The atomics require their effective address to be a multiple of the access
# size.  That used to be stated only in `properties` and in the manual's prose,
# so the ISS simply performed a misaligned atomic -- SETranslatingPortProxy does
# not care.  The description now checks the address and THROWs MISALIGN, the
# trap the architecture names HTO_DMIS, and the shim turns that into a panic
# because SE mode has no ring to divert it to.
#
#   misalign.s        the aligned accesses, one per size, each a compare-and-
#                     swap against a seeded value.  Must exit 0.  This is the
#                     half that stops the check from firing on everything: a
#                     test that only looked for the trap would pass with every
#                     atomic rejected.
#
#                     CAS rather than ald/asd for a reason that has since gone
#                     away -- MEM_atomic_cas was once the only atomic the shim
#                     implemented -- and kept because it exercises a different
#                     instruction family than atomics.s does.
#   misalign_trap.s   an 8-byte atomic store one past an 8-aligned address.
#                     Must panic; reaching the exit is the failure.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5  LVX_CPU(atomic|timing|minor)
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
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

# --- the aligned accesses, which must be untouched -----------------------------
if ! build misalign; then
    echo "misalign           FAIL (build)"; exit 2
fi
out="$(run misalign)"
code="$(printf '%s' "$out" | grep -oE 'exited \(code=[0-9]+\)' | grep -oE '[0-9]+' | head -1)"
names=(. "1: byte at an odd address" "2: half word at +2" \
         "3: word at +4"             "4: double word at +0")
case "${code:-x}" in
  0) echo "misalign           PASS (every naturally aligned atomic swaps)" ;;
  [1-4]) echo "misalign           FAIL (case ${names[$code]} did not swap)"; rc=1 ;;
  x) echo "misalign           FAIL (no exit code from gem5 -- the check fired on"
     echo "                         an ALIGNED access?)"
     printf '%s\n' "$out" | grep -oE 'misaligned atomic access to 0x[0-9a-f]+' | head -1; rc=1 ;;
  *) echo "misalign           FAIL (unexpected exit code $code)"; rc=1 ;;
esac

# --- the access that traps -----------------------------------------------------
if ! build misalign_trap; then
    echo "misalign_trap      FAIL (build)"; exit 2
fi
out="$(run misalign_trap)"
if printf '%s' "$out" | grep -q 'misaligned atomic access to 0x'; then
    addr="$(printf '%s' "$out" | grep -oE 'misaligned atomic access to 0x[0-9a-f]+' | head -1)"
    echo "misalign_trap      PASS ($addr)"
    # The address must be odd and the report must name the alignment it wanted:
    # a message that said neither would pass the grep above and tell nobody
    # which access was at fault.
    if ! printf '%s' "$out" | grep -q 'must be a multiple of 8'; then
        echo "misalign_trap      FAIL (the trap did not report the required alignment)"; rc=1
    fi
elif printf '%s' "$out" | grep -q 'exited (code='; then
    echo "misalign_trap      FAIL (the misaligned atomic was performed)"; rc=1
else
    echo "misalign_trap      FAIL (no alignment trap and no exit)"; rc=1
fi

# --- negative control ----------------------------------------------------------
# A test whose expectation cannot fail is worth nothing: make the trap program's
# address aligned and require the trap to stop being reported.
#
# It now runs to completion too, so this asserts both halves -- no DMIS, and the
# store actually happened.  It used to be able to check only the first, because
# asd's MEM_atomic_store was a panic stub and gem5 died either way.
sed 's/addd \$r2 = \$r2, 1/addd $r2 = $r2, 0/' \
    "$here/misalign_trap.s" > "$work/misalign_neg.s"
cmp -s "$here/misalign_trap.s" "$work/misalign_neg.s" &&
    { echo "negative-ctrl      FAIL (the mutation did not apply)"; exit 2; }
"$BIN/lvx-mbr-as" "$work/misalign_neg.s" -o "$work/misalign_neg.o" &&
"$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/misalign_neg.o" -o "$work/misalign_neg.elf" ||
    { echo "negative-ctrl      FAIL (build)"; exit 2; }
out="$(timeout 120 "$GEM5" --outdir="$work/m5-neg" "$RUNCFG" "$work/misalign_neg.elf" 2>&1)"
if printf '%s' "$out" | grep -q 'misaligned atomic access to 0x'; then
    echo "negative-ctrl      FAIL (an ALIGNED address was reported misaligned)"; rc=1
elif ! printf '%s' "$out" | grep -q 'exited (code='; then
    echo "negative-ctrl      FAIL (no DMIS, but the store did not complete either)"; rc=1
else
    echo "negative-ctrl      PASS (the same store at an aligned address completes)"
fi

exit $rc
