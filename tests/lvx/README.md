# LVX gem5 SE-mode smoke tests

Minimal freestanding LVX programs that exercise the decode → shim → execute
pipeline end to end and exit via `scall` (kv4-v1 syscall ABI: number in the
scall operand, args in r0..r7, return in r0).

## Build a test (needs the lvx-mbr toolchain on PATH)

```bash
lvx-mbr-as compute.s -o compute.o
lvx-mbr-ld -e _start -Ttext=0x10000 -nostdlib compute.o -o compute.elf
```

## Run under gem5

```bash
build/LVX/gem5.opt tests/lvx/run_lvx.py compute.elf
```

Expected:
- `exit42`  → `target exited (code=42)`
- `compute` → `target exited (code=19)`  (computes 5*3 + 4)

## Bundle front-end tests (multi-instruction / multi-syllable)
- `mk64`   → `code=188`  — a >32-bit `make` (double/triple encoding, IMMX operand)
- `par`    → `code=188`  — two `make`s in one bundle (ALU0/ALU1), summed in the next
- `bundle` → `code=7`    — mixed: triple `make` + a 2-instruction bundle

Verified the decode matches `lvx-mbr-objdump` (instructions-per-bundle and bundle
byte size) via `--debug-flags=LvxDecode`.

## Floating-point regression tests
- `fpu_crash_repro` → **fixed as of 2026-07-30** (`code=0`, the low 32
  bits of a real fused `ffmad`'s result -- see the file's own comments).
  Originally crashed the gem5 process itself (`Illegal instruction (core
  dumped)`, not a simulated guest trap) for every floating-point
  arithmetic opcode tried; kept as a regression test now that it's fixed.
  Re-verified end to end from the lvx-mlir side too: a chained
  `ffmad`→`ffmsd` kernel with non-trivial operands now executes with a
  bit-exact, sign-and-magnitude-correct fused result (lvx-mlir's
  `docs/lvx/EndToEndValidation.md`, "ffma/ffms accumulator coalescing,
  verified end to end").
- `fcompd_crash_repro` → **also fixed as of 2026-07-30**
  (`Behavior_floatcomp_64` added to `shim_fp.cc`), same day it was found.
  `code=1` as expected. Floating-point comparisons (`fcompd`/`fcompw`)
  were a narrower follow-on gap the arithmetic fix above didn't cover --
  see the file's own comments.
