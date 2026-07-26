# arch/lvx — LVX ISA support for gem5

Port design; see `../../../PORTING-PLAN.md` and `../../../PHASE0-FINDINGS.md`
for the strategy and its Phase 0 validation.

## Approach (unlike other gem5 ISAs)

LVX does **not** use gem5's `.isa` parser. Instruction **decode** and
**semantics** are reused verbatim from the LVX MDS `BE/GEM5` output
(`behavior_bodies.inc`, `Decode.c`), compiled as C. This directory hand-writes only:

1. a thin runtime **shim** the generated C calls, mapped onto gem5's
   `ExecContext`; and
2. the LVX **bundle front-end** (de-bundling) plus standard gem5 ISA plumbing.

Build selection: `build_opts/LVX` → `USE_LVX_ISA` (see `Kconfig`). SE-mode
(AtomicSimpleCPU) functional simulation is the Phase 1–3 target.

## Planned files (added incrementally)

| File | Layer | Role |
|------|-------|------|
| `generated/behavior_bodies.inc`, `generated/Decode.c` | A | MDS output (regenerated from sibling `lvx-mds`), compiled as C |
| `shim.{cc,hh}`, `int256.{cc,hh}` | B | runtime API (`operandRead`/`operandFromValue`/`operandFromRegFile`/`commitRegFiles`, `MEM_*`, PC/branch, `syscall`) over `ExecContext`; FP → gem5 SoftFloat; Int256 ported from Kalray BSL |
| `decoder.{cc,hh}` | C | read syllables to bundle end (parallel bit), reassemble IMMX/magic immediates, per-instruction decode via `Decode.c`, build `LvxStaticInst`s |
| `insts/*.{cc,hh}` | C | `LvxStaticInst` (macroop/microop) dispatching `execute()` to generated bodies; fetch/execute/commit sequencing |
| `isa.{cc,hh}`, `LvxISA.py` | B | `BaseISA`: 512-reg file (r0–r63 GPR, SFR/XCR misc), reset |
| `regs/*.hh` | B | register classes and indices |
| `pcstate.hh` | C | per-instruction PC (LVX PC = first syllable of the instruction) |
| `process.cc`, `reg_abi.{cc,hh}` | B | SE process, kv4-v1 ABI arg/ret conventions, `scall` → `SyscallDesc` |
| `faults.{cc,hh}` | B | traps (`throw_PRIVILEGE`/`throw_OPCODE`, …) |
| `remote_gdb.{cc,hh}` | B | GDB stub for `lvx-gdb` |

## Notes / gotchas

- Generated code uses `this` as a parameter name → compile the `generated/`
  unit as **C**, not C++.
- LVX binaries currently carry `e_machine = 256` (== `EM_KVX`); gem5 recognizes
  256 as LVX (decision recorded 2026-07-11). Loader hook in
  `src/base/loader/elf_object.cc::determineArch()`.
- `int256_t` is the 256-bit SIMD value type; scalar ops use the low 64 bits.
- LP64, little-endian.
