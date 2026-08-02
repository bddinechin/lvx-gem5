# native-x86 ↔ LVX-ISS differential harness

The validation oracle the LVX roadmap is built on: **compile the same C program
with the host `cc` and with `lvx-mbr-gcc`, run the LVX binary under the gem5 ISS,
and diff the result against the native run.** The oracle is native x86 execution —
deliberately *independent of `Behavior`*, since the gem5 ISS **is** `Behavior`
compiled (`BE/GEM5` tuple + `Decode.c`), so a Behavior-derived model could never
independently validate it. This same harness validates the GCC, LLVM and MLIR
back-ends against one reference.

## What the signal is

These programs are **freestanding** — deliberately, even though the `-mbr` newlib
now builds and runs (see "Hosted programs" below) — so the whole signal is the
process **exit code (0..255)**. Each `c/*.c`
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

The default run is **clean in every cell**; anything else is a regression. (It was
not always: when the matrix was introduced only `lvx-1 -O2` passed — see the
history below.)

`crt0.o` and `libmin.o` are rebuilt per core, since `ld` refuses to mix objects of
different LVX cores. The native reference is built once per program at `-O2` and
reused across every cell: it is the oracle, not a variable of the experiment.

## Baseline (2026-08-02) — the matrix

```
          -O0        -O1        -O2        -Os
  lvx-1   12/12      12/12      12/12      12/12
  lvx-2   12/12      12/12      12/12      12/12
```

The 12 programs cover integer arithmetic, nested loops, if/else ladders, C `switch`,
recursion (call/ret + RA), many-argument calls (register + stack), signed/unsigned
casts, bit manipulation, struct/pointer aggregate copies, integer division and
modulo, and `__int128` — the ISS executing real compiled C, validated against x86.

### What the matrix found

Adding the two dimensions immediately found that **only the cell the port was
developed in was clean**. Three distinct lvx-gcc bugs, none of them harness faults,
each isolated to specific cells (which is the point of the matrix — a single-cell
harness cannot tell an `-O0` bug from an lvx-2 bug). **All three are now fixed**;
they are kept here because each says what a single-cell harness would have missed:

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

Note that the ISS runs lvx-2 code, but none of these programs contain vector types,
so the SIMD *execution* path is still substantially unvalidated even where it
compiles. Beware of proving otherwise with a test GCC constant-folds away: a
256-bit `v8si` add of literals compiles to a single `maked $r0 = 18`, which
exercises nothing.

## Hosted programs

A newlib-linked program now runs end to end under the ISS — `printf` of integers
and `%f`, `malloc`, `fopen`/`fprintf`/`fgets`, and libm all work. This harness does
**not** cover that: it compares exit codes, which structurally cannot catch a
`printf` formatting bug. A hosted harness diffing *stdout* is the natural
follow-on, and is not written yet.

One hosted gap is known: `<fenv.h>` reaches `wfxl`, still a panic stub in the
shim, so `fesetround` aborts the simulator.

## Growing the corpus

Keep programs **libgcc-free**: nothing here links `libgcc.a`, so a program that
needs a helper fails at link rather than producing a mismatch. Both restrictions
the corpus used to carry have since lifted, which is why `divmod.c` exists:

- **`/` and `%` are fine now.** They were banned because GCC lowered them to
  `__divsi3`/`__modsi3`; lvx-gcc emits the hardware divmod family instead, so the
  link stays clean. Use `volatile` operands or the divisor folds to a
  multiply-and-shift and the test proves nothing.
- **Floating point is fine now.** The scalar f16/f32/f64 surface is implemented in
  the shim over Berkeley SoftFloat and matches RISC-V; it was banned when those
  helpers were panic stubs.

Still out: vector types (the SIMD helper bodies are stubs), and anything reaching
`wfxl`/`wfxm` or the cache/TLB maintenance helpers.
