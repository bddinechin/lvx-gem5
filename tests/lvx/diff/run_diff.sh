#!/usr/bin/env bash
#
# Native-x86  <->  LVX-ISS  differential test harness.
#
# For each C program: build it with the host cc AND with lvx-mbr-gcc (freestanding,
# linked against crt0_mbr.S + libmin.c), run the LVX ELF under gem5 SE-mode, and
# compare the process exit code against the native run.  The exit code (0..255) is
# the whole signal: these programs are freestanding (no libc yet), so main() returns
# its result and the crt exits with it.  A mismatch means the LVX toolchain miscompiled
# or the ISS misexecuted -- the two are told apart by which opt levels diverge and by
# reading the emitted asm.
#
# Usage:   ./run_diff.sh [file.c ...]     (no args => every c/*.c)
# Env:     LVX_TOOLCHAIN_BIN  GEM5  LVX_CPU(atomic|timing|minor)  HOSTCC  OPTS
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-/home/bd3/lvx-csw/lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
export LVX_CPU="${LVX_CPU:-atomic}"
HOSTCC="${HOSTCC:-cc}"
OPTS="${OPTS:--O2}"   # -O2 is the clean baseline; OPTS="-O0 -O2" exposes the -O0 gcc ICE

for tool in "$BIN/lvx-mbr-gcc" "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG"; do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Build the shared freestanding crt + minimal runtime once.
"$BIN/lvx-mbr-as" "$here/crt0_mbr.S" -o "$work/crt0.o"                     || { echo "crt0 assemble failed"; exit 2; }
"$BIN/lvx-mbr-gcc" -c -O2 -ffreestanding -fno-builtin -nostdlib \
    "$here/libmin.c" -o "$work/libmin.o"                                   || { echo "libmin build failed"; exit 2; }

pass=0 fail=0
run_one() {
    local src="$1" opt="$2" name; name="$(basename "$src" .c)"

    # native reference (process exit code is 8-bit)
    if ! "$HOSTCC" -O2 -w "$src" -o "$work/x86" 2>"$work/nerr"; then
        printf '  %-16s %-4s  NATIVE-BUILD-FAIL\n' "$name" "$opt"; fail=$((fail+1)); return; fi
    "$work/x86"; local nat=$(( $? & 255 ))

    # lvx build: compile only, then link with ld directly (the gcc driver's default
    # -mbr link spec wants a runtime/linker-script that is not installed yet).
    if ! "$BIN/lvx-mbr-gcc" -c $opt -ffreestanding -fno-builtin -nostdlib \
            "$src" -o "$work/p.o" 2>"$work/lerr"; then
        printf '  %-16s %-4s  LVX-COMPILE-FAIL\n' "$name" "$opt"
        sed 's/^/      /' "$work/lerr" | head -3; fail=$((fail+1)); return; fi
    if ! "$BIN/lvx-mbr-ld" "$work/crt0.o" "$work/p.o" "$work/libmin.o" \
            -o "$work/p.elf" 2>"$work/lderr"; then
        printf '  %-16s %-4s  LVX-LINK-FAIL (%s)\n' "$name" "$opt" \
            "$(grep -oiE 'undefined reference to .*|cannot find [^ ]*' "$work/lderr" | head -1)"
        fail=$((fail+1)); return; fi

    local out code
    out="$(timeout 120 "$GEM5" --outdir="$work/m5" "$RUNCFG" "$work/p.elf" 2>&1)"
    code="$(printf '%s' "$out" | grep -oE 'exited \(code=[0-9]+\)' | grep -oE '[0-9]+' | head -1)"
    if [ -z "$code" ]; then
        local why; why="$(printf '%s' "$out" | grep -oiE 'panic|fatal|Illegal instruction|timed out' | head -1)"
        printf '  %-16s %-4s  ISS-FAIL (%s)\n' "$name" "$opt" "${why:-no-exit}"; fail=$((fail+1)); return; fi

    if [ "$code" = "$nat" ]; then
        printf '  %-16s %-4s  PASS (=%s)\n' "$name" "$opt" "$code"; pass=$((pass+1))
    else
        printf '  %-16s %-4s  MISMATCH  lvx=%s  native=%s\n' "$name" "$opt" "$code" "$nat"; fail=$((fail+1))
    fi
}

srcs=("$@"); [ $# -eq 0 ] && srcs=("$here"/c/*.c)
printf 'native-x86 <-> LVX-ISS differential   (CPU=%s, opts:%s)\n' "$LVX_CPU" "$OPTS"
for s in "${srcs[@]}"; do for o in $OPTS; do run_one "$s" "$o"; done; done
echo "----"
printf 'PASS=%d  FAIL=%d\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
