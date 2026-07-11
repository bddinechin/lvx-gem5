# Retargeting gem5 to LVX — Porting Plan

Status: **draft for review — nothing implemented yet.**
Goal: a functional gem5 simulator that runs `lvx-mbr-gcc` output so we can validate the LVX compilers.

## Scope

- **SE (syscall-emulation) mode only** for phases 0–3. Newlib bare-runtime programs use `scall`; gem5's SE layer emulates those on the host. No MMU, devices, or boot.
- **Functional correctness first, timing later.** Start with `AtomicSimpleCPU`. VLIW cycle-accuracy (O3, bundle latencies, dual-issue) is a separable phase-4 effort, not needed to validate compiler output.
- The compiler emits LP64 little-endian LVX-1 ELF. We only execute what the compiler produces, so the instruction set grows by coverage rather than being implemented up front.

## Ground truth (source of authority)

| Concern | Authority |
|---|---|
| Instruction semantics | `<Behavior>` element of each `<Opcode>` in `lvx-mds/refs/MDD/lvx/lvx_v1/Opcode.table` — a parsed, typed S-expression AST (grammar: `MDS/DOC/Behavior.y`; walker: `MDS/LIB/Behavior.pm`). Already lowered to the ISS by `MDS/BE/LAO/BIN/Behavior.pl`. |
| Decode tree | `lvx-mds/refs/MDD/lvx/lvx_v1/Decoding.table` — authoritative, pre-optimized (Theiling, LCTES 2001), nested `<Decode shift/mask/case>` → `opcodes=`. |
| Encoding spaces | `Encoding.table`: `simple` (1×32-bit), `double` (2×32-bit), `triple` (3×32-bit). A single instruction is 1–3 syllables (main + up to two IMMX). |
| VLIW bundling | `lvx-target/lvx_VLIWInstructionBundling.tex` (spec) + `lvx-binutils/gas/config/tc-lvx.c` (assembler = the inverse operation). |
| `APPLY` helpers / FP / atomics | KVX ISS `iss_core/iss/include/kvx/helpers_core.h` and `iss_core/syscall_lib/` as the reference implementation. |
| ABI / syscalls | kv4-v1 ABI (identical to LVX); `lvx-newlib` syscall stubs; `iss_core/syscall_lib`. |

> Note: the MDD `Dispersal`/`Bundle`/`Template` tables are **not** used — their contents are stale and are being removed from the MDS. Bundling authority is the LaTeX spec above.

## The two independent "multi-word" mechanisms

The front-end must disentangle both, in order:

1. **Bundle grouping** — read 32-bit syllables until the parallel bit clears.
2. **Instruction reassembly within the bundle** — attach IMMX extension syllables to their main syllable, reconstructing each instruction's logical `simple`/`double`/`triple` word.
3. **Per-instruction decode** — run each reassembled word through the translated `Decoding.table` tree for its encoding space → opcode → `StaticInst`.

### Syllable format (from `lvx_VLIWInstructionBundling.tex`)

- Bit 31 = **parallel bit**: 0 ⇒ last syllable in the bundle, else 1.
- Bits 30–29 = **steering**: 0 BCU, 1 LSU, 2 ALU, 3 EXT.
- **IMMX** (immediate-extension) syllables carry steering 0 with a 2-bit **tag** in bits 28–27 (0 ALU0, 1 ALU1, 2 LSU0, 3 LSU1) and a 27-bit payload.
- 8 **issue slots**: BCU0/1, ALU0/1, LSU0/1, EXT0/1. Binary layout places main syllables in that fixed slot order; ALU/LSU IMMX syllables come **last** in the bundle (LSB payload first when two IMMX); a BCU offset-extension IMMX sits as the second syllable, right after its branch.
- Up to 16 syllables accepted in a valid bundle (18 physical).

### De-bundling algorithm (front-end)

1. Read syllables until the parallel bit clears.
2. Classify by steering; assign main syllables to issue slots in canonical order; distinguish steering-0 syllables as BCU instructions vs. IMMX extensions via tag/position.
3. Reassemble each instruction's logical word (main + IMMX payload(s)) into 32/64/96 bits matching its encoding space. Handle the two immediate schemes:
   - **Standard extension**: 10-bit → 37-bit (1 IMMX) → 64-bit (2 IMMX).
   - **"Magic immediates"**: ALU 32-bit signed and SIMD 32-bit splatted, where the 6-bit register specifier is reinterpreted as 1 splat bit + 5 payload bits (+ IMMX 27-bit payload = 32 bits, sign-extended or replicated to lane width).
4. Decode each reassembled word through the `Decoding.table` tree for its encoding space.

### PC semantics

PC-relative instructions are based on the PC of their **own first syllable**, not the bundle base — LVX uses per-instruction PC (confirmed by the assembler). gem5's PC/branch-target handling must reflect this.

## Strategy: generation-first via a new `BE/GEM5` MDS back-end

The LVX MDS has no ISS/QEMU/gem5 back-end (KVX has ISS + QEMU). We add the missing one. It reuses the exact sources that already feed the validated ISS:

- **Decode** — translate `Decoding.table` into gem5 nested-switch decoders, one per encoding space, instantiating per-opcode `StaticInst`s.
- **Semantics** — reuse `Behavior.pm`'s `CodeGen` tree-walker (as `BE/LAO/Behavior.pl` does) to emit each opcode's **fetch → execute → commit** bodies.

The fetch/execute/commit phase split already encodes VLIW **read-all-then-write-all-at-bundle-boundary** semantics, so the register-swap-in-a-bundle hazard is handled by the model rather than by bespoke gem5 code. Everything expensive (per-opcode semantics for the whole ISA) is generated from the source of truth and stays in sync as the ISA evolves.

## Three work layers

### Layer A — Generator (`MDS/BE/GEM5`, in `lvx-mds`)
- Driver modeled on `Behavior.pl`: iterate `@Opcode::table`, parse each `<Behavior>`, `Simplify`, `CodeGen`, assemble fetch/execute/commit.
- **Retarget the emitted leaf API** from the LAO runtime (`Processor` / `OperandDecoded` / `HELPER(operandFromRegFile_*)` / `Int256_`) to gem5 `ExecContext` idioms (`readIntRegOperand`/`setIntRegOperand`, `readMem`/`writeMem`, `setNextPC`). The tree-walker stays intact; only leaf emission changes. **This is the main new code and it is finite.**
- Emit the decode tree from `Decoding.table`, and per-opcode `StaticInst` subclasses with correct source/dest register operand lists (from `operands`/`properties`) so gem5's dependency bookkeeping works for later timing CPUs.

### Layer B — gem5 `arch/lvx` runtime + glue (hand-written, small)
- The **runtime abstraction** the generated code targets: register-file read/write, memory read/write, control (PC) writes → `setNextPC`, the `Int256_` type for SIMD (mapped to gem5 vector regs / `lvx-modes` OI). Written once; ISS/LAO runtime is the reference.
- The **`APPLY.<helper>` library**: a finite, enumerable set (FP ops, `MEM_atomic_add`, …) collected from the generator's helper set. This is the irreducible hand-written core; port from KVX `helpers_core.h`.
- Standard gem5 plumbing: `ISA` object, register classes (r0–r63 int; SFR/XCR as misc regs), `Process`/ABI, `scall` → gem5 `SyscallDesc` table, and `EM_LVX` recognition in gem5's ELF loader.

### Layer C — Bundle front-end (hand-written, table-guided)
- De-bundling + instruction reassembly + microop sequencing per the algorithm above, driven by `lvx_VLIWInstructionBundling.tex` with `tc-lvx.c` as the inverse cross-check. Least generator leverage; the one genuinely new mechanism.

## Phases

- **Phase 0 — Skeleton + generator spike.** Pin gem5 as a submodule; empty `arch/lvx` that builds; recognize `EM_LVX`. Stand up `BE/GEM5`; emit decode + fetch/execute/commit for a trivial opcode (`await`/`nop`). **De-risks the whole effort** — proves `CodeGen` and `Decoding.table` translation retarget cleanly.
- **Phase 1 — Runtime + scalar integer core (generated) + bundle front-end.** Layer B runtime + core helpers; generate decode+semantics for integer ALU / immediates / load-store / compare / control flow / `scall`; implement Layer C incl. IMMX reassembly and magic immediates.
  - **Milestone:** `-O0` C "hello world" from `lvx-mbr-gcc` runs to a correct exit code; register-swap-in-a-bundle and 64-bit-IMMX-immediate tests pass.
- **Phase 2 — Full scalar + FP + syscalls.** Complete the helper library (FP) and the newlib syscall surface; wire gem5's remote GDB stub for `lvx-gdb`.
  - **Milestone:** portable C test corpus runs, outputs diffed against native x86, at `-O0/-O1/-O2` — the compiler-validation harness.
- **Phase 3 — SIMD/vector.** `Int256_`/vector-mode mapping (incl. splatted magic immediates); semantics regenerate for free.
- **Phase 4 — Timing (optional, separable).** `TimingSimpleCPU`, then O3 with the scheduling automaton if cycle modeling is ever needed. Not required for compiler validation.

## Risks

- **`CodeGen` retargeting** (LAO runtime → gem5 `ExecContext`) is the critical path — settled by the Phase-0 spike. Fallback: implement the LAO runtime API as a thin gem5 shim and reuse `BE/LAO` C output near-verbatim (less generator work, more runtime shim).
- **De-bundling correctness** — IMMX tag/steering reassembly, magic immediates, per-instruction PC. Now cross-checkable against one authoritative doc plus the assembler.
- **`Int256_`/SIMD mapping** to gem5 vector regs — deferred to Phase 3.
- **No golden LVX simulator** for differential validation → native-x86 diffing + per-helper unit tests + `lvx-gdb`. (Cross-checking against the KVX ISS is possible only at the helper-semantics level, since encodings differ.)

## Validation

- Differential testing against native x86 for portable C programs (compare stdout / exit code).
- Per-instruction / per-helper unit tests with hand-computed expected results, cross-checked against KVX ISS helpers where semantics match.
- `lvx-gdb` over gem5's remote GDB stub for interactive debugging.
- Reuse the binutils gas/ld LVX test corpus as assembled inputs.

## Open decisions

1. Confirm **generation-first via `BE/GEM5`** (vs. the LAO-runtime-shim fallback) and **SE-mode-only** scope.
2. Confirm the gem5 baseline to pin as a submodule (latest stable release unless a specific version is preferred).

## Recommended first step

**Phase 0**: the `CodeGen` + `Decoding.table` retargeting spike on a handful of opcodes. Its outcome sizes the rest of the project.
