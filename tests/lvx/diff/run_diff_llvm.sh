#!/usr/bin/env bash
#
# Native-x86 <-> LVX-ISS differential harness, LLVM backend variant.
#
# Same oracle as run_diff.sh (native x86 exit code), but the LVX side is built
# with THIS repo's clang -emit-llvm -> llc -mtriple=lvx -> lvx-mbr-as -> lvx-mbr-ld
# instead of lvx-mbr-gcc (which isn't built in this checkout). clang has no LVX
# frontend target registered (Basic/Targets + Driver ToolChain -- separate,
# not-yet-done work), so C is parsed with -target x86_64-unknown-linux-gnu and
# the resulting generic LLVM IR is fed to llc -mtriple=lvx for real LVX codegen
# -- the same approach already verified against lvx-mbr-as this session.
#
# Usage:   ./run_diff_llvm.sh [file.c ...]     (no args => every c/*.c)
# Env:     LVX_TOOLCHAIN_BIN  GEM5  LVX_CPU(atomic|timing|minor)  HOSTCC  OPTS
#          CLANG  LLC
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

# Defaults derived from this script's own location, like every other check here:
# the absolute ones that used to be written out named /home/guembu/bd3/lvx-llvm,
# which predates the move into lvx-csw and resolves to nothing, so the clang and
# llc defaults pointed at files that are not there.
csw="$(cd "$gem5root/.." && pwd)"
BIN="${LVX_TOOLCHAIN_BIN:-$csw/lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
CLANG="${CLANG:-$csw/lvx-llvm/llvm-project/build/bin/clang}"
LLC="${LLC:-$csw/lvx-llvm/llvm-project/build/bin/llc}"
export LVX_CPU="${LVX_CPU:-atomic}"
HOSTCC="${HOSTCC:-cc}"
OPTS="${OPTS:--O2}"

for tool in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG" "$CLANG" "$LLC"; do
    [ -e "$tool" ] || { echo "missing: $tool"; exit 2; }
done

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Compiles one freestanding C file through clang -emit-llvm -> llc -> lvx-mbr-as.
# -fno-vectorize -fno-slp-vectorize: this backend has no SIMD/vector codegen
# at all (v4i64 exists only for the calling convention's register-quad
# mechanics, not real vector arithmetic/memory ops) -- LLVM's auto-vectorizer
# at -O2 turns even a plain byte-copy loop into a <4 x i8> store, which is a
# separate, unimplemented feature, not a bug in what this harness is testing.
compile_llvm() {
    local src="$1" opt="$2" out="$3"
    "$CLANG" -S -emit-llvm "$opt" -target x86_64-unknown-linux-gnu \
        -ffreestanding -fno-builtin -nostdlib -w \
        -fno-vectorize -fno-slp-vectorize \
        "$src" -o "$work/$(basename "$out").ll" 2>"$work/cerr" \
        && "$LLC" -mtriple=lvx -filetype=asm "$work/$(basename "$out").ll" \
               -o "$work/$(basename "$out").s" 2>"$work/lcerr" \
        && "$BIN/lvx-mbr-as" "$work/$(basename "$out").s" -o "$out" 2>"$work/aserr"
}

# Build the shared freestanding crt + minimal runtime once, entirely through
# our own pipeline (crt0_mbr.S is hand-written asm either way).
"$BIN/lvx-mbr-as" "$here/crt0_mbr.S" -o "$work/crt0.o" || { echo "crt0 assemble failed"; exit 2; }
compile_llvm "$here/libmin.c" -O2 "$work/libmin.o" || { echo "libmin build failed"; cat "$work"/*err 2>/dev/null; exit 2; }

pass=0 fail=0
run_one() {
    local src="$1" opt="$2" name; name="$(basename "$src" .c)"

    if ! "$HOSTCC" -O2 -w "$src" -o "$work/x86" 2>"$work/nerr"; then
        printf '  %-16s %-4s  NATIVE-BUILD-FAIL\n' "$name" "$opt"; fail=$((fail+1)); return; fi
    "$work/x86"; local nat=$(( $? & 255 ))

    if ! compile_llvm "$src" "$opt" "$work/p.o"; then
        printf '  %-16s %-4s  LVX-COMPILE-FAIL\n' "$name" "$opt"
        sed 's/^/      /' "$work"/cerr "$work"/lcerr "$work"/aserr 2>/dev/null | grep -v '^\s*$' | head -3
        fail=$((fail+1)); return; fi
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
printf 'native-x86 <-> LVX-ISS differential (LLVM backend)   (CPU=%s, opts:%s)\n' "$LVX_CPU" "$OPTS"
for s in "${srcs[@]}"; do for o in $OPTS; do run_one "$s" "$o"; done; done
echo "----"
printf 'PASS=%d  FAIL=%d\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
