# LVX gem5 targeting — status

Snapshot of the `arch/lvx` SE-mode port. See `PORTING-PLAN.md` for the strategy
and `PHASE0-FINDINGS.md` for the reuse-LAO spike. Updated 2026-08-02 (the body has
been extended well past that date — hosted C and the full scalar-FP surface landed
2026-09-14).

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

- Bundle de-bundling, IMMX/multi-syllable reassembly, per-instruction PC, at
  the full architectural bundle width of **18 syllables** -- the ten issue slots
  plus IMMX's eight, i.e. `LVX_MAXBUNDLEWORDS`. That bound was 16 until
  2026-10-09, two short because it had been derived by hand from eight issue
  slots instead of ten, and overflow ended the bundle *while the parallel bit
  still said more syllables followed*: a 17- or 18-syllable bundle executed as
  two, one extra issue and one extra cycle, exit code 0, no diagnostic. Past 18
  the words are not an LVX bundle at all and the decoder now panics naming the
  fetch address. `tests/lvx/diff/check_bundle_syllables.sh` pins all of it,
  including a negative control proving `simInsts` can see a split at all.
  Note gas will not assemble such a bundle -- its resource table caps one near
  15 syllables -- so the test splices the syllables itself.
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
  ABI (see `../docs/lvx-isa.md`), enough for hosted stdio. `close` deliberately
  reports success without acting on fds 0..2: SE-mode passes guest descriptors
  through to the host, and newlib's exit-time stdio cleanup would otherwise close
  the simulator's own stdout. Covered by `tests/lvx/scall.c` (12 checks).

## Behavior helpers implemented in the shim

The generated bodies call operator helpers; those not implemented are panic stubs
(`helper_stubs.inc`, `lvx_behavior_unimpl`). Reaching one now prints *which*
helper is missing and how to implement it, then `abort()`s. It used to be a bare
`__builtin_trap()` — a `ud2`, so SIGILL with no message, no PC and an empty
`stats.txt`; the only clue was the `.cold` section of whichever body had inlined
it, which is why the `wfxl` case below was misdiagnosed as a compiler bug more
than once. A helper is implemented by
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
  `Register@raccess/@waccess` and `BitField@owners/@rerror/@werror`); the walk
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

### Timing (`LVX_CPU=minor`)

**Per-operand read stages** landed 2026-10-17 too: a store's stored value, an
atomic's, and the integer MAC's accumulator are read at E1 rather than RR, so
the dependence into them is a cycle shorter — what lvx-gcc writes as a
`define_bypass`. An `fmulw` feeding a store is now 3 cycles away, not 4, and a
self-accumulating `maddw` chain costs 1 a step, not 2.

The **load shadow** landed 2026-10-17: `LvxMinorCPU` sets MinorCPU's new
`nonBlockingFuncUnits`, so an instruction leaves its functional unit when its
own latency expires instead of holding it against everything behind it. Work
placed in a long operation's shadow now costs nothing, which it could not
before — see `docs/register-buffer-deps.md`, *Implementing a load shadow*,
for why two other candidate fixes do not work.

Three timing defects were fixed on 2026-10-16 and one named, all in
`docs/register-buffer-deps.md` and
`../docs/handoff-mds-load-variants.md`; `tests/lvx/diff/check_timing.sh`
pins what was fixed. Fixed: the **GPR tuples** `PGR`/`QGR` were untracked, so
599 lvx_v2 opcodes — `lq`, `lo` and every packed SIMD result — had no
destination register and therefore no latency; an **uncached load** (`ld.u`,
`ld.us`) returns at SR, a latency of 15, not the L1's 3; and a **taken branch**
now costs `Processor@minTaken`, charged as a bubble on issue that a pending
result latency elapses against rather than adding to.

Still open, in order of size:

- **The hardware-loop back edge costs ~6 cycles instead of 1.** (It is at
  least free of the *taken-branch penalty* since 2026-10-17 — see
  `freeRedirect` — but it is still invisible to Minor's branch predictor, so
  every back edge is an unpredicted redirect paying the front-end refill.) It is invisible
  to Minor's branch predictor (the bundle holds no branch), so every back edge
  is an unpredicted redirect paying the full front-end refill — which makes a
  `LOOPDO` loop look dearer than the rotated branch loop it replaces. This is
  the inverse of the architecture and the largest remaining error.
- **A conditional branch should cost one cycle more than an unconditional one**
  (`Opcode@timing`'s `%0: Transfer.c` — `cb`/`igoto`/`icall` resolve in RR, not
  ID). The ISS charges `minTaken` for both, so a `cb` is one cycle cheap.
- **A mispredicted branch pays Minor's refill on top of `minTaken`.** The
  predictor is kept on deliberately, as a stand-in for an LVX fetch engine that
  keeps up; the residue is the first execution of each branch and each loop
  exit, so a warm loop is unaffected.
- **A result's latency is the whole bundle's**, so a bundle holding both an
  `ffmaw` and an `addd` marks both ready at the maximum of the two. This is
  the destination counterpart of the per-source read stages (done), and the
  one thing in this list that does need the micro-op split.
- **`XVR` and the RISC-V `RV` register file are still untracked**, so an
  extension-unit or RV64G dependence stalls nothing.

### Simulation speed

`Decoder::decode` used to build a fresh `LvxStaticInst` on **every fetch** --
splitting the bundle by steering, reassembling IMMX, running the generated
operand decode per instruction and building the bundle's register lists -- so
a loop paid for its body's decode on every trip, and anything added to that
constructor was paid per dynamic instruction. Since 2026-10-17 it goes through
`GenericISA::BasicDecodeCache`, keyed on `ExtMachInst`, whose `operator==` and
`std::hash` the port had carried unused since it was written. Worth
**1.14-1.28x** of host instruction rate, measured under the atomic CPU on both
microbenchmarks and real compiled code (`divmodvx`: 603k -> 770k inst/s), and
it changes no cycle count -- verified identical on twelve programs.
`tests/lvx/diff/check_decode_cache.sh` pins the property that makes the
sharing sound.

### Behavior helpers

**25 helpers remain panic stubs on `lvx_v2`** (the superset core), 22 on `lvx_v1`
— measured 2026-10-08 from `lvx-mds`'s `lvx-refs/BE/GEM5/<core>/helper_stubs.inc`,
which is per core and independent of whichever core `generated/` currently holds.
(35 stub *lines* on lvx_v2: a few helpers appear at more than one signature, so
the grep below counts lines, not names.)

This said 60, which was the sum of the groups below when they were written. Two
groups have since been implemented in full and three are partly done, so the
counts in them are upper bounds; each now says what actually remains. Get the
live inventory with:

```sh
grep -B0 lvx_behavior_unimpl src/arch/lvx/generated/helper_stubs.inc |
  grep -oE 'HELPER\([a-z0-9_]+\)'
```

- **`wfxl`/`wfxm` — reachable from hand-written assembly only, as of
  2026-10-08.** These are the general-purpose system-register field writes. Until
  today they were *the* stub a hosted C program reached: newlib's `fenv` was plain
  C over `wfxl $cs`, so `fesetround`/`feclearexcept` aborted the simulator and all
  rounding-mode and exception-flag testing was blocked.
  **That is fixed, and not by implementing this stub.** newlib now uses the ISA's
  dedicated native accessors — `frrm`/`fsrm` for the rounding mode,
  `frflags`/`fsflags` for the exception flags (format `BCU_CSR`,
  `encoding: simple`; of that family only `csrrw`/`csrrs`/`csrrc` are *also*
  RISC-V, so this is no RISC-V dependency). Their generated bodies call **no
  helper at all**, so they need nothing here. Using a generic SFR write where a
  dedicated instruction exists was the actual defect; see
  `lvx-csw/validation/SIMDE.md` and lvx-newlib `d4a84ad`.
  So these are **no longer the highest-value stub** — nothing in C reaches them —
  but still worth implementing, since hand-written assembly touching `$cs` will
  abort. Note the implemented `wfxl_check_access`/`wfxm_check_access` listed above
  are the permission checks, not the operations themselves.
- **Atomics beyond CAS** — ***all implemented, none remain*** (was 12): `MEM_atomic_{add,and,eor,ior,max,maxu,min,minu,
  swap,dus,load,store}` — the `ALADD*`/`ASWAP*` families. CAS is done, and GCC's
  `__atomic_fetch_add` expands without reaching these, so today they are only
  reachable from hand-written assembly.
- **SIMD / vector helper bodies** — **5 remain** of 12 (`bits2bytes`,
  `insert_64`, `join_64_x4`, `reflect_32`, `crc32_be_u32`; `blend`,
  `lanecond_*` and `intcomp_*` are done): `blend`, `lanecond_{8,16,32}`,
  `intcomp_{8,16,128}`, `bits2bytes`, `insert_64`, `join_64_x4`, `reflect_32`,
  `crc32_be_u32`. Note GCC does not use `intcomp_128` — it expands `__int128`
  compares into 64-bit pieces, which `tests/lvx/diff/c/i128.c` pins.
- **Cache and TLB maintenance** — **7 remain** of 17 (`probetlb`, `readtlb`,
  `writetlb`, `invaldtlb`, `invalitlb`, `dinvallsw_owner`, `mmi_owner`; the
  `MEM_d*`/`MEM_i1inval*` family is done): `MEM_d{1inval,flushl,flushlsw,invall,
  invallsw,purgel,purgelsw,touchl}`, `MEM_i1inval{,s}`, `probetlb`, `readtlb`,
  `writetlb`, `invaldtlb`, `invalitlb`, `dinvallsw_owner`, `mmi_owner`. Bare-metal
  concerns with no SE-mode meaning.
- **Privileged / system** — **8 remain** of 10 (`throw_OPCODE` and
  `throw_PRIVILEGE` are done): `rfe`, `waitit`, `idle`, `break`,
  `throw_OPCODE`, `throw_PRIVILEGE`, `syncgroup`, and the `rfe_owner`,
  `stop_owner`, `syncgroup_owner` variants — full-system, out of scope here.
- **Complex FP** — **all 3 remain**, but the `lvx_v2`-only packed f16<->i16
  conversions noted with them are ***implemented***: `fmulc_32_32`/`ffmac_32_32`/`fconj_32_32` (FMULWC/FFMAWC,
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
