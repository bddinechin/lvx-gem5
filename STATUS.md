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

**FP divide and square root cost 13 cycles**, not 11, since 2026-10-19. The
functional units are latency buckets, one per pipeline write stage, and there
was none for FD — so the nine `FDIV*`/`FSQRT*` opcodes, which write there,
fell into SF's bucket and ran two cycles short. A latency with no exact bucket
is charged a lower one silently, the routing having no way to say that none
fits, so the per-destination reduction is now measured against the bucket the
FU will actually charge rather than against the bundle's own maximum: the two
can no longer disagree unnoticed. Measured on self-chained `fsqrtw`/`fdivw`:
11.01 → 13.01 cycles a step, with `faddw` unchanged at 4.01; PolyBench's
`correlation`, which has sqrt on a hot path, moves 279849 → 280664.

**MinorCPU could deadlock in a taken-branch bubble** until 2026-10-19, and the
shape is worth knowing because no micro test could reach it. Issue is held off
until the bubble's release cycle, but that stall was not itself a reason to
tick: when the branch had squashed the front end (so no input) and the
pipeline had drained (so nothing in flight), `need_to_tick` went false, the
CPU slept, and nothing existed to wake it. Simulated time stopped dead at a
fixed cycle count. It took an FP-heavy real kernel — PolyBench's `correlation`
— to coincide those three, which is a coverage gap as much as a bug: the
validation suite is micro tests and the differential harness runs `atomic`, so
nothing in CI runs a kernel of that size under `minor`.

**A bundle's results retire at their own latencies** since 2026-10-19, not all
at the bundle's maximum: a 1-cycle `copyd` sharing a bundle with a 3-cycle
`ld` is readable after 1. This is the mirror of the per-source read stages,
and it did *not* need the micro-op split this file long said it would — a
vector parallel to `destRegIdxArr`, and `markupInstDests` taking a
per-destination reduction, is the whole of it. Verified in the scoreboard: one
bundle issued at cycle 216 marks `$r0` ready at 217 and `$r6` at 219.

**Bundle lengths reach fetch** since 2026-10-19, which was a much larger error
than the one above. A bundle's length is known only at decode, and the PC
state's npc came from the *previous* bundle's, so every change of bundle
length read to Execute as a taken branch and squashed the pipeline — six times
the cycles on a loop that alternates lengths, and wrong in proportion to how
much real code's bundle lengths vary. `divmodvx` re-measures 6.7x faster on
its native route. Every hand-written timing fixture was uniform-length, which
is why nothing caught it; `timing_mixlen.s` exists now.

**The hardware-loop back edge is free**, on both sides, as of 2026-10-18. It
was already exempt from the taken-branch penalty (`freeRedirect`), but it
reached the *fetch* side only through Minor's branch predictor, which Fetch2
consults only for a bundle that is `isControl()` — so a loop whose last bundle
held a branch was free, and one whose last bundle held arithmetic paid a full
front-end refill every trip. `BaseISA::fetchRedirect` (new, false by default)
lets the ISA redirect the fetch stream itself, and Fetch2 follows it exactly as
it follows a correct prediction, so Execute later sees a correctly predicted
branch and does not squash. A seven-bundle body went from 10.17 cycles a trip
to 7.18, and costs the same now whether or not its last bundle holds a branch.

Residual: a one- or two-bundle body still floors at ~4.1 cycles a trip rather
than 1 or 2, which is Fetch2's redirect turnaround — the same floor a correctly
predicted branch has, so no longer something peculiar to hardware loops. Taking
it to 1 would need a loop buffer.

**The branch cost model** is complete as of 2026-10-18: a not-taken branch
costs nothing, a taken unconditional one `minTaken`, and a taken conditional
one `minTaken + 1` — the extra being `Opcode@timing`'s `%0: Transfer.<c>`,
now exported per opcode, which says `cb`/`ccb`/`igoto`/`icall` settle the next
PC a stage later than `goto`/`call`/`ret`. All three are measured exactly by
`tests/lvx/diff/check_timing.sh`.

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
599 lvx_2 opcodes — `lq`, `lo` and every packed SIMD result — had no
destination register and therefore no latency; an **uncached load** (`ld.u`,
`ld.us`) returns at SR, a latency of 15, not the L1's 3; and a **taken branch**
now costs `Processor@minTaken`, charged as a bubble on issue that a pending
result latency elapses against rather than adding to.

Still open, in order of size:

- **A mispredicted branch pays Minor's refill on top of the architectural
  penalty.** The predictor is kept on deliberately, and *not* as a speculation
  mechanism: it stands in for an LVX fetch engine that keeps up with a taken
  branch, and since the penalty is a hard release floor it can only ever make
  a branch cost *more* than the architecture says, never less. Steady-state it
  costs nothing measurable — the directed loops come out at exactly
  `bundles + minTaken(+1)`, with 0.17 cycles a trip of cold start over 1000
  trips. The residue is the first execution of each branch and each loop exit.

  Turning prediction off is not the improvement it sounds like, and the number
  says why: an *unpredicted* redirect costs **5.13 cycles** of front-end refill
  (measured on a hardware loop, whose back edge is never predicted), against
  the 2 or 3 the architecture charges. Minor's refill is structural —
  `executeBranchDelay` plus the three forward delays, each hard-floored at 1
  (`fatal: fetch1ToFetch2ForwardDelay must be >= 1`) — so it cannot be tuned
  below the architectural penalty, and disabling prediction would replace a
  cost LVX does not have with a bigger one it also does not have. Making the
  penalty genuinely independent of prediction needs the front end restructured,
  not a parameter.
- **`XVR` and the RISC-V `RV` register file are still untracked**, so an
  extension-unit or RV64G dependence stalls nothing.

### What the ISS suite now covers under the pipeline model

Three of this week's timing bugs were reachable only by code larger than the
suite, and two of them were introduced with twenty-odd timing assertions
passing over them. `tests/lvx/diff/check_kernel.sh` closes that with two
kernels, both `-O2` so bundle lengths vary and hardware loops appear: a
`stencil` with `fsqrtw`/`fdivw` inside the recurrence, and `seidel` —
PolyBench's seidel-2d shape in double, updated in place, a tight chain of
short-latency work with `fdivd`. Each asserts that it *completes* under
`minor`, computes a host build's answer, and that `atomic` and `minor` agree
on it. ~62k and ~70k bundles, a few seconds a core.

Worth recording: `seidel` is *not* the harder case by divergence from the
functional model — 2.25x against the stencil's 3.92x — which is the argument
for having both rather than against. A single ratio does not say which shapes
a model gets wrong.

That answer check is new coverage in its own right: `run_diff.sh` compares
results against the host but runs the atomic CPU, so until now nothing checked
that the pipeline model computes correctly at all.

Still thin, and in a specific way: two shapes, both stencils, both FP, both
tiny working sets so neither touches the memory system. Nothing here has a
pointer chase, an indirect branch, or a working set past the L1.

### Parallel bundle semantics are now tested, and one of them was wrong

A bundle is not a short sequence: its instructions read their sources at the
same cycle and write afterwards, so a reader beside a writer sees the value
being replaced and two instructions can exchange registers with no temporary.
`LvxStaticInst::execute` gets this structurally -- every sub-instruction's
fetch phase, then every execute phase, then every commit phase -- and nothing
tested it until `tests/lvx/diff/parallel.s` (the swap, a reader beside a
writer of the same register, a three-way rotate, and two `cmoved` writes to
one register under disjoint predicates in both orders). It is worth testing
now because it is exactly what the micro-op split would break: gem5 executes
micro-ops in sequence, so one `StaticInst` per syllable makes syllable k+1
observe k's writes, and the symptom is a wrong answer rather than a crash.

Writing the same test one level down, for memory, found a live bug.
`MEM_load` and `MEM_store` both run from the behaviour *execute* phase, and
execute() ran the non-BCU syllables in slot order -- so a bundle holding a
load and a store to the same address gave an answer that depended on the
assembler's steering: `sd ... ; ld ...' let the load see the stored value,
`ld ... ; sd ...' did not. execute() now runs every syllable that only reads
memory before every syllable that modifies it, reading the generated memory
classification (`lvx_reg_deps[].mem`); the atomics count as modifiers, since
their read and write are one indivisible access and no order can also put a
load before a second atomic's read. Pinned by `check_memorder.sh`, five cases,
each verified by mutation to report its own number.

No compiled code reaches it -- of 50,631 bundles across libc and the
validation binaries, none holds two memory accesses -- which is why it went
unnoticed, and is no reason for the ISS to disagree with the architecture.
Both are the invariants the micro-op split has to preserve, which is why they
are tests and not comments.

### The micro-op split landed, and the model was issuing two bundles a cycle

A bundle is now a gem5 macroop whose syllables are micro-ops, one per occupied
issue slot -- what a memory syllable needs in order to be an `isMemRef()` of
its own and go through the load/store queue, which is the remaining step. The
parallel semantics come from a snapshot: the head micro-op runs every
syllable's fetch phase into the thread's `BundleExec` and each micro-op
executes and commits out of that, so a register written by one syllable is
invisible to the next. Functional correctness turns out not to depend on the
timing model at all -- gem5 executes at commit and commits in order -- which
is why the model could be wrong while every answer stayed right. It was.

**A bundle used to be one instruction, so it occupied one functional unit, and
`executeIssueLimit` is 2.** Two consecutive bundles whose syllables landed in
*different* latency buckets issued in the same cycle; two in the same bucket
serialised on `MinorFU::alreadyPushed`. So the issue rate was a function of
functional-unit variety, which is not an LVX property -- one bundle issues per
cycle, always -- and every multi-bundle dependence chain was understated. Ten
unrolled `fmulw` into an ALU consumer measured 42 cycles a trip against the 55
the machine costs, 24% optimistic. **Any `LVX_CPU=minor` figure for code with
mixed-latency bundles, taken before 2026-10-19, is optimistic by up to that
much.** The uniform-latency fixtures were unaffected, which is why 24 timing
assertions passed over it.

Six of those assertions encoded the old behaviour; they now encode the
derivation instead, as exact cycles a trip, and the pre-split binary fails all
six. One bundle per cycle is enforced at *issue*
(`executeOneMacroopPerCycle`), not by narrowing the front end -- doing the
latter also stops the front end running ahead, so a redirect's turnaround
never closes and the hardware loop went from 7 cycles a trip to 10.

The split also surfaced a gap in Minor's own macroop support: `Decode` carried
`predictedTaken` onto the last micro-op but not `triedToPredict`, so a branch
predicted *not* taken reached Execute looking unpredicted -- which both
squashed the front end and told the predictor nothing, so the BTB never
learned and every taken branch paid a full refill. Invisible to an ISA whose
macroops do not contain branches.

Cost: the atomic CPU is unaffected (766k inst/s on the seidel kernel, against
770k on record). Under `minor` that kernel runs 1.72 micro-ops per bundle, so
Minor does that much more per-instruction work; it measures 73k inst/s, with
no like-for-like pre-split figure to compare.

See `docs/register-buffer-deps.md` for the micro-op order's three load-bearing
boundaries and what the LSQ step still needs.

### `gem5.fast` is available, and buys less than one might hope

`VARIANT=fast ./build-cores.sh` builds `gem5-lvx-<N>.fast` beside the `.opt`
binaries. Both are -O3; `.fast` adds NDEBUG and `TRACING_ON=0`, so it drops
the asserts and the whole DPRINTF machinery, and `.opt` adds `-g`.

Measured on the seidel kernel, best of five: **1.12x under `atomic`** (807k ->
906k inst/s) and **1.06x under `minor`** (77.0k -> 82.0k). Worth having, and
not the answer to ISS speed -- the cost is in the generated behaviour and the
pipeline's own data structures, both already optimised in `.opt`, not in trace
checks. The binary is 37 MB against 780 MB, which is the other reason to want
it.

Cycle-identical to `.opt`, which is the test: 277,253 cycles from both on the
kernel, and `run_checks.sh` passes 22/22 on `.fast` (it honours
`GEM5_LVX1`/`GEM5_LVX2`). Note `--debug-flags` is *rejected* there rather than
ignored -- "fatal: Tracing is not enabled" -- so `.opt` stays the one to
develop against. Every harness defaults to the `.opt` name, so `.fast` is
opt-in and is therefore the one that can go stale unnoticed; `build-cores.sh`
says so at the top.

### The hardware loop's prefetch buffer is modelled

`LOOPDO`'s own behaviour invalidates one -- `(EFFECT.ID.invalpfb)` in
`Instruction.yml` -- so the machine replays a hardware loop's body from a
prefetch buffer and the back edge costs no fetch. The ISS made the back edge
free in the *branch* sense (`freeRedirect`, no `minTaken`) but still re-fetched
the body every trip, and paid for that twice:

- a body of 2, 3 or 4 bundles all cost **4.09** cycles a trip, 5 or 6 cost
  **6.12**, and only from 7 up did a trip cost its bundles. The floor is the
  pipeline between Fetch2 deciding the redirect and Execute issuing the
  target -- three forward delays and the signal back.
- the *same* 4-bundle body cost **4.07** inside one 64-byte line and **6.09**
  across two.

Together they cost lvx-mlir's software-pipelined VEC=4 `mymma` its entire
gain: 6.03 cycles a trip against the 4 its scheduler had planned, so
pipelining that kernel looked worthless
(`../docs/handoff-iss-lvx-mlir-findings.md`, issues 1 and 4 -- they are one
bug, and the second is the cause of the first).

`Fetch2` now holds the body: captured on one trip, emitted from the buffer on
the rest with no redirect and no line fetch, so nothing drains and a trip
costs its bundles. The instruction ending each trip still claims a correct
prediction to the loop top, which is what stops Execute squashing when it
resolves the architectural back edge. Only a straight line between `LS` and
`LE` is taken -- each instruction must start where the last ended -- and a
stream change, a branch out or a body past 32 bundles drops it and the next
trip recaptures, falling back to the fetch-every-trip path.

Measured after: every body length from 2 to 8 costs its bundle count, and the
straddling body costs what the aligned one does (`check_hwloop_fetch.sh`).
`mymma-vec4-pipe` runs at **4.00 cycles a trip against its schedule's 4**, and
the auto-vectorised and hand-written VEC=8 kernels -- identical instruction
for instruction, 0.73 apart before because one straddled a line -- now agree
exactly.

One thing it cost: those two VEC=8 kernels now measure 19.82 cycles a trip
where the better-placed one measured 19.00, against a schedule of 19. Their
bodies are five bundles with 15-cycle `.u` loads, so fetch is nowhere near the
limit and the 0.8 is something else the buffer introduced on a long body. It
is not alignment any more -- the two agree -- and it is worth finding.

**One trap worth keeping.** The first attempt was functionally wrong, not just
slow: on the cycle the replay starts, Fetch2 carried on decoding the line
sequentially past `LE` and handed Execute instructions from *after* the loop,
which were then committed -- `run_diff`'s oracle caught it as a wrong answer.
The old redirect was saved from that by the `BranchData` it signalled; the
buffer signals none, because not redirecting the front end is the whole point.
The replay has to end the cycle's decoding itself.

### Loads and stores now go through the load/store queue

Which is what the split was for. A load or store syllable is an `isMemRef()`
of its own, so MinorCPU's queue carries the access and **the data cache sees
it** -- on the seidel kernel, 25,537 accesses and 413 misses where every
counter used to read zero whatever the program did. `check_dcache.sh` pins
that, as the difference between two runs of one fixture.

The access is split across two passes of the syllable's execute phase: one to
compute the address and hand the request to the queue, one to consume the
response. MDS emits one function per phase and there is no seam inside it, so
the phase runs twice -- sound only while a load or store body touches nothing
but its operand slots and the memory helper. BE/GEM5 now decides that rather
than the port assuming it: `lvx_reg_deps[].lsq` is set for an access whose
execute phase has no storage write, trap or branch hint **and** makes exactly
one access, since the queue is one per instruction. 75 of 75 accesses qualify
on lvx_1; 91 of 215 on lvx_2, the other 124 being the extension accesses
(`XLO`, `XSO`, `XPL*`), which also update `$cs.XMF` and the XVR file. Those
and the atomics keep the functional path -- correct, but invisible to the
cache.

LVX's byte masks map straight onto gem5's `byte_enable`, so a `MASKM`-prefixed
access goes out as one request with its gaps simply not enabled, rather than
as the several the functional path splits it into.

**The ISA still states the dependence latency, deliberately.**
`executeMemRefsAssumeFixedLatency` marks a memory reference's destinations at
its functional unit's latency -- `ld` 3 cycles, `ld.u` 15 -- instead of making
dependents wait for the response. The access can still take longer, since
in-order commit waits for the response too, so the hierarchy is a floor rather
than a replacement.

**A miss is not free, and an earlier version of this said so wrongly.** On the
seidel kernel the 413 misses carry 13,085 cycles of miss latency and move the
total by 18, which is what "nearly free" was generalised from -- but that
kernel has independent work to hide them behind. A loop whose next address
depends on the load it just made hides nothing: 2,000 such misses measured
53.7 cycles a trip for a five-bundle body, so about 48 cycles each, because
in-order commit waits for the response and everything behind commit waits for
that. Misses are hidden when there is work to hide them behind and exposed
when there is not, which is the behaviour one wants; what the fixed latency
buys is that *dependents* do not wait, not that misses are cheap.

### There are two levels now, and the second one costs what the ISA says

The description's two load latencies describe two levels: `ld` writes at CR, 3
cycles after RR, which is an L1 hit; `ld.u` carries `MemoryLevel=2` and writes
at SR, 15 cycles, which is the next level reached without touching L1. The
configuration had an L1 and then DRAM, so an L1 miss cost about 48 cycles
where the architecture has something at 15, and the ISS asserted the 15 out of
the description rather than producing it.

With a 256kB 16-way L2 (tag 11, data 11, response 3) **an L1 miss that hits it
costs 15 cycles**, against the description's SR − RR of 15, and a miss past it
costs 67. `check_hierarchy.sh` pins that -- the middle assertion being the one
that matters, since a configuration whose second level has drifted from SR − RR
is no longer describing this machine. The latencies are a modelling choice;
what is not a choice is that there are two levels and roughly what each costs,
which the description fixes.

**What is still not modelled is `.u` actually bypassing L1** (the handoff's
issue 2). It cannot be expressed as a gem5 request flag: `Request::UNCACHEABLE`
makes `Cache::access` invalidate and forward without allocating, but *every*
level does that, so the access lands in DRAM rather than at level 2. Doing it
properly wants a per-request no-allocate honoured by the L1 alone -- a small
gem5 change -- and until then a `.u` load still allocates in L1 and still hits
there, charged its architectural 15.

**And "let the hierarchy decide" is not one flag away.** Turning
`executeMemRefsAssumeFixedLatency` off adds exactly one cycle to a load, not a
cache round trip: `ld` goes from 4 to 5 and `ld.u` from 16 to 17. The reason is
that the access initiates at *commit*, which is already the functional unit's
latency into the instruction's life, so the memory time is serialised after
the ISA's rather than being it. For the hierarchy to decide, a load's
functional unit would have to be the address path (one cycle) with the access
issued then -- which is `executeAllowEarlyMemoryIssue`, and that is the one
thing `nonBlockingFuncUnits` cannot be combined with, because the early path
reaches for the instruction at its FU's end and a non-blocking unit has
released it. Load shadow or hierarchy-priced loads: today the model can have
either, and reworking the early path onto the `fuCompleted` stamp is what
would let it have both.

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

**25 helpers remain panic stubs on `lvx_2`** (the superset core), 22 on `lvx_1`
— measured 2026-10-08 from `lvx-mds`'s `lvx-refs/BE/GEM5/<core>/helper_stubs.inc`,
which is per core and independent of whichever core `generated/` currently holds.
(35 stub *lines* on lvx_2: a few helpers appear at more than one signature, so
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
- **Complex FP** — **all 3 remain**, but the `lvx_2`-only packed f16<->i16
  conversions noted with them are ***implemented***: `fmulc_32_32`/`ffmac_32_32`/`fconj_32_32` (FMULWC/FFMAWC,
  both cores), plus the **`lvx_2`-only packed f16↔i16 conversions** (4):
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
