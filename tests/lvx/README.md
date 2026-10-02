# LVX gem5 SE-mode smoke tests

Minimal freestanding LVX programs that exercise the decode → shim → execute
pipeline end to end and exit via `scall` (LVX syscall ABI: number in the
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
- `mk64`   → `code=188`  — a >32-bit `maked` (double/triple encoding, IMMX operand)
- `par`    → `code=188`  — two `maked`s in one bundle (ALU0/ALU1), summed in the next
- `bundle` → `code=7`    — mixed: triple `maked` + a 2-instruction bundle

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
- `fsign` → `code=9` — the `FSIGN*` family (RISC-V `FSGNJ`/`FSGNJN`/`FSGNJX`)
  at all three scalar widths.  Nine checks, one per mnemonic, each adding 1 to
  the exit code, so the code is a pass count and a crashed run (0) is
  distinguishable from success.  Covers two ISA-description bugs: a non-sign
  mask that was one bit too narrow (`0x3FFF..` for `0x7FFF..`, clearing the top
  exponent bit), and a stray `NOT` in `FSIGNM*`'s `behavior:` that its
  `execution:` C did not have, which inverted the resulting sign.  Against an
  ISS carrying the latter, this test returns `code=6` — the three `FSIGNM*`
  checks fail, one per width.  Neither bug was reachable by the C-vs-ISS
  differential harness: the mask was wrong identically in both descriptions,
  and nothing else in this suite executes an `fsign*`.
- `fcompd_crash_repro` → **also fixed as of 2026-07-30**
  (`Behavior_floatcomp_64` added to `shim_fp.cc`), same day it was found.
  `code=1` as expected. Floating-point comparisons (`fcompd`/`fcompw`)
  were a narrower follow-on gap the arithmetic fix above didn't cover --
  see the file's own comments.

## Round-to-integral, and the flag that separates the two instructions
- `fround` → `code=10`.  `FROUND*` is round-to-integral *without* signalling
  inexact, beside `FRINT*`, which signals it: IEEE-754's roundToIntegral and
  roundToIntegralExact, C's `nearbyint` and `rint`, RISC-V Zfa's `fround` and
  `froundnx`.  The only difference between the two instructions is that one
  flag, so the only test that tells them apart is one that reads `$cs` —
  checks 1 and 2 are the same value through both, with `fflags` read after
  each.

  Checks 5–8 are the directed modifiers, which are the point of the new
  instruction: C requires `nearbyint`, `floor`, `ceil`, `trunc`, `round` and
  `roundeven` to leave the inexact flag alone, so none of the six could be
  built from `FRINT*` — lvx-gcc's `scalar.md` said exactly that and provided
  only `rint()`, and `roundeven` was a ten-instruction emulation that saved
  `$cs`, converted out to an integer and back, and restored it.  The full
  optab mapping is in `lvx-csw/docs/lvx-codegen.md` section 3b.

  `fflags` is reached with the RISC-V CSR instructions, which are an atomic
  self-swap: `csrrw $rX = 1` writes `$rX` into `fflags` and leaves the old
  value in `$rX`; `csrrs $rX = 1` with `$rX` zero reads it without changing it.

## System-call regression test
- `scall` → `code=0` — the SE-mode syscall shim (`Behavior_syscall` in
  `src/arch/lvx/shim.cc`).  Written in C rather than assembly, since the point
  is the shim rather than the decoder, and built freestanding against the same
  `diff/crt0_mbr.S` the differential harness uses:

  ```bash
  lvx-mbr-as   tests/lvx/diff/crt0_mbr.S -o crt0.o
  lvx-mbr-gcc  -c -O2 -ffreestanding -fno-builtin -nostdlib tests/lvx/scall.c -o scall.o
  lvx-mbr-ld   crt0.o scall.o -o scall.elf
  build/LVX/gem5.opt tests/lvx/run_lvx.py scall.elf
  ```

  Twelve checks over a real file in `/tmp`: `open`, `write`, `lseek`, `read`
  (comparing the bytes back), `fstat` (checking `st_size` through the
  `uint64_t[13]` result array libgloss expects), `isatty`, `close`, `stat`,
  `access`, `unlink`, `access` again for `-ENOENT`, and an unimplemented number
  for `-ENOSYS`.  The exit code is the number of the first failing step, so a
  regression names itself; `code=0` means all twelve passed.  The run also
  prints one expected `unhandled scall #99` warning, from the last check.

  The syscall numbers and the `S_*` open-flag encoding are the target's, owned
  by lvx-newlib (`newlib/libc/sys/mbr/include/mbr/lvx/scall_no.h`) and issued by
  `libgloss/lvx-mbr/asm_syscalls.S`.  Keep the shim and that header in sync.

## RV64G personality (the `PS.RV` mode)

In `rv/`, built by the upstream `riscv64-unknown-elf` toolchain (ADR-0004: LVX
assembles and compiles no RISC-V) -- see `../../../riscv-toolchain/README.md`
for where that comes from and the one trap in using it.

```bash
make -C tests/lvx/rv check                                 # on gem5-lvx1.opt
make -C tests/lvx/rv check GEM5=$PWD/build/gem5-lvx2.opt   # on gem5-lvx2.opt
```

`check` is `run` (the self-checking programs), then `diff` (the host FP
oracle), then `spike` (the stronger one).

- `rv64i` -> `rv64i ok` on stdout, then `code=0`. Nine checks over the base
  integer set: the W forms and their sign extension from bit 31, the logical
  ops, the 64- and 32-bit shifts and their count masks, SLT/SLTU, all six
  branches, the loads and stores at every width signed and unsigned, a counted
  loop with an indexed store, and a call that reaches a static address through
  LUI/AUIPC and returns.  Each check returns its own number, so the exit code
  names the first thing that broke.  It also proves the whole entry path at
  once -- an `EM_RISCV` ELF claimed as `loader::LvxRv64`, `PCState.rv()` set
  from it, `moreBytes` fetching one fixed 32-bit word rather than a
  parallel-bit bundle, `Decode_Decoding_riscv` dispatching it, and `ecall`
  reaching `Behavior_rv_syscall` with the RISC-V ABI (number in `a7`,
  arguments in `a0..a5`, result in `a0`).
- `rv64ma` -> `rv64ma ok`, then `code=0`. M and A in eight checks: the high
  multiplies (where a wrong signedness is invisible to a plain `*`), the
  divides including RISC-V's two *defined* special cases (by zero, and
  most-negative by -1), the signed remainder's sign, the W forms' extension
  from bit 31, the read-modify-writes returning the OLD value, and a
  compare-and-swap the compiler builds out of LR/SC -- which is where an
  inverted SC result or a reservation that never holds shows up, as a retry
  loop that either never terminates or never stores.  Check 3 is the negative
  dividend that caught both of KV4's divide operators being the floored pair
  (lvx-mds 7b9b785).
- `rv64csr` -> `rv64csr ok`, then `code=0`. Zicsr and Zifencei in eight checks
  -- and as much a test of the *sharing* as of the encoding, since the RISC-V
  `csrrw`/`csrrs`/`csrrc` are the native LVX instructions under a second
  format, over the same BitAlias dispatch (fcsr/fflags/frm onto CS,
  mepc/mtvec/mtval/mscratch onto SPC/EV/EA/SR, the machine IDs reading zero).
  Check 4 is the immediate forms with bit 4 set, which a sign-extended 5-bit
  immediate would turn into all ones; check 7 is that `x0` survived check 6's
  six `csrw`s -- `csrw csr, rs` *is* `csrrw x0, csr, rs`, and the shared
  dispatch commits unconditionally, so x0 has to be hardwired in the shim
  rather than guarded by the format.
- `fp` -> `fp: identical, 5138 words`, and it is **not** self-checking: the same
  `fp.c` is built twice, once for RV64 and once for the host, run on both, and
  the two outputs compared word for word by `fpdiff.py`.  F and D were the one
  group the port could not import as it stood -- every FP body was rewritten
  from KV4's "return a value, sweep the flags up later" shape into LVX's
  flag-tuple one -- so "it decodes" was worth very little and "it computes the
  same double as x86" was worth a lot.  It writes RAW BIT PATTERNS, not text,
  which is what makes it catch a signed zero, a NaN payload, a result rounded
  the wrong way in the last place, and a float that was never NaN-boxed.
  Coverage: the five arithmetic operations over every ordered PAIR of 20
  doubles and 16 floats (an operand swap in FSUB or FDIV shows up at once),
  sqrt, the four FMA sign combinations, min/max, the three compares, every
  conversion in both directions and both signednesses, the sign-injections, and
  two accumulation chains so a float that lost its boxing poisons everything
  after it.

  Its first run found 41 differing words of 5138 -- and **every one was the ISS
  being right**, in the three places C leaves the answer undefined or
  unspecified and RISC-V specifies it: the sign of a produced NaN (x86's
  default NaN is negative, RISC-V's canonical one positive), out-of-range and
  NaN float-to-integer conversions (x86 returns "integer indefinite" and wraps
  negatives into unsigned; RISC-V saturates), and min/max of the two zeros.
  So the host side computes RISC-V's rule for those three, rather than the test
  carrying an exclusion list -- the comparison stays exact and the three
  divergences are stated in code.
- `fpflags` -> `fpflags: identical to spike, 6074 words`, and it is the test
  the host oracle could not be: it emits a PAIR per operation, the result bits
  and the **fflags** that operation raised.  That was the weakest point of the
  F/D port -- every body was rewritten and every flag tuple re-derived, and a
  tuple element in the wrong position is invisible to any test that only looks
  at results.  Compared against **Spike**, which has the flags; see
  `../../../spike-build/README.md` for how it is built and the three ways
  running a test under it differs from gem5.

  Its inputs are chosen for the flags rather than the values, and the two that
  matter most are the ones `fp.c` does not have at all: a quiet NaN and a
  **signalling** NaN.  They are what separates the quiet comparison from the
  signalling ones, and the measured table is the proof the distinction is live
  rather than vacuously matching:

  | a | `feq` | `flt` | `fle` |
  |---|---|---|---|
  | quiet NaN | *nothing* | NV | NV |
  | signalling NaN | NV | NV | NV |
  | 1.0 | nothing | nothing | nothing |

  Had `floatcomp` stayed quiet-only, as it was before the port, the first row
  would read `nothing` three times.  907 of its 3037 flag words are non-zero,
  covering NX, NX+UF, NX+OF, DZ and NV, so the comparison has teeth throughout
  and not only on the compares.
- `zfa` -> `zfa: identical to spike, 4108 words`.  Zfa's `fround` and
  `froundnx`, which are **not** a second implementation of anything: they are
  the native LVX `FROUND*` and `FRINT*` carrying a RISC-V encoding, the way
  Zicsr's `csrrw` is the native `CSRRW` under `RVZI_CSRR`.  So what this
  checks is that the second encoding reaches the same behaviour and the two
  personalities cannot drift.

  The pair differs in one flag and nothing else, so the measured table is
  where the test earns its keep — same value from both, and NX is the only
  column that moves:

  | input | mode | `fround.d` | flags | `froundnx.d` | flags |
  |---|---|---|---|---|---|
  | 2.5 | rne | 2.0 | — | 2.0 | NX |
  | 2.5 | rup | 3.0 | — | 3.0 | NX |
  | −2.5 | rdn | −3.0 | — | −3.0 | NX |
  | −2.5 | rmm | −3.0 | — | −3.0 | NX |
  | 2.0 | rne | 2.0 | — | 2.0 | — |
  | sNaN | rne | NaN | NV | NaN | NV |

  It now covers **all of Zfa for RV64 F+D**, and what is worth checking about
  each is how it differs from the base instruction it sits beside — which the
  measured data shows:

  | | the Zfa one does | the base one does |
  |---|---|---|
  | `fminm.d(qNaN, 2.0)` | canonical NaN, no flag | `fmin.d` returns 2.0 |
  | `fminm.d(sNaN, 2.0)` | canonical NaN, NV | same |
  | `fltq.d(qNaN, 2.0)` | 0, **no flag** | `flt.d` raises NV |
  | `fcvtmod.w.d(2^31)` | −2147483648, wrapped | `fcvt.w.d` saturates to 2^31−1 |
  | `fcvtmod.w.d(2^32)` | 0, wrapped | saturates to 2^31−1 |
  | `fli.d` index 0 / 16 / 31 | −1.0 / 1.0 / canonical NaN | — |

  `fminm`/`fmaxm` and `fltq`/`fleq` are the native LVX `FMIN*`/`FMAX*` and the
  quiet `FCOMP*` re-encoded; `fli` and `fcvtmod.w.d` are RISC-V-only, having no
  native counterpart.  `fli`'s immediate lives in the *rs1 field*, so it cannot
  be a compiler-allocated operand — each of the 32 constants needs its own
  `.insn` with the index written as a register token, hence the list rather
  than a loop.

  Spike caught a real bug here that no self-checking test of mine would have:
  `fcvtmod.w.d` was not sign-extending its 32-bit result into the 64-bit
  destination (Spike's `sext32(frac)`), so four of 4108 words differed and all
  four were the high half.  A test written from my own understanding would have
  encoded the same mistake in its expected values.

  The instructions are written with `.insn`, because the installed binutils is
  2.35.1 and predates Zfa — the encoding is still the encoding, and Spike
  knows the mnemonics.  Note `SPIKE_ISA` is `rv64imafd_zfa`: without the `_zfa`
  Spike refuses the word rather than executing it.
- `slt` -> `code=7`, a bitmap rather than a pass count. It pins the one bug the
  port introduced: KV4's branch conditions were typed helpers (`comp64_lt`),
  and porting them to LVX's `(LT a b)` dropped the type -- a Behavior register
  read is unsigned at its container width, so `bgez` with -1 in the register
  branched as if -1 >= 0. Only a negative operand shows it.

The RV instruction set is now **all of RV64G**, plus the first two
instructions of **Zfa**: I, M, A, F, D, Zicsr, Zifencei, and Zfa's
`fround`/`froundnx` at both widths.  What is still missing is above the ISA -- traps,
interrupts, PMP, more than one hart -- which `docs/riscv-mode.md` in `lvx-csw`
tracks as milestones 2 and 3.  Not ported, deliberately: the B extension,
Zacas, Zabha, Zicbo and Zicond, which KV4's material also carries and the
profile of ADR-0001 does not name.
