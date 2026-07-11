# Phase 0 findings — gem5 ⇄ LVX spike

Goal of Phase 0: de-risk the critical path (how the MDS-generated semantics and
decoder reach gem5) and size Phase 1. **Result: settled, with running code.**

## Headline

The MDS **already** generates, via the existing `BE/LAO` back-end, everything we
need as portable C:

- `lvx-mds/refs/BE/LAO/lvx_v1/Behavior.tuple` (74K lines) — per-opcode
  `fetch`/`execute`/`commit` bodies.
- `lvx-mds/refs/BE/LAO/lvx_v1/Decode.c` (4.6K lines) — the `Decoding.table`
  decode tree as nested-switch C, one function per encoding space
  (`simple`/`double`/`triple`).

Both are coupled only to a small, well-defined runtime API. So we **reuse the
generated C verbatim** and hand-write only a runtime shim — we do **not** build a
`BE/GEM5` back-end or retarget `Behavior.pm::CodeGen`. (This was rev-3's
"fallback"; the spike shows it is the cheaper, lower-risk primary path.)

## What was proven (spike in `scratchpad/lvx_spike/`, reproducible)

1. **Generated semantics compile and run unmodified.** The verbatim `execute`
   bodies for `AWAIT` and register-register `ADDW` were extracted from
   `Behavior.tuple` and compiled against a ~100-line hand-written shim + a
   minimal `Int256_`. Results correct, including the `signextw` modifier and the
   `sx32`/`zx32` result-extension path.
2. **VLIW parallel semantics fall out of the fetch/execute/commit split.** A
   two-instruction "bundle" where op2 reads a register op1 writes
   (`r0=r1+r1 ; r1=r0+r0`) yields `r1=20` (deferred write-back), not the
   sequential `80`. The engine: run all `execute`s (reads live, writes buffered),
   then flush at the bundle boundary. This is the register-swap hazard the plan
   flagged — handled structurally, no bespoke code.
3. **The decoder is reusable as-is.** The full 963-opcode `simple`-space
   `Decode_Decoding_lvx_v1_simple()` compiles cleanly as standalone C.

Notes:
- The generated code uses `this` as a parameter name ⇒ **must be compiled as C**
  (or preprocessed to rename) when linked into gem5's C++.
- `Int256_` is the SIMD value type (256-bit); scalar ops use the low 64 bits.

## Runtime seam (the hand-written shim scope)

Census of every primitive the generated LVX code depends on:

- **Int256** — ~35 pure arithmetic fns (`add/sub/mul/div/and/or/xor/shl/shru/
  sx/zx/sat/clz/ctz/rol/ror/...`). Port from Kalray BSL `Int256.h`.
- **Operand engine** (hot path): `operandRead` (×2553), `operandFromValue`
  (×740), `operandFromRegFile_*`, `operandToRegFile_*`, `commitRegFiles`
  (= deferred write-back). ~7 functions → gem5 `readIntRegOperand` /
  `setIntRegOperand`.
- **Memory**: `MEM_load` / `MEM_store` / `MEM_atomic_*` / `MEM_fence` → gem5
  `readMem`/`writeMem`; cache ops are no-ops in SE mode.
- **Control**: `readFromStorage_PC`, `writeToStorage_NPC`, `branch_info`,
  `bcucond`, `guard` → gem5 PC/branch handling.
- **`syscall`** — one helper → gem5 `SyscallDesc`.
- **FP** — helper names are the **Berkeley SoftFloat** API (`f32_add`,
  `f64_mulAdd`, `i32_to_f32`, `decode_riscv_float_rounding_mode`, …), which gem5
  already vendors for RISC-V. FP is essentially free.
- **Privileged / TLB / wfx** — FS-only; stub or throw in SE mode.

Reference implementations of all of the above exist in Kalray's ISS
(`iss_core/.../ISS_Behavior.c`, `helpers_core.h`) and LAO BSL. Reuse approved.

## Revised layer model (supersedes rev-3 §"Three work layers")

- **Layer A (generator)** — *nothing new to build.* Keep the existing `BE/LAO`
  back-end enabled; consume its `Behavior.tuple` + `Decode.c`. Regeneration on
  ISA change is automatic.
- **Layer B (runtime shim, hand-written, small)** — Int256 + the HELPER
  categories above over gem5 `ExecContext`; FP → gem5 SoftFloat; ISA plumbing
  (register classes r0–r63 + SFR/XCR, `Process`/ABI, `scall`→`SyscallDesc`,
  `EM_LVX` in the ELF loader).
- **Layer C (bundle front-end, hand-written)** — de-bundling per
  `lvx-target/lvx_VLIWInstructionBundling.tex`, then per-instruction decode via
  the reused `Decode.c`, then sequence fetch/execute/commit with
  `commitRegFiles` at the bundle boundary (proven in the spike).

## Phase 1 (sized)

1. Bring `Behavior.tuple` + `Decode.c` into the gem5 build, compiled as C.
2. Implement Layer B shim (Int256 + operand/mem/control/syscall helpers; FP via
   SoftFloat).
3. Implement Layer C bundle front-end (IMMX reassembly, magic immediates,
   per-instruction PC).
4. gem5 `arch/lvx` plumbing + `EM_LVX` recognition.
5. **Milestone:** `-O0` C "hello world" from `lvx-mbr-gcc` runs to correct exit;
   register-swap-in-a-bundle and 64-bit-IMMX-immediate tests pass.

Remaining Phase-0 task deferred into Phase 1: the empty `arch/lvx` skeleton +
`EM_LVX` (now that the strategy is settled, this belongs with the shim work).
