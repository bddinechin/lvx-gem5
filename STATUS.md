# LVX gem5 targeting — status

Snapshot of the `arch/lvx` SE-mode port. See `PORTING-PLAN.md` for the strategy
and `PHASE0-FINDINGS.md` for the reuse-LAO spike. Updated 2026-07-31.

## Where it stands

**SE-mode functional simulation runs `lvx-mbr-gcc`-compiled C.** A validation
harness (`../validation/`, native-x86 differential) confirms correct execution of
programs exercising loops, shifts, bitwise ops, integer comparisons, branches,
function calls, and the kv4-v1 prologue/epilogue — output matched against the same
C compiled and run natively on x86.

The architecture is unchanged from the plan: the MDS `BE/GEM5` output
(`behavior_bodies.inc` + `Decode.c`) is compiled verbatim as C (Layer A), over a
hand-written runtime shim (Layer B) and bundle front-end (Layer C). Nothing is
re-derived; regenerate `generated/` via `regen.sh` / `make -C
lvx-mds/build_lvx/BE/GEM5 install`.

## What works

- Bundle de-bundling, IMMX/multi-syllable reassembly, per-instruction PC.
- Decode → fetch/execute/commit with bundle-boundary commit (read-all-then-
  write-all; the register-swap-in-a-bundle hazard is handled structurally).
- Integer ALU, immediates (incl. >32-bit via IMMX), loads/stores, comparisons,
  conditional branches, `call`/`ret`, `get`/`set` of system registers.
- **Scalar floating point** (f16/f32/f64): full IEEE arithmetic, conversions,
  compares, min/max, classify, and reciprocal seeds — see "Behavior helpers".
- Syscalls: `exit` (#1) and `write` (#17), enough to run and check freestanding
  programs; kv4-v1 ABI (args r0..r7, return r0).

## Behavior helpers implemented in the shim

The generated bodies call operator helpers; those not implemented are panic stubs
(`helper_stubs.inc`, `lvx_behavior_unimpl` → trap). A helper is implemented by
listing it in the `BE/GEM5` `shim-helpers` manifest (in `lvx-mds`) and defining
`Behavior_<name>` in the shim — `helper-stubs.pl` then emits a prototype for it
instead of a stub. Implemented:

- **Core + control/system** (`shim.cc`): the operand/register/storage/memory/
  syscall set, plus `branch_info`, `srhpc_update`, `bcucond`, `intcomp_32/64`,
  `ccbcomp`, `get`, and the `get/set/wfxl/wfxm_check_access` permission checks
  (SE permits all). Scalar-integer control/compare semantics mirror
  `../../kv4-csw/lao/LAO/kvx/Behavior.c`.
- **Floating point** (`shim_fp.cc`, over Berkeley SoftFloat in `ext/softfloat`,
  RISC-V specialization): the **complete scalar f16/f32/f64 surface** —
  arithmetic (add/sub/mul/fma/div/sqrt/rint), min/max (all four RISC-V variants),
  `floatcomp`, `classify`, the fW↔integer and fW↔fW' conversions, and the
  `vfrec7`/`vfrsqrt7` reciprocal seeds. All match RISC-V FP exactly (see the
  `lvx-fp-matches-riscv` project-memory note); the three widths are generated
  from width macros. Self-checking tests in `tests/lvx/diff/` (`fp_cmp.s`,
  `conv_cmp.s`, `fcomp_cmp.s`, `fp32_cmp.s`, `fp16_cmp.s`, `fast_cmp.s`, driven by
  `check_*.sh`) pass on both cores. The old `fpu_crash_repro.s` (any FP op used to
  SIGILL the gem5 process via the trap stub) now runs.

## Known gaps / next

- **Scalar FP is done; the remaining FP stubs are:** the complex-FP ops
  `fmulc_32_32`/`ffmac_32_32`/`fconj_32_32` (FMULWC/FFMAWC, both cores) and the
  `lvx_v2`-only packed f16↔i16 conversions (`f16_to_i16`/`_ui16`,
  `i16_to_f16`/`ui16_to_f16`). Both decompose into standard ops and are
  implementable RISC-V-conformantly.
- **SIMD / vector helper bodies** (XVR/XBR/XCR register-file access, shuffles,
  lane predicates) plus **atomics** and **cache/TLB maintenance** are still panic
  stubs.
- **Not an ISS bug, but it blocks some tests:** `lvx-gcc` lowers integer `/` and
  `%` to an FP-reciprocal sequence emitting mnemonics the assembler rejects. That
  is compiler-side work (separate).
- Timing/O3, full-system mode: out of scope (SE functional only).

## Fixes landed reaching this milestone (2026-07-18)

1. Implemented the control/compare/system-register helpers above (they were panic
   stubs, so `call`/`ret`/branches/`get $ra`/compares crashed with SIGILL).
2. **`operands.c` register decode**: a register operand's raw field is its
   position *within its file*, so it must decode as `filebase + raw`. The compact
   `rc_<class>[raw]` lookup only works for full-file-in-order classes; the
   `onlysetReg` view omits the non-settable PC, so `set $ra` wrote the wrong SFR
   (misc reg 4/CS instead of 3/RA), looping every function epilogue. Fixed with a
   per-class file base (SFR → PC; other files keep the compact table).
