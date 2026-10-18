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
> as a Read. **Verified value-preserving** by the `BE/GEM5/TEST` differential
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
> `branch_info`/`MEM_*`/… from `int256_t` to native types) and adding the missing
> `CS` storage helpers.
>
> **`LvxMinorCPU` wired.** `LvxCPU.py` adds `LvxMinorCPU(BaseMinorCPU, LvxCPU)`;
> the bundle now carries `IntAluOp` (not `No_OpClass`) so a MinorCPU FU accepts
> it — a non-memory class deliberately, since the shim serves loads/stores
> atomically inside `execute()` and MinorCPU must not expect an LSQ request.
> `run_lvx.py` selects the CPU via `LVX_CPU` (`atomic` default → the harness stays
> unchanged and fast; `timing`; `minor`) and adds L1 caches on the timing path so
> the cycle count reflects the pipeline, not uncached fetch latency. Verified: a
> loop runs functionally identically under all three (result 285, code 29) while
> Minor reports **317 cycles vs Atomic's 27** — the reg lists driving
> dependency/FU-latency stalls.
>
> **FU latencies calibrated from the MDS pipeline stages.** Resource hazards need
> no modelling (the assembler's bundling guarantees a valid bundle has none), so
> only dependence latency matters, and it is `writeStage − readStage` from the
> Behavior stage annotations (`operand{From,To}RegFile`'s stage argument, against
> the Processor `pipeline`). `reg-operands.pl` folds `max(write stage) − RR` into
> `lvx_reg_deps[op].lat` — all cross-checked by mnemonic, e.g. the `SR` writers
> are exactly the `ACSWAP*`/`ASWAP*`/`AL*` atomics, not a typo. The stages
> themselves are a generated **named enum** — BE/GEM5
> emits `lvx_stages.h` (C `enum LvxStage`) + a Python twin from `Processor.table`,
> and `static_inst.cc`/`LvxCPU.py` use `LVX_STAGE_* - LVX_STAGE_RR`, never a
> literal. `LvxStaticInst` routes each bundle (max latency over its syllables) to
> a latency-bucket FU in `LvxFUPool` whose `opLat` is that latency. Verified in a
> cache-resident loop: a lat-2 (`maddd`) dependent chain costs ~2× the per-op
> latency of a lat-1 (`addd`) chain (20.3 vs 12.4 cyc/iter over a 10-op body).
>
> (The absolute stage *numbers* in the paragraph above have since moved —
> `SF=16, SR=24` was the pipeline of 2026-07; it is `SF=12, FD=14, SR=16` now,
> so the latencies are ALU 1, cached load 3, integer divide 11, FP divide/sqrt
> 13, atomic RMW 15. Which is the whole argument for the generated enum: the
> code did not have to change, only this prose, which is not generated.)
>
> **Three corrections since, all 2026-10-16** (see
> `../../docs/handoff-mds-load-variants.md`):
>
> - **The GPR tuples are tracked.** `reg-operands.pl` modelled only `GPR` and
>   `SFR`, so an operand in `PGR` (an aligned pair) or `QGR` (an aligned quad)
>   was invisible — and that is where every 128- and 256-bit value lives, there
>   being no wide register file. 599 lvx_v2 opcodes had **no destination at
>   all** and so a latency of 0: `lq`, `lo`, and every packed SIMD result. They
>   now expand into their member GPRs, which is the granule the scoreboard
>   tracks and what a later scalar read of half a vector depends on. The port's
>   per-bundle list bounds are now the architectural worst case (`MaxInstSrcRegs
>   × MaxBundleIssue`), checked against the generated table by a
>   `_Static_assert` in `behavior.c`, and overflow panics rather than dropping a
>   dependency — a dropped dependency being a cycle count quietly too low.
> - **An uncached load's latency is 15, not 3.** The write stage of the loaded
>   operand is a property of the `variant` *modifier*, not of the Opcode: `lq`
>   and `lq.u` are one record with one Behavior. `lvx_reg_deps` now carries a
>   latency per encoded modifier value (`varLat`/`varSlot`) and `setUpRegs`
>   indexes it with the decoded field.
> - **A taken branch costs `Processor@minTaken`.** `LvxMinorCPU` sets
>   `takenBranchBubble`, a new (ISA-agnostic, default-0) MinorCPU parameter that
>   holds issue until `branch issue cycle + 1 + minTaken`. A *release cycle*
>   anchored at **issue**, and both halves of that matter. A release cycle
>   rather than a countdown means it combines with the scoreboard as a max, so
>   a pending result latency elapses during the stall instead of being added to
>   it. Anchored at issue rather than at commit — where the branch is actually
>   detected to have been taken — because in-order commit can hold a branch
>   many cycles behind a long-latency instruction ahead of it, and a
>   commit-anchored bubble then lands *after* the latency it was supposed to
>   disappear into: `ld.u; goto; use` measured 16.6 cycles a copy at a penalty
>   of 0 and 18.6 at 2, where the architecture says the branch is free. The
>   issue cycle is now recorded on `MinorDynInst` (`issueCycle`). Minor's own
>   taken-branch cost is its front-end refill, which is deeper than LVX's and
>   nearly free once the branch predictor has learned a loop — 0.2 cycles per
>   taken branch, measured. All of it is pinned by
>   `tests/lvx/diff/check_timing.sh`.
>
> **Fixed 2026-10-17: a long latency can be scheduled around.**
> `nonBlockingFuncUnits` (new, default off, set by `LvxMinorCPU`) is the third
> option below, and it measures exactly right: the sweep plateaus to N = 14 and
> then takes slope 1, and an instruction merely *following* an uncached load
> went from 8.4 cycles to 2.3. `minTaken`, the hardware-loop back edge and
> every dependent case are unchanged to within a cycle, which is what separates
> it from the uniform-depth option. What follows is the diagnosis, kept because
> two of the three candidate fixes do not work and the reasons are worth having.
>
> **The defect was that MinorCPU drains its functional units in commit
> order.** An instruction that
> reaches the end of a short FU while a long-latency one is still ahead of it
> in the in-order commit queue cannot retire, so it stalls its own FU and
> blocks every later instruction of that class. The consequence is that there
> is **no load shadow**: independent work placed between an `ld.u` and its
> first use does not overlap the load at all, and a copy of
> `ld.u; N fillers; use` costs `15 + N + 1` where the architecture says
> `max(N + 1, 15) + 1`. Measured across N = 0..19, the curve has slope 1
> throughout instead of a plateau to N = 14. So the 15-cycle latency is now
> charged, but software pipelining and prefetch distance get no credit for
> hiding it — the ISS is pessimistic exactly where a scheduler is trying to
> win. See *Implementing a load shadow* below; note it is **not** what the
> micro-op split fixes, the blocking being independent of granularity.
>
> **Also wrong: the hardware-loop back
> edge.** Being the fetch engine's, it is exempt from `takenBranchBubble` —
> though not for the reason first given here. "The bundle holds no branch" was
> the test, and it is the wrong one: the bundle LE falls off can perfectly well
> hold a conditional branch that was *not* taken, and then the ISS charged the
> penalty anyway, two cycles a trip the hardware does not spend. Since
> 2026-10-17 the PC state carries the fact instead (`PCStateBase::freeRedirect`,
> set by `LvxStaticInst::execute` only when the loop engine took the edge), and
> `tests/lvx/diff/check_timing.sh` pins it with a loop whose last bundle is
> exactly that untaken `cb`. What remains wrong is that the back edge is
> invisible to Minor's branch predictor, which means every one is an
> *unpredicted* redirect and pays the full front-end refill. A one-bundle `LOOPDO` body
> measures **6.1 cycles a trip** where the architecture says 1, so the ISS makes
> a hardware loop look dearer than the rotated branch loop it exists to replace
> (4.1 cycles a trip for a three-bundle body). Fixing it needs a way to tell
> Minor that a redirect is free, which is a dynamic fact — the loop end is
> `LE`, a run-time register — and so not expressible through `StaticInst`
> flags. Untouched by the above; `LVX_TAKEN_BUBBLE=0` reproduces it exactly.
>
> **Read stage is per-source — implemented 2026-10-17.** The true latency is
> `writeStage(producer) - readStage(that consumer's source)`. Most sources are
> read at RR, but a **store's stored value**, an **atomic's**, and the
> **integer MAC's accumulator** are read at `E1`, so the edges into those are
> one cycle shorter — which lvx-gcc states as a `define_bypass` per producer
> class and the ISS used to ignore, over-stalling each by exactly one.
> `reg-operands.pl` already parsed the read stage out of
> `operandFromRegFile_*(this, STAGE, …)` and threw it away for reads; it now
> emits it as `relLat = readStage - RR` per source operand (323 lvx_v2 opcodes
> have one), `setUpRegs` folds it into a per-source vector — taking the
> **minimum** over the sub-instructions that read a register, the earliest
> reader being the binding one — and `StaticInst::srcRegsRelativeLats()` (new,
> null by default) hands it to `Scoreboard::canInstIssue`, which already knew
> how to apply such a relaxation. Measured: an `fmulw` feeding a store is 3
> cycles away against 4 for an ordinary consumer, and a self-accumulating
> `maddw` chain costs 1 cycle a step against the 2 of a `mulw` chain.
>
> This paragraph used to say the fix needed the **micro-op split**, on the
> grounds that `srcRegsRelativeLats` was reachable only per-FU through
> `MinorFUTiming`'s mask/match on raw instruction bits. That was too
> pessimistic twice over: the vector can ride on the instruction instead, and
> a bundle-wide source list is not an obstacle once the minimum is taken. What
> genuinely still needs micro-ops is the **destination** side — a bundle
> holding both an `ffmaw` and an `addd` marks both results ready at the
> bundle's maximum latency.
>
> ## Implementing a load shadow
>
> The chain is exact, and none of it is about granularity. `FUPipeline` is a
> `SelfStallingPipeline` whose **depth is `opLat`** (`func_unit.cc`), and
> `advance()` sets `stalled = isPopable()` — an FU stalls the moment an
> instruction reaches its output end and `commit` has not taken it. `commit`
> takes instructions from `inFlightInsts` **in order**, and its readiness test
> is literally "is the head of `inFlightInsts` sitting at the end of its FU"
> (`funcUnits[inst->fuIndex]->front().inst->id == inst->id`). So: a 1-cycle
> `addd` issued behind a 15-cycle `ld.u` reaches the end of the 1-deep IntAlu
> FU in one cycle, cannot be committed because the load is still ahead of it,
> stalls that FU, and `issue` then refuses every later IntAlu instruction
> (`else if (fu->stalled)`). One short instruction is enough to serialise the
> whole lane. **A micro-op split changes none of this** — the blocking is
> per-FU and per-commit-order, not per-bundle.
>
> What LVX actually is: in-order *issue*, interlocked, with **out-of-order
> writeback** (each result lands at its own write stage, E1…SR). Minor models
> in-order writeback, which is a different machine.
>
> Three things were tried. The first is a trap worth recording:
>
> - **Keep `opLat = 1` and carry the real latency in
>   `MinorFUTiming::extraAssumedLat`** — which `issue` adds to the scoreboard
>   retire time (`curCycle + opLat + extraCommitLat + extraAssumedLat`), and
>   which needs no mask/match hackery because the bucket FU has already
>   selected the instruction (`mask = 0` matches everything). Config-only, and
>   **wrong**: the scoreboard entry is cleared at *commit*
>   (`clearInstDests`), so with commit one cycle after issue the dependence is
>   torn down at cycle 1 and the 15 is never enforced. Measured: the
>   dependent-load loop fell from 16.3 to 2.4 cycles a copy. Minor's invariant
>   is "commit happens when the result is ready", and `opLat` cannot be made to
>   lie about it.
>
> - **Uniform FU depth** — add a `MinorFU.pipelineDepth` separate from `opLat`
>   (the dependence latency stays `opLat`, the occupancy becomes the depth) and
>   give every LVX FU depth 15. Because the depths are then equal and issue is
>   in order, instructions reach their FU ends *in issue order*, which is the
>   order commit wants them in, so no FU ever stalls. ~15 lines of gem5 plus
>   one config line, and it works exactly: the sweep plateaus at 16.3 cycles a
>   copy to N = 14 and then takes slope 1, against an ideal of
>   `max(N + 1, 15) + 1`. **But it breaks the branch model.** Branch resolution
>   moves to the end of a 15-deep pipeline, so (a) the `minTaken` bubble, which
>   is anchored at the branch's issue cycle, is always already in the past by
>   the time commit discovers the branch was taken — the ALU loops fell back to
>   their penalty-free numbers (6160 → 4169) — and (b) every *unpredicted*
>   redirect pays a 15-cycle refill, which takes the hardware-loop back edge
>   from 6.1 to 20.2 cycles a trip. Not a net win on its own.
>
> - **Non-blocking functional units** — the fix taken. Let an
>   instruction leave its FU after its own `opLat`, stamping a completion cycle
>   on the `MinorDynInst`, and change commit's readiness test from "at the end
>   of its FU" to "completed, and not later than now". Commit stays in order,
>   so sequential semantics are untouched; the scoreboard entry lives until
>   commit, which is no earlier than `returnCycle`, so lingering entries are
>   harmless (a consumer tests the cycle, not the entry's existence). A branch
>   still completes at issue + 1 and resolves there, so `minTaken` and the
>   hardware-loop cost are unchanged. Touch points: `FUPipeline::advance` (stamp
>   and do not stall), `Execute::commit`'s FU-front test and its matching
>   `stalled = false`, `Execute::evaluate`'s `head_inst_might_commit` and
>   `can_issue_next`, and `issue`'s `fu->stalled` arm. Two caveats: the
>   LSQ/mem-ref path assumes the instruction is at its FU front, so gate the
>   mode on a parameter and `fatal()` on a memref under it (LVX issues none —
>   the shim serves memory inside `execute()`); and `inFlightInsts` is sized
>   `sum(opLat)` = 36, which the backlog behind one 15-cycle load fits, but a
>   generic version should size it deliberately and have `issue` check it.
>
> The penalty is per branch as of 2026-10-18: `minTaken` for one that settles
> the next PC in ID (`goto`, `call`, `ret`) and a cycle more for one that waits
> on a comparison or a register and settles it in RR (`cb`, `ccb`, `igoto`,
> `icall`), the difference coming from `Opcode@timing`'s `%0: Transfer.<c>`
> through the PC state, like `freeRedirect` and for the same reason — which of
> a bundle's two BCU slots actually transferred is a run-time fact.
>
> One hole survives either fix: a branch sitting *behind* a long-latency
> operation commits late, so the bubble's release cycle has passed and the
> penalty goes uncharged. The fix is to charge it at **issue**, keyed on
> `inst->predictedTaken` — which is known then, is correct whenever the
> predictor is, and is the same stand-in for LVX's fetch engine that the
> predictor already is — leaving the commit-time path as the correction for the
> unpredicted case.
>
> **The micro-op split (scoped next step; orthogonal to the above).** Each sub-instruction becomes a
> MinorCPU micro-op with its own opClass/opLat, source list, and per-source
> `srcRegsRelativeLats`. It also fixes the per-*result* limitation (a load and an
> ALU result in one bundle share the bundle's max latency today). The hard part
> is preserving **VLIW parallel semantics**: real `-O2` bundles pack e.g.
> `make $r0=1 ; sw ...=$r0`, where the store reads the *old* `$r0` — so micro-ops
> need a **bundle-entry register snapshot** for reads and **intra-bundle
> dependency suppression** in the scoreboard (a parallel swap `copy $r0=$r1 ;
> copy $r1=$r0` is unrepresentable sequentially, and neither Minor nor O3 handles
> VLIW read/write phasing natively). This is why the port is bundle-at-a-time.
>
> **Other follow-ups:** the lvx_v2 `bufferNReg`→N-XVR source expansion (raise
> `MaxBundleSrcRegs` for buffer64Reg); routing loads/stores through the timing
> memory path; implicit `scall` arg reads; and the FP/atomic helper stubs (those
> long-latency ops can't run functionally yet, so only the calibration, not full
> execution, is exercised for them).

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
new rule (the tree round-trips `Opcode.table`, which `BE/GEM5` re-reads).
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
is isolated) — and diff the regenerated `lvx_v2 behavior_bodies.inc`:

- **24 changed lines, all `(AGGL.XVR` → `(AGGL.XVR.%2`**, on exactly the 12
  block-read nodes (2 reads × 6 buffer sizes). **Zero non-`.%2` changes.**
- All 12 sit **inside `/* */` S-expr comments** (in-comment 12, in-code 0): the
  emitted C is byte-identical, so the `BE/GEM5/TEST` lvx_v2 trace is unchanged by
  construction — no need to run the harness to know it stays 0/1453.
- **lvx_v1 `behavior_bodies.inc` byte-identical** — the grammar addition is inert on
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

**Verified value-preserving** by the `BE/GEM5/TEST` differential harness against
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
   `LIB/Width.pm` (bound by N) and to `BE/GEM5` CodeGen (lower to the storage
   form). Re-express the `BIA<N>` formats + `XACCESSO`/`XALIGNO`.
2. `make all && make -C BE/GEM5 check` — `BE/GEM5/TEST` **lvx_v2** must stay
   0/1453 (value semantics identical by construction).
3. New `BE/GEM5` emitters: `operandRole` table + `bufferNReg` block expansion.
4. gem5: `LvxStaticInst` register lists; raise `MaxInstSrcRegs`; wire
   `LvxMinorCPU`; per-slot `OpClass`.
5. A directed timing test: a producer writing an `XVR` block, then `XACCESSO`
   off it — assert MinorCPU inserts the RAW stall (cycle count > the functional
   lower bound), and that an independent block does **not** stall.

## Scope note

Steps 1–2 are the MDS prototype the previous turn asked for and are verifiable
against `BE/GEM5/TEST` today. Steps 3–5 are the gem5 target work; they also
require the port to grow register lists and a stalling CPU for the *first* time,
which benefits every instruction, with the buffer expansion (step 2/§2) being the
part that makes `XACCESSO`/`XALIGNO` correct rather than silently dependency-free.
