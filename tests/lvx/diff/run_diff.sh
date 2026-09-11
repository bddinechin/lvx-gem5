#!/usr/bin/env bash
#
# Native-x86  <->  LVX-ISS  differential test harness.
#
# For each C program: build it with the host cc AND with lvx-mbr-gcc (freestanding,
# linked against crt0_mbr.S + libmin.c), run the LVX ELF under gem5 SE-mode, and
# compare the process exit code against the native run.  The exit code (0..255) is
# the whole signal: these programs are freestanding (no libc yet), so main() returns
# its result and the crt exits with it.  A mismatch means the LVX toolchain miscompiled
# or the ISS misexecuted -- the two are told apart by which cells of the matrix
# diverge and by reading the emitted asm.
#
# The matrix is (core x optimization level).  Both dimensions matter and neither is
# redundant: the port was developed against lvx-1 -O2 alone, and every other cell was
# broken when the matrix was first run -- a -O0 ICE in the mach pass across 9/10
# programs on both cores, and a bundle-packing bug the assembler rejects at lvx-2 -O2.
# Restrict with MARCH/OPTS while chasing one of them; the full matrix is the default
# so that a fix in one cell cannot quietly break another.
#
# Usage:   ./run_diff.sh [file.c ...]     (no args => every c/*.c)
# Env:     LVX_TOOLCHAIN_BIN  GEM5  LVX_CPU(atomic|timing|minor)  HOSTCC  MARCH  OPTS
#
# Known-clean baseline:   MARCH=lvx-1 OPTS=-O2 ./run_diff.sh
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
# The ISS is chosen PER MARCH, not once. This matrix builds every cell for
# both cores (MARCH below), and the default used to be a single unnamed
# build/LVX/gem5.opt -- which is in fact the lvx-2 executable, so the lvx-1
# half of the matrix ran on the lvx-2 simulator and nothing said so. That
# matters here more than anywhere: the lvx-2 columns are what catch a
# wrong-core BE/GEM5 install, and they cannot if both columns are lvx-2.
#
# GEM5 still overrides both, for pinning the whole run to one simulator
# deliberately.
GEM5_LVX1="${GEM5_LVX1:-$gem5root/build/gem5-lvx1.opt}"
GEM5_LVX2="${GEM5_LVX2:-$gem5root/build/gem5-lvx2.opt}"
gem5_for() {
    [ -n "${GEM5:-}" ] && { printf '%s' "$GEM5"; return; }
    case "$1" in
        lvx-1) printf '%s' "$GEM5_LVX1" ;;
        lvx-2) printf '%s' "$GEM5_LVX2" ;;
        *) echo "no ISS known for -march=$1" >&2; exit 2 ;;
    esac
}
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
export LVX_CPU="${LVX_CPU:-atomic}"
HOSTCC="${HOSTCC:-cc}"
MARCH="${MARCH:-lvx-1 lvx-2}"
OPTS="${OPTS:--O0 -O1 -O2 -Os}"

for tool in "$BIN/lvx-mbr-gcc" "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$RUNCFG" \
            $(for m in $MARCH; do gem5_for "$m"; echo; done); do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# The crt and the minimal runtime are per-core: ld refuses to mix object files of
# different LVX cores ("architecture of input file is incompatible").
for m in $MARCH; do
    mkdir -p "$work/$m"
    "$BIN/lvx-mbr-as" -march="$m" "$here/crt0_mbr.S" -o "$work/$m/crt0.o" \
        || { echo "crt0 assemble failed for $m"; exit 2; }
    "$BIN/lvx-mbr-gcc" -march="$m" -c -O2 -ffreestanding -fno-builtin -nostdlib \
        "$here/libmin.c" -o "$work/$m/libmin.o" \
        || { echo "libmin build failed for $m"; exit 2; }
done

# Native reference per source.  It is the oracle, not a variable of the experiment,
# so it is built once at -O2 and reused across every cell.
declare -A nat_cache
native_code() {
    local src="$1" key; key="$(basename "$src" .c)"
    if [ -z "${nat_cache[$key]+set}" ]; then
        if "$HOSTCC" -O2 -w "$src" -o "$work/x86" 2>/dev/null; then
            "$work/x86"; nat_cache[$key]=$(( $? & 255 ))
        else
            nat_cache[$key]="BUILDFAIL"
        fi
    fi
    printf '%s' "${nat_cache[$key]}"
}

pass=0 fail=0
declare -A cell_pass cell_fail cell_note

note_cell() { # core opt verdict
    local k="$1|$2"
    if [ "$3" = pass ]; then
        cell_pass[$k]=$(( ${cell_pass[$k]:-0} + 1 )); pass=$((pass+1))
    else
        cell_fail[$k]=$(( ${cell_fail[$k]:-0} + 1 )); fail=$((fail+1))
        [ -n "${cell_note[$k]:-}" ] || cell_note[$k]="$4"
    fi
}

run_one() {
    local src="$1" m="$2" opt="$3" name; name="$(basename "$src" .c)"
    local pfx; pfx="$(printf '  %-16s %-6s %-4s' "$name" "$m" "$opt")"

    local nat; nat="$(native_code "$src")"
    if [ "$nat" = BUILDFAIL ]; then
        echo "$pfx NATIVE-BUILD-FAIL"; note_cell "$m" "$opt" fail "native build"; return
    fi

    # Compile only, then link with ld directly: the gcc driver's -mbr link spec pulls
    # in libc/libgloss and a runtime whose _start does not exist yet, and these
    # programs are deliberately freestanding.
    if ! "$BIN/lvx-mbr-gcc" -march="$m" -c $opt -ffreestanding -fno-builtin -nostdlib \
            "$src" -o "$work/p.o" 2>"$work/lerr"; then
        local why
        if grep -q "internal compiler error" "$work/lerr"; then
            why="ICE $(grep -oP 'internal compiler error: \K.*' "$work/lerr" | head -1)"
        elif grep -qE "(Fatal error|Error):" "$work/lerr"; then
            why="ASM $(grep -oP '(Fatal error|Error): \K.*' "$work/lerr" | head -1)"
        else
            why="$(grep -oP 'error: \K.*' "$work/lerr" | head -1)"
        fi
        echo "$pfx LVX-BUILD-FAIL  ${why}"
        note_cell "$m" "$opt" fail "${why}"; return
    fi

    if ! "$BIN/lvx-mbr-ld" "$work/$m/crt0.o" "$work/p.o" "$work/$m/libmin.o" \
            -o "$work/p.elf" 2>"$work/lderr"; then
        local why; why="$(grep -oiE 'undefined reference to .*|cannot find [^ ]*' "$work/lderr" | head -1)"
        echo "$pfx LVX-LINK-FAIL ($why)"; note_cell "$m" "$opt" fail "link: $why"; return
    fi

    local out code
    local gem5; gem5="$(gem5_for "$m")"
    out="$(timeout 120 "$gem5" --outdir="$work/m5" "$RUNCFG" "$work/p.elf" 2>&1)"
    code="$(printf '%s' "$out" | grep -oE 'exited \(code=[0-9]+\)' | grep -oE '[0-9]+' | head -1)"
    if [ -z "$code" ]; then
        local why; why="$(printf '%s' "$out" | grep -oiE 'panic|fatal|Illegal instruction|timed out' | head -1)"
        echo "$pfx ISS-FAIL (${why:-no-exit})"; note_cell "$m" "$opt" fail "ISS ${why:-no-exit}"; return
    fi

    if [ "$code" = "$nat" ]; then
        echo "$pfx PASS (=$code)"; note_cell "$m" "$opt" pass
    else
        echo "$pfx MISMATCH  lvx=$code  native=$nat"
        note_cell "$m" "$opt" fail "mismatch"
    fi
}

srcs=("$@"); [ $# -eq 0 ] && srcs=("$here"/c/*.c)
printf 'native-x86 <-> LVX-ISS differential   (CPU=%s, cores:%s, opts:%s)\n' \
    "$LVX_CPU" "$MARCH" "$OPTS"
for m in $MARCH; do for o in $OPTS; do for s in "${srcs[@]}"; do run_one "$s" "$m" "$o"; done; done; done

echo "----"
printf 'matrix (pass/total per cell)\n'
printf '  %-8s' ''; for o in $OPTS; do printf '%-14s' "$o"; done; echo
for m in $MARCH; do
    printf '  %-8s' "$m"
    for o in $OPTS; do
        k="$m|$o"; p=${cell_pass[$k]:-0}; f=${cell_fail[$k]:-0}
        printf '%-14s' "$p/$((p+f))"
    done
    echo
done
# One representative reason per broken cell -- enough to tell the cells apart
# without scrolling back through the per-test lines.
for m in $MARCH; do for o in $OPTS; do
    k="$m|$o"; [ -n "${cell_note[$k]:-}" ] && printf '  %-8s %-4s  %s\n' "$m" "$o" "${cell_note[$k]}"
done; done
echo "----"
printf 'PASS=%d  FAIL=%d\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
