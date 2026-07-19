# Register-buffer dependencies: XACCESSO/XALIGNO and cycle-accurate timing

> **Status (2026-07-19).** MDS half **implemented and verified, phases 1 and 2.**
> Phase 1 (attribution): the operand-attributed `Location`
> (`AGGL.<storage>.<proxy>`) is in the grammar and generator. Phase 2
> (operand-relative generation): `blockExpand` (MDE `Opcode.pl`) rewrites the
> attributed Location's element index into `(METHOD(%k) << log2N) + (idx & (N-1))`,
> deriving `N` from the operand's RegClass (`count(fine file)/count(buffer class)`),
> so the block size has one source of truth instead of a hand-written shift/mask;
> XACCESSO/XALIGNO lost their `buffer`/`mask`/`where` arithmetic. Because
> `METHOD(%k)` now sits inside the Location, `proxyActions` also classifies `%k`
> as a Read. **Verified value-preserving** by the `BE/LAO/TEST` differential
> harness: 1453/1453 lvx_v2 traces identical to the pre-change generation, over
> Kalray's real `Int256_`.
>
> **gem5 half — foundation implemented and verified.** A `BE/GEM5` generator
> (`reg-operands.pl`) emits `reg_operands.inc`: per opcode, the source and
> destination register operands, parsed from the behavior bodies'
> `operand{From,To}RegFile` / `{read,write}FromStorage` calls (sources may be read
> in any phase — RET reads `$ra` in *execute*). `LvxStaticInst` now builds
> `_srcRegIdx`/`_destRegIdx` from it (GPR→`intRegClass`, SFR→`miscRegClass`),
> aggregated and deduped across a bundle's instructions. Verified with a
> `LvxRegs` dump: `make/sllw/addw/addd` report exactly the right GPR src/dest
> chains (immediates excluded), `RET` its `$ra` source; functional execution
> unchanged (validation harness loops/branches/shifts still match native x86).
> Building it also required **resyncing the hand-written shim to the current
> lvx-mds helper-width ABI** (the intervening unboxing narrowed `syscall`/
> `branch_info`/`MEM_*`/… from `Int256_` to native types) and adding the missing
> `CS` storage helpers. **Remaining (follow-up):** wire `LvxMinorCPU` + FU pool +
> per-opcode `OpClass` to actually insert stalls; the lvx_v2 `bufferNReg`→N-XVR
> source expansion (needs `MaxBundleSrcRegs` raised for buffer64Reg); and implicit
> `scall` arg reads (r0..r7) are not yet in the operand model.

## The problem, precisely

`XACCESSO`/`XALIGNO` (and the `xpl*` family) read a register of the `XVR` file
selected by a **run-time** value. That register cannot be an operand, so the
Behavior description drops to the *direct storage path*:

```
buffer  = METHOD(%2) << log2(N)          ; base of the aligned N-register block
where_0 = buffer + (index_0 & (N-1))     ; index_0 = (%3 >> 5)  — run-time
(LOAD.RR (AGGL.XVR (READ.where_0) (CONST.1)))
```

`AGGL.XVR <computed>` names a **Storage**, not an operand. It therefore carries
**no operand attribution**: the Operator gets no read-Parameter for the block,
so nothing downstream that works off the instruction's read/write *register set*
can see the dependency.

This is exactly why the KVX ISS could not add stall cycles for these
instructions: its dependency tracker keyed off each operator's register
Parameters, and the direct-storage read produced none. The result was an
optimistic (too-low) cycle count for any code using the register-buffer
instructions. **We must not inherit this on the gem5 LVX target.**

## Current gem5 LVX state (as of this note)

- `LvxStaticInst` is a **whole-bundle** `StaticInst` (`No_OpClass`) that declares
  **no** `_srcRegIdx`/`_destRegIdx`. `execute()` reaches straight into the
  `ThreadContext` through the Behavior runtime. So the port has *no* register
  dependency tracking for **any** instruction, not just the buffer ones.
- Only **Atomic / NonCaching / TimingSimple** CPUs are wired (`LvxCPU.py`). None
  of these insert register-dependency stalls — `TimingSimpleCPU` models *memory*
  timing but still executes one instruction to completion. The CPUs that stall on
  register hazards are **MinorCPU** (in-order) and **O3CPU** (out-of-order);
  neither is bound, and neither would have anything to consume yet.
- `XACCESSO`/`XALIGNO` are `lvx_v2`; the port's generated tuples are `lvx_v1`
  only. So this is a *design-ahead* fix: get the attribution right in the model
  now, so the buffer instructions arrive already trackable.

The register-list machinery below is therefore foundational for **any** timing
CPU on LVX; the buffer instructions are the case that the naive version gets
wrong.

## The construct: an operand-attributed `Location` (phase 1, implemented)

The direct path is `LOAD`/`STORE` over a `Location = (AGGL|AGGB storage address
extent)`. The one thing wrong with the buffer read is that the `Location` names a
**Storage** and nothing ties it to the operand whose block it accesses. So the
minimal, provably value-preserving change is to let a `Location` **also name the
operand**, keeping the storage and the (still hand-computed) address exactly as
they are. `Storage` is an `Ident` and `Proxy` is the `PROXY` token, so the forms
are lexically disjoint — no ambiguity.

### Grammar (`MDS/DOC/Behavior.y`) — as landed

```
Location
	: '(' AGGL '.' Storage Address Extent ')'
	| '(' AGGB '.' Storage Address Extent ')'
	| '(' AGGL '.' Storage '.' Proxy Address Extent ')'   /* NEW: attributed */
	| '(' AGGB '.' Storage '.' Proxy Address Extent ')'
	;
```

`LIB/Behavior.pa`: the `AGGL`/`AGGB` builders take an optional `$proxy` and store
it in **slot 4** of the node — invisible to `CodeGen` and `Width.pm`, which read
`[1..3]`, so the emitted C is untouched. A special case in `pretty` renders the
attributed node as `(AGGL.storage.proxy addr extent)` so it re-parses through the
new rule (the tree round-trips `Opcode.table`, which `BE/LAO` re-reads).
Regenerate `LIB/Behavior.pm` with `make -f Maintainer Behavior.pm` (any bison;
the yaxcc normalization makes it deterministic — verified byte-identical on a
second run).

### `XACCESSO`/`XALIGNO` as changed

Only the two block-read `Location`s gain the attribution; everything else — the
`buffer`/`mask`/`where` prologue arithmetic — is unchanged:

```
(WRITE.result1_0 (F2I.256 (LOAD.RR (AGGL.XVR.%2 (READ.where_0) (CONST.1)))))
(WRITE.result1_1 (F2I.256 (LOAD.RR (AGGL.XVR.%2 (READ.where_1) (CONST.1)))))
```

`%2` is the `bufferNReg` operand. It now appears *inside the Location*, which is
what marks this a **block value-read** rather than an anonymous storage read — the
single fact the KVX ISS's dependency tracker lacked. `where_0`/`where_1` stay the
run-time addresses; nothing about the value computation moves.

### Why this and not indexed `ACCESS`/`COMMIT` (an earlier idea)

- `COMMIT.<stage>.<proxy> Integer [Mask]` already exists; an indexed
  `COMMIT.<proxy> Integer(index) Integer(value)` collides with the `Mask` form.
- `ACCESS`/`COMMIT` are the *operand-path* spellings, whose contract is a
  decode-time register. Overloading them to carry a run-time index muddies that.
- The buffer read **is** a direct-path access; attributing the `Location` fixes
  exactly the defect (no operand attribution) and keeps the value semantics
  byte-identical, which an indexed `ACCESS` would not.

### Verification (harness-free, rigorous)

Because `CodeGen` ignores slot 4, the functional C cannot change; proved directly
rather than argued. Build the current sources twice — with and without the
`.%2` on `Instruction.yml` (grammar/`.pm` changes present in both, so their effect
is isolated) — and diff the regenerated `lvx_v2 Behavior.tuple`:

- **24 changed lines, all `(AGGL.XVR` → `(AGGL.XVR.%2`**, on exactly the 12
  block-read nodes (2 reads × 6 buffer sizes). **Zero non-`.%2` changes.**
- All 12 sit **inside `/* */` S-expr comments** (in-comment 12, in-code 0): the
  emitted C is byte-identical, so the `BE/LAO/TEST` lvx_v2 trace is unchanged by
  construction — no need to run the harness to know it stays 0/1453.
- **lvx_v1 `Behavior.tuple` byte-identical** — the grammar addition is inert on
  the core that uses no attributed `Location` (control for the `.pm` change).

### Phase 2 (implemented): operand-*relative* generation

`blockExpand` (`MDS/MDD/MDE/BIN/Opcode.pl`, run right after `Normalize`) rewrites
each attributed Location whose address is an *element index*:

```
(AGGL.XVR.%2 (READ.index_0) 1)
  →  (AGGL.XVR.%2 (ADD (SHL (METHOD.%2) log2N) (AND (READ.index_0) (N-1))) 1)
```

`N` is derived, not written: `N = count(fine file) / count(%k's RegClass)`, where
the fine file is the RegClass carrying `regFileName == <storage>` (here `XVR` =
`xwordoReg`, 64 registers) and `%2`'s class is `buffer2Reg` (32) → `N = 2`,
`log2N = 1`. So the block size has a single source of truth — the operand's
RegClass — instead of the hand-written `WRITE.mask (CONST.N-1)` / `SHL … log2N`
that the six `BIA<N>` prologues carried; those, and `where_0`/`where_1`, are
deleted. `blockExpand` asserts `N` is integral and a power of two, so a mismatched
`bufferNReg`/storage pairing fails the build loudly.

Two payoffs beyond deleting the arithmetic:

- **The `Width.pm` bound is now free.** The generated address is
  `(METHOD ∈ [0,31]) << 1 + (idx & 1) ∈ [0,63]`, provably inside `XVR`'s 64
  registers — the narrow type the analysis needs, with no hand-written `ZX.256`.
- **`%k` becomes a tracked read.** `METHOD(%k)` now sits inside the LOAD Location,
  so `proxyActions` (the operand read/write inference) classifies `%k` as a Read
  at the LOAD's stage — the dependency, now visible to that inference too, not
  only to a tree walk over slot 4.

**Verified value-preserving** by the `BE/LAO/TEST` differential harness against
Kalray's `Int256_`: the pre-change generation and the phase-2 generation produce
**1453/1453 identical `lvx_v2` traces**. Since phase 2 changes the emitted C (the
address arithmetic is inlined into `readFromStorage_XVR` instead of staged through
`where_0`), the harness — not a comment-only diff — is what proves equivalence.

## The gem5 side: register lists + a timing CPU

### 1. Emit `_srcRegIdx`/`_destRegIdx` from decoded operands

`LvxStaticInst` (or per-`SubInst`) must publish RegIds so a timing CPU can build
the dependency graph. The Operator's read/write Parameters — which the
operand-attributed `Location` now populates for buffer reads — are the source of
truth. A `BE/GEM5` generator emits, per opcode, the operand→role map; the
constructor walks it:

```cpp
// pseudo-code, in LvxStaticInst after lvx_decode_operands():
for (each operand k of si.opcode) {
    switch (operandRole(si.opcode, k)) {     // generated: READ / WRITE / ENCODING
      case WRITE:
        for (RegId r : registersOf(k, si.decoded))   // block-expanded, see below
            addDest(r);
        break;
      case READ:
        for (RegId r : registersOf(k, si.decoded))
            addSrc(r);
        break;
      case ENCODING: break;                  // METHOD-only: no value dependency
    }
}
```

`ENCODING` is the key distinction: an operand used only via `METHOD` (e.g. a
shift-amount immediate, or the buffer base *when it were still METHOD-only*)
creates no register dependency; an operand appearing in an `ACCESS`/`COMMIT` or
inside a `Location` does.

### 2. A buffer operand expands to its N aligned XVR registers

`registersOf(%2)` for a `bufferNReg` returns the **whole aligned block**:

```cpp
// base in XVR-index space = decoded block ordinal << log2(N)
unsigned base = decodedBlockOrdinal(k) << log2N(k);
for (unsigned j = 0; j < N(k); ++j)
    out.push_back(RegId(LvxXvrRegClass, base + j));
```

We depend on **all N**, not the two elements actually read: the index is a
run-time value, so at decode any element of the block may be the source, and the
aligned block is the hardware dependency granule (it is produced as a unit). This
is accurate, not pessimistic — the block is filled before it is accessed.

### 3. The capacity caveat for large buffers

`buffer64Reg` expands to **64** source RegIds, which exceeds gem5's per-inst
source-register capacity (`MaxInstSrcRegs`, and the bundle aggregates several
sub-insts). Three honest options, in order of preference:

1. **Raise `LvxISA`'s `MaxInstSrcRegs`/`MaxInstDestRegs`** to cover the largest
   block plus the rest of a bundle. Costs a bit of per-inst memory; correct.
2. **Model the `XVR` file with block-granular RegIds** so a `bufferNReg` is one
   RegId — but this needs the scoreboard to see that a write to `XVR[i]`
   invalidates every block RegId covering `i`, which gem5's per-class scoreboard
   does not do across classes. Rejected unless we add alias-aware scoreboarding.
3. **Cap the dependency at the first M registers** for the largest buffers — a
   deliberate approximation that under-serializes; only acceptable if `buffer64`
   is rare and timing on it is not load-bearing. Document it if chosen.

Recommend (1).

### 4. Wire a stalling CPU

`LvxCPU.py` must bind **`MinorCPU`** (and/or **`O3CPU`**). `TimingSimpleCPU` will
faithfully *execute* the register lists but never stalls on them. Add:

```python
from m5.objects.BaseMinorCPU import BaseMinorCPU
class LvxMinorCPU(BaseMinorCPU, LvxCPU):
    mmu = LvxMMU()
```

plus a Minor functional-unit pool reflecting the LVX issue slots
(BCU0/1, ALU0/1, LSU0/1, EXT0..3). The bundle StaticInst's sub-insts must expose
per-slot op-classes for the FU pool to schedule against — a second generated
fact (`OpClass` per opcode) that the current `No_OpClass` bundle also lacks.

## Verification / build sequence

1. `Behavior.y` + regen `Behavior.pm`; add the `Proxy`-`Location` case to
   `LIB/Width.pm` (bound by N) and to `BE/LAO` CodeGen (lower to the storage
   form). Re-express the `BIA<N>` formats + `XACCESSO`/`XALIGNO`.
2. `make all && make -C BE/LAO check` — `BE/LAO/TEST` **lvx_v2** must stay
   0/1453 (value semantics identical by construction).
3. New `BE/GEM5` emitters: `operandRole` table + `bufferNReg` block expansion.
4. gem5: `LvxStaticInst` register lists; raise `MaxInstSrcRegs`; wire
   `LvxMinorCPU`; per-slot `OpClass`.
5. A directed timing test: a producer writing an `XVR` block, then `XACCESSO`
   off it — assert MinorCPU inserts the RAW stall (cycle count > the functional
   lower bound), and that an independent block does **not** stall.

## Scope note

Steps 1–2 are the MDS prototype the previous turn asked for and are verifiable
against `BE/LAO/TEST` today. Steps 3–5 are the gem5 target work; they also
require the port to grow register lists and a stalling CPU for the *first* time,
which benefits every instruction, with the buffer expansion (step 2/§2) being the
part that makes `XACCESSO`/`XALIGNO` correct rather than silently dependency-free.
