# LVX gem5 targeting — status

Snapshot of the `arch/lvx` SE-mode port. See `PORTING-PLAN.md` for the strategy
and `PHASE0-FINDINGS.md` for the reuse-LAO spike. Updated 2026-08-02.

## Where it stands

**SE-mode functional simulation runs `lvx-mbr-gcc`-compiled C, freestanding and
hosted.** The differential harness (`tests/lvx/diff/`, native-x86 oracle) is clean
across its whole matrix — **96/96**: 12 programs × {lvx-1, lvx-2} × {-O0, -O1,
-O2, -Os} — covering loops, shifts, bitwise ops, integer comparisons, branches,
function calls, the LVX prologue/epilogue, integer division and modulo, and
`__int128`, each checked against the same C compiled and run natively on x86.

**A newlib-linked program runs end to end**: `printf` of integers and `%f`,
`malloc`, `fopen`/`fprintf`/`fgets` round-trip, libm (`sqrt`/`pow`/`exp`), and
`__atomic_fetch_add` all behave. The one hosted libc surface that still aborts the
simulator is `<fenv.h>` — see the gaps below.

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
- **`GUARD` predication**, memory fences, and **atomic compare-and-swap**
  (`ACSWAP*`). `GUARD` sits in a BCU slot and predicates the other units of its
  own bundle, so execute runs the BCU slots first and then skips the guarded
  ones; suppressing commit alone would not work, since a store writes memory
  during execute.
- The 8×8 **bit-matrix** helpers (`SBMM8*`/`SBMMT8*`), which newlib's `memset`
  reaches through its byte-splat.
- **Syscalls**: the set libgloss issues — `exit`, `write`, `read`, `open`,
  `close`, `lseek`, `fstat`, `stat`, `isatty`, `access`, `unlink` — over the LVX
  ABI (args r0..r7, return r0), enough for hosted stdio. `close` deliberately
  reports success without acting on fds 0..2: SE-mode passes guest descriptors
  through to the host, and newlib's exit-time stdio cleanup would otherwise close
  the simulator's own stdout. Covered by `tests/lvx/scall.c` (12 checks).

## Behavior helpers implemented in the shim

The generated bodies call operator helpers; those not implemented are panic stubs
(`helper_stubs.inc`, `lvx_behavior_unimpl` → trap). A helper is implemented by
listing it in the `BE/GEM5` `shim-helpers` manifest (in `lvx-mds`) and defining
`Behavior_<name>` in the shim — `helper-stubs.pl` then emits a prototype for it
instead of a stub. Implemented:

- **Core + control/system** (`shim.cc`): the operand/register/storage/memory/
  syscall set, plus `branch_info`, `srhpc_update`, `bcucond`, `intcomp_32/64`,
  `ccbcomp`, `get`, and the `get/set/wfxl/wfxm_check_access` permission checks,
  implementing the LVX scalar-integer control/compare semantics.
- **System-register ownership** (`shim.cc`, US11995218): the four
  `*_check_access` helpers are the real per-bit-field privilege check, not the
  `return true` they used to be — the current ring (PS.PL) against the ring that
  owns each field, with the field's `rerror`/`werror` deciding what a refusal
  means (read through, read as zero, drop the write, or trap). The table comes
  from MDS (`generated/ownership.inc`, from `MDS/BE/GEM5/BIN/ownership.pl` over
  `Register@raccess/@waccess` and `BitRange@owners/@rerror/@werror`); the walk
  mirrors KVX's `Behavior_default_check_access`. SE mode runs at PL0, so nothing
  the ISS runs today can be refused, which is why `tests/lvx/diff/
  check_ownership.sh` leaves PL0 on purpose. A refusal that the description says
  is a trap panics: SE mode has no ring to divert it to.
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

60 helpers remain panic stubs on `lvx_v2` (the superset core); the list below is
the whole of it, grouped. Get the current inventory with:

```sh
grep -B0 lvx_behavior_unimpl src/arch/lvx/generated/helper_stubs.inc |
  grep -oE 'HELPER\([a-z0-9_]+\)'
```

- **`wfxl`/`wfxm` — the one stub a hosted C program reaches.** newlib's `fenv`
  implementation is plain C over `wfxl $cs`, so `fesetround`/`feclearexcept`
  abort the simulator. This blocks all rounding-mode and exception-flag testing,
  which is otherwise the only untested part of an FP surface that is complete and
  RISC-V-conformant. **Highest-value stub to implement.** Note the implemented
  `wfxl_check_access`/`wfxm_check_access` listed above are the permission checks,
  not the operations themselves.
- **Atomics beyond CAS** (12): `MEM_atomic_{add,and,eor,ior,max,maxu,min,minu,
  swap,dus,load,store}` — the `ALADD*`/`ASWAP*` families. CAS is done, and GCC's
  `__atomic_fetch_add` expands without reaching these, so today they are only
  reachable from hand-written assembly.
- **SIMD / vector helper bodies** (12): `blend`, `lanecond_{8,16,32}`,
  `intcomp_{8,16,128}`, `bits2bytes`, `insert_64`, `join_64_x4`, `reflect_32`,
  `crc32_be_u32`. Note GCC does not use `intcomp_128` — it expands `__int128`
  compares into 64-bit pieces, which `tests/lvx/diff/c/i128.c` pins.
- **Cache and TLB maintenance** (17): `MEM_d{1inval,flushl,flushlsw,invall,
  invallsw,purgel,purgelsw,touchl}`, `MEM_i1inval{,s}`, `probetlb`, `readtlb`,
  `writetlb`, `invaldtlb`, `invalitlb`, `dinvallsw_owner`, `mmi_owner`. Bare-metal
  concerns with no SE-mode meaning.
- **Privileged / system** (10): `rfe`, `waitit`, `idle`, `break`,
  `throw_OPCODE`, `throw_PRIVILEGE`, `syncgroup`, and the `rfe_owner`,
  `stop_owner`, `syncgroup_owner` variants — full-system, out of scope here.
- **Complex FP** (3): `fmulc_32_32`/`ffmac_32_32`/`fconj_32_32` (FMULWC/FFMAWC,
  both cores), plus the **`lvx_v2`-only packed f16↔i16 conversions** (4):
  `f16_to_i16`/`_ui16`, `i16_to_f16`/`ui16_to_f16`. All decompose into standard
  ops and are implementable RISC-V-conformantly.
- Timing/O3, full-system mode: out of scope (SE functional only).

When implementing a helper, the KVX behavior-helper runtime is the reference the
LVX behaviors were generated against: `kv4-csw/lao/LAO/kvx/Behavior.c` and
`kv4-csw/lao/LAO/kvx/kv4/Behavior.c`. LVX signatures take their arguments already
unboxed, so the `Int256_toUInt64` calls at entry that the KVX versions carry are
not needed; only the result is boxed. Port it, then verify against an independent
host-side model rather than merely checking that it stops trapping — that is how
the bit-matrix operand order (`sbmm8d $d = s1, s2` calls `_BMM_8(s2, s1)`) was
pinned.

### Fixed since the last snapshot

- Integer `/` and `%` used to lower to an FP-reciprocal sequence whose mnemonics
  the assembler rejected. lvx-gcc emits the hardware divmod family now;
  `tests/lvx/diff/c/divmod.c` covers it, 8/8 across the matrix.
- Atomics were listed here wholesale as panic stubs; compare-and-swap is in.

## Fixes landed reaching this milestone (2026-07-18)

1. Implemented the control/compare/system-register helpers above (they were panic
   stubs, so `call`/`ret`/branches/`get $ra`/compares crashed with SIGILL).
2. **`operands.c` register decode**: a register operand's raw field is its
   position *within its file*, so it must decode as `filebase + raw`. The compact
   `rc_<class>[raw]` lookup only works for full-file-in-order classes; the
   `onlysetReg` view omits the non-settable PC, so `set $ra` wrote the wrong SFR
   (misc reg 4/CS instead of 3/RA), looping every function epilogue. Fixed with a
   per-class file base (SFR → PC; other files keep the compact table).
