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
./run_diff.sh                 # every c/*.c at -O2
./run_diff.sh c/loops.c       # one program
OPTS="-O0 -O2" ./run_diff.sh  # add -O0 (currently exposes a gcc ICE, see below)
LVX_CPU=minor ./run_diff.sh   # in-order pipeline model instead of atomic
```
Env: `LVX_TOOLCHAIN_BIN`, `GEM5`, `HOSTCC`, `OPTS`, `LVX_CPU`. Exit status is
nonzero if any case fails, so it drops straight into CI.

## Baseline (2026-07-23, at -O2)

**9 of 10 pass** — `arith bits branches cswitch funcs loops recursion signedness
structptr` all match native x86, covering integer arithmetic, nested loops,
if/else ladders, C `switch`, recursion (call/ret + RA), many-argument calls
(register + stack), signed/unsigned casts, bit manipulation, and struct/pointer
aggregate copies. This is the ISS executing real compiled C, validated against x86.

Two lvx-gcc bugs the harness found (these are the next work, not harness faults):

- **`array.c` at -O2** — GCC emits `addx2wp $r2 = $r0, $r0`, which the assembler
  rejects with "Unexpected token": a gcc↔assembler mnemonic/operand mismatch on the
  word-pair add-shift (distinct from the known LITE even/odd parity-constraint error).
- **`-O0`, almost every program** — backend ICE `safe_as_a<rtx_insn*>` at
  `is-a.h:268` during the machine reorg pass. `-O0`-only; `-O1`/`-Og`/`-O2`/`-Os`
  are all fine. So the harness defaults to `-O2`; `OPTS="-O0 -O2"` reproduces it.

## Growing the corpus

Keep programs **libgcc-free** for now: no `/` or `%` by a non-power-of-two (GCC
lowers those to `__divsi3`/`__modsi3` at `-O0`), and no floating point (the ISS's
FP `APPLY` helpers are still panic stubs). Those two gaps — libgcc and the FP
helpers — are exactly what the harness will motivate building next; when newlib's
mbr port lands, `libmin.c` and the freestanding link go away and stdout joins the
exit code as signal.
