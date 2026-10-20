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

## Running all of it

`./run_checks.sh` runs the matrix and every `check_*.sh` beside it, and exits
non-zero if any of them fails; `make check-iss` from `lvx-csw/` is the same
thing. Until 2026-10-19 nothing ran any of this -- there is no CI in this tree
and no make target reached these scripts -- which is how three ISS timing bugs
reached the tree in one week with twenty-odd assertions in them passing over
it, two of them found by a benchmark run elsewhere. A suite nobody runs is
documentation.

It discovers the checks by glob rather than from a list, so a new one is
covered without editing anything, and it names the ISS binary explicitly
rather than letting each script default to `build/LVX/gem5.opt`, which holds
whichever core was built last. `check_timing.sh` and `check_kernel.sh` run
against both cores, those being the two whose answers depend on which core
built the binary. About two minutes, or ~35 s with `SKIP_DIFF=1`.

## Standalone checks beside the matrix

Some behaviour has no x86 equivalent to diff against, so it is checked directly by
a `check_*.sh` in this directory: `check_highmult.sh` (the four `muld` variants),
`check_fp.sh` / `check_fpn.sh` / `check_fcomp.sh` / `check_conv.sh` (the scalar FP
surface), `check_ccb.sh` (the fused compare-and-branch predicates), `check_xvr.sh`
(the lvx-2 vector moves), **`check_ownership.sh`** (system-register ownership) and
**`check_timing.sh`** (the taken-branch penalty and the load variants' latency)
**`check_decode_cache.sh`** (that a cached bundle is still PC-relative) and
**`check_kernel.sh`** (a real kernel under the pipeline model).

**`check_kernel.sh`** is the one that runs real code under the pipeline model,
and it exists because three timing bugs reached the tree in one week with
twenty-odd timing assertions passing over them. Everything else here is either
a micro test or runs the atomic CPU, so nothing exercised MinorCPU on code of
any size; one of the three was an outright deadlock, found by somebody else's
PolyBench run after 1h19m rather than by this suite. `kernel.c` holds two
kernels, each compiled `-O2` so bundle lengths vary and GCC emits hardware
loops: `stencil` puts a long latency (`fsqrtw`, `fdivw`) inside a
loop-carried dependence, and `seidel` -- PolyBench's seidel-2d shape, in
double and updated in place -- puts a tight chain of short-latency work there
instead, so each element waits on its west neighbour. Neither subsumes the
other, and `seidel` is notably *not* the harder case by divergence from the
functional model (2.25x against `stencil`'s 3.92x, where PolyBench's own
seidel-2d gives 2.51x); it is in because that is a different shape and
because it is the only thing here that brings double-precision FP and `fdivd`
under the pipeline model at all. It asserts that the kernel *completes*
under `minor`, that it computes the host build's answer, and that `atomic` and
`minor` agree on that answer; the expected value is derived from a host build
rather than written down, which works because every operation in it is exactly
rounded. The cycles-per-bundle ceiling is a deliberately loose smoke bound --
the bundle-length bug cost 6x on code whose lengths vary a lot but only 30% on
this kernel, so a ceiling tight enough to catch it here would false-alarm the
moment the model improves. Exact timing belongs in `check_timing.sh`.

**`check_timing.sh`** is the odd one in the other direction: it checks *cycles*
rather than results, so it is the only script here that needs `LVX_CPU=minor`
(it sets it itself). Two description numbers nothing in the ISS read until
2026-10-16 are what it pins -- `Processor@minTaken`, the cycles a taken branch
costs beyond its own bundle, and the write stage of an *uncached* load, which is
SR rather than the L1 stage (`Modifier@variant`'s `LoadedAt`, reaching the ISS
through `Opcode@timing`). Both are read out of the generated `lvx_stages.py`, so
changing one in `Processor.yml` moves the expectation with it. The assertions about
*dangling* are the subtle ones: code that is already waiting on a latency must
cost the *same* whatever `minTaken` is, because the branch's stall cycles
elapse against that latency instead of adding to it. `LVX_TAKEN_BUBBLE` (see
`run_lvx.py`) is what makes it measurable -- the same binary at a penalty of 0
and of `minTaken`, so the prologue and the cold instruction fetch cancel
exactly. `ld.u; goto; use` is the sharpest of them: it fails if the penalty is
ever anchored at the branch's *commit* rather than its issue, since in-order
commit holds the branch behind the load and a commit-anchored bubble lands
after the latency it should have vanished into.

**`check_parallel.sh`** pins VLIW parallel semantics inside one bundle: a
reader beside a writer sees the value being replaced, and two instructions can
exchange registers with no temporary. The ISS gets this structurally and
nothing tested it; it is here because it is what the micro-op split would
break, gem5's micro-ops executing in sequence so that syllable k+1 would see
k's writes. Each of the five cases is built so sequential execution gives a
different, specific result, and the exit code says which failed -- verified by
mutating each case and watching it report its own number.

**`check_memorder.sh`** is the same rule one level down, and it found a real
bug. A bundle's two LSU slots can hold a load and a store to the same address;
they issue together, so the load must read what memory held before the bundle.
Both `MEM_load` and `MEM_store` run from the behaviour *execute* phase, and
execute() ran the non-BCU syllables in slot order -- so `sd ... ; ld ...' let
the load see the stored value while `ld ... ; sd ...' did not, and which one
you got depended on the assembler's steering. execute() now runs every
syllable that only reads memory before every syllable that modifies it, the
atomics counted as modifiers. No compiled code reaches this (of 50,631 bundles
across libc and the validation binaries, none holds two memory accesses),
which is why it went unnoticed and is no reason for the ISS to disagree with
the architecture.

**`check_dcache.sh`** pins that the data cache sees the program's accesses at
all. Until 2026-10-20 it saw none: every load and store was served
functionally inside the behaviour through a port proxy, so no request reached
the hierarchy and every counter read zero however many accesses the program
made -- a failure no counter can report, zero accesses and no plumbing being
indistinguishable. Asserted as the difference between two runs of the same
fixture, one doing twice the trips, which cancels whatever the startup and
exit paths access. A `minor'-only test: the functional CPU models have no
load/store queue and access memory through the proxy by design.

`check_timing.sh` grew two assertions and corrected six on 2026-10-19, when
the micro-op split began enforcing one bundle per cycle. The six had been
calibrated against a model that issued two bundles in a cycle whenever their
syllables used different latency buckets, so they encoded costs up to 24%
below the machine's; each now states its expectation as exact cycles a trip,
derived in a comment from the recurrence, the loop's bundle count and the
branch penalty. The pre-split binary fails all six, which is the check that
the new expectations have teeth.

Two are about the hardware loop, and the second is the sharp one: the loop must
cost the same whether or not the bundle `LE` falls off happens to hold a
branch. It did not until 2026-10-18, the back edge having reached the fetch
side only through the branch predictor, which Fetch2 consults only for a
bundle that is `isControl()`.

Four assertions are about operands read *later* than RR -- a store's stored
value and the integer MAC's accumulator, at E1 -- which shorten the dependence
into them by a cycle, lvx-gcc's `define_bypass` seen from the other side. Each
is measured against its own control of the same shape reading at RR, so no
per-opcode latency is written down, and the two controls are themselves checked
against the generated stage enum. The `maddw` one is the sharp case: 1 cycle a
step against the control's 2, so a regression halves nothing quietly.

The last two assertions are about *lanes*. A 128-bit value lives in an aligned
GPR pair and a 256-bit one in a quad, so `lq $r2r3` writes two general
registers and `lo $r4r5r6r7` four, and a scalar read of any one of them depends
on the load. `timing_tuple.s` uses all six -- both halves of the pair, all four
lanes of the quad -- so a lane missing from the port's tuple expansion costs a
sixth of the measured time. Verified against the negative control too: with the
use reading a register *outside* the tuple the same loop costs 2.45 cycles a
copy, against 4.25 when it reads a lane.

**`check_decode_cache.sh`** guards the one thing the decode cache risks. The
decoder caches a decoded bundle by its syllables, so a single `LvxStaticInst`
is now shared by every address those bytes appear at -- sound only because
nothing in a decoded bundle is absolute, each sub-instruction holding its byte
offset *within* the bundle while the PC comes from the execution context. The
fixture is two byte-identical `goto` bundles at different addresses, each of
which must reach its own target; the differential harness cannot see this,
since it compares against x86 and so cannot say which LVX address a branch
reached. The script checks the two bundles really are byte-identical before
believing the result, so the fixture cannot quietly stop testing anything.

`check_ownership.sh` is the odd one: everything else here runs at PL0, where the
ownership model is invisible by construction, so its two programs leave PL0 on
purpose. `ownership.s` covers the refusals that are not traps (`$men` reads back at
PL0 and reads as zero at PL1) and `ownership_trap.s` the one that is (a PL1 write
to `$men` must panic the ISS, since SE mode has no ring to divert a privilege trap
to). Together they are what keeps the check from being dead code.

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
