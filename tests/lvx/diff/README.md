# native-x86 ↔ LVX-ISS differential harness

The validation oracle the LVX roadmap is built on: **compile the same C program
with the host `cc` and with `lvx-mbr-gcc`, run the LVX binary under the gem5 ISS,
and diff the result against the native run.** The oracle is native x86 execution —
deliberately *independent of `Behavior`*, since the gem5 ISS **is** `Behavior`
compiled (`BE/GEM5` tuple + `Decode.c`), so a Behavior-derived model could never
independently validate it. This same harness validates the GCC, LLVM and MLIR
back-ends against one reference.

## What the signal is

These programs are **freestanding** — there is no newlib in the `-mbr` toolchain
yet — so the whole signal is the process **exit code (0..255)**. Each `c/*.c`
computes a result in `main` and returns it `& 0xFF`; the crt (`crt0_mbr.S`) calls
`main` and exits with it via the LVX syscall ABI (number in the `scall` operand,
args/return in `r0..`). `libmin.c` supplies only `memcpy`/`memset`/`memmove`/`memcmp`,
which GCC emits calls to even under `-ffreestanding`; nothing else is linked, so a
mismatch means a **compiler or ISS bug**, never a missing symbol.

## Running

```sh
./run_diff.sh                        # the full matrix: 2 cores x 4 opt levels
./run_diff.sh c/loops.c              # one program, still across the matrix
MARCH=lvx-1 OPTS=-O2 ./run_diff.sh   # the known-clean baseline cell
MARCH=lvx-2 OPTS=-O2 ./run_diff.sh   # one cell while chasing a bug in it
LVX_CPU=minor ./run_diff.sh          # in-order pipeline model instead of atomic
```
Env: `LVX_TOOLCHAIN_BIN`, `GEM5`, `HOSTCC`, `MARCH`, `OPTS`, `LVX_CPU`. Exit status
is nonzero if any case fails.

**The default run currently fails**, on purpose — see the matrix below. Use
`MARCH=lvx-1 OPTS=-O2` for the green cell; that is the configuration the port was
developed against and the only one that was ever exercised.

`crt0.o` and `libmin.o` are rebuilt per core, since `ld` refuses to mix objects of
different LVX cores. The native reference is built once per program at `-O2` and
reused across every cell: it is the oracle, not a variable of the experiment.

## Baseline (2026-08-02) — the matrix

```
          -O0      -O1      -O2      -Os
  lvx-1   1/10     10/10    10/10    9/10
  lvx-2   1/10     10/10     9/10    9/10
```

The 10 programs cover integer arithmetic, nested loops, if/else ladders, C `switch`,
recursion (call/ret + RA), many-argument calls (register + stack), signed/unsigned
casts, bit manipulation, and struct/pointer aggregate copies — the ISS executing
real compiled C, validated against x86.

Adding the two dimensions immediately found that **only the cell the port was
developed in was clean**. Three distinct lvx-gcc bugs, none of them harness faults,
each isolated to specific cells (which is the point of the matrix — a single-cell
harness cannot tell an `-O0` bug from an lvx-2 bug):

- **`-O0`, both cores, 9/10 programs** — backend ICE `in safe_as_a, at is-a.h:268`
  during the machine reorg (`mach`) pass. Core-independent, `-O0`-only.
- **`-Os`, both cores, `loops.c`** — backend ICE, segfault, also in `mach`.
  Core-independent.
- **lvx-2 `-O2`, `array.c`** — the assembler rejects GCC's output: *"too many ALU
  FULL or LITE instructions in bundle"*. A bundle-packing bug on the SIMD path, so
  GCC's ALU FULL/LITE accounting disagrees with the assembler's. lvx-2 `-Os` builds
  the same program fine, so it is specific to `-O2`'s scheduling.

An earlier `array.c` failure at lvx-1 `-O2` (GCC emitting `addx2wp`, which the
assembler rejected) is fixed; the 64-bit SIMD it came from has since been removed.

Note that the ISS runs lvx-2 code — the lvx-2 `-O1` column is a clean 10/10 — but
none of these programs contain vector types, so the SIMD *execution* path is still
substantially unvalidated even where it compiles. Beware of proving otherwise with a
test GCC constant-folds away: a 256-bit `v8si` add of literals compiles to a single
`maked $r0 = 18`, which exercises nothing.

## Growing the corpus

Keep programs **libgcc-free** for now: no `/` or `%` by a non-power-of-two (GCC
lowers those to `__divsi3`/`__modsi3` at `-O0`), and no floating point (the ISS's
FP `APPLY` helpers are still panic stubs). Those two gaps — libgcc and the FP
helpers — are exactly what the harness will motivate building next; when newlib's
mbr port lands, `libmin.c` and the freestanding link go away and stdout joins the
exit code as signal.
