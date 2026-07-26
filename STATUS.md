# LVX gem5 targeting — status

Snapshot of the `arch/lvx` SE-mode port. See `PORTING-PLAN.md` for the strategy
and `PHASE0-FINDINGS.md` for the reuse-LAO spike. Updated 2026-07-18.

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
- Syscalls: `exit` (#1) and `write` (#17), enough to run and check freestanding
  programs; kv4-v1 ABI (args r0..r7, return r0).

## Behavior helpers implemented in the shim

The generated bodies call operator helpers; those not implemented are panic stubs
(`helper_stubs.inc`, `lvx_behavior_unimpl` → trap). Implemented so far (in
`shim.cc`, and excluded from the generated stubs via the `BE/GEM5` `shim-helpers`
manifest in `lvx-mds`): the core operand/register/storage/memory/syscall set,
plus `branch_info`, `srhpc_update`, `bcucond`, `intcomp_32/64`, `get`, and the
`get/set/wfxl/wfxm_check_access` permission checks (SE permits all). Scalar-integer
control and comparison semantics mirror `../epi-csw/lao/LAO/kvx/Behavior.c`.

## Known gaps / next

- **FP / SIMD helper bodies are still panic stubs** — the `f16/f32/f64_*` family,
  atomics, `wfxl/wfxm`, cache/TLB maintenance. Wire these (SoftFloat / the KVX
  helper reference) as the compilers start emitting FP.
- **Not an ISS bug, but it blocks some tests:** `lvx-gcc` lowers integer `/` and
  `%` to an FP-reciprocal sequence emitting `frecw.rn`/`fwidenlwd` — removed
  mnemonics the assembler rejects. That is compiler-side work (separate).
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
