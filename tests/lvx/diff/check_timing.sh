#!/usr/bin/env bash
#
# Timing check for the MinorCPU model: the taken-branch penalty and the load
# variants' latency, both read from the ISA description rather than written
# down here.
#
# Two numbers in Processor.yml are what the ISS has to charge, and both were
# being ignored until 2026-10-16:
#
#   minTaken   the cycles a taken branch costs beyond its own bundle before the
#              target's first bundle issues.  MinorCPU's own taken-branch cost
#              is its front-end refill, which models a machine LVX is not --
#              deeper, and nearly free once the branch predictor has learned a
#              loop.  Measured at 0.2 cycles per taken branch, i.e. nothing.
#   SR         where an uncached load's result is written ("stream return"),
#              against CR for an L1 hit.  `ld' and `ld.u' are one Opcode with
#              one Behavior, so the per-variant write stage lives on the
#              modifier (Modifier@variant's LoadedAt) and reaches the ISS
#              through Opcode@timing; without it every load cost an L1 hit.
#
# Twenty-three assertions, each failing alone if its piece regresses:
#
#   1. a taken *conditional* branch costs minTaken + 1, and a taken
#      *unconditional* one minTaken -- the difference being when each settles
#      the next PC (Opcode@timing's `%0: Transfer.<c>': `cb' waits for its
#      comparison and transfers in RR, `goto' knows its target in ID).  Each
#      is the difference between running the same binary with the bubble at 0
#      and at minTaken, so the prologue and the cache behaviour cancel.
#   1a. a *not-taken* branch costs nothing but its own bundle.
#   2. the loops these are measured in cost one cycle per bundle with the
#      bubble at 0, which is what makes (1) the whole of a branch's cost
#      rather than part of it.
#   3. a load whose first use is the very next bundle stalls for its whole
#      latency: 1 + (CR - RR) cycles a copy cached, 1 + (SR - RR) uncached.
#      Both figures, not just their difference, so neither a load that is
#      uniformly slow nor one that is uniformly fast can pass.
#   4. put a *taken branch* between that load and its first use and the copy
#      costs the same: the branch issues while the load is still in flight, so
#      its stall cycles elapse against the load's latency rather than adding to
#      it ("the latency dangles"), and the load looks (SR - RR) - minTaken
#      cycles long from the far side of the branch.  Asserted as equality of
#      the same binary at a penalty of 0 and of minTaken, which is exact.
#      This is the assertion that fails if the penalty is ever anchored at the
#      branch's commit instead of its issue -- in-order commit holds the
#      branch behind the load, so a commit-anchored bubble lands *after* the
#      latency it was supposed to disappear into and costs the full minTaken.
#   5. and the branch-free loop whose recurrence is a load is bubble-invariant
#      too, for the same reason.
#   6. the load's *shadow* is fillable: eight independent bundles between the
#      load and its first use cost less than one cycle a copy between them,
#      the load's latency covering all eight.  Stated as a bound rather than
#      an equality because this fixture's loop body is ten bundles a copy and
#      its own overhead is a few tenths; the thing it excludes is not subtle,
#      being eight cycles a copy.  MinorCPU's default is in-order writeback --
#      an FU self-stalls the moment an instruction reaches its end that
#      in-order commit is not ready for -- so without nonBlockingFuncUnits
#      one short instruction behind a long one serialises its whole lane and
#      the eight fillers cost the full eight.
#   7. a hardware loop's back edge costs nothing -- no architectural penalty
#      (the loop engine takes that edge, not a branch) and no front-end
#      refill either, the fetch side being told about it through
#      BaseISA::fetchRedirect.  Asserted both ways round: the loop costs its
#      bundle count, and it costs the *same* whether or not the bundle LE
#      falls off happens to hold a branch, which is what the refill used to
#      depend on.
#   7a. a loop whose bundles alternate in length costs its recurrence and no
#      more.  A bundle's length is known only at decode, so the PC state's
#      npc comes from the previous bundle's, and when that went uncorrected
#      every change of length read as a taken branch and squashed the
#      pipeline -- six times the cycles.  Every other fixture here is
#      uniform-length, which is why this one has to exist.
#   8. an operand read later than RR shortens the dependence into it: a
#      store's stored value and the integer MAC's accumulator are read at E1,
#      so a producer feeding one is a cycle closer than its latency says.
#      Each is measured against its own control -- the same shape reading at
#      RR -- so neither needs a per-opcode latency written down here, and the
#      two controls are themselves checked against the stage enum.
#   9. a *wide* load's first use is one register of the tuple it wrote, and
#      stalls for the same whole latency -- both halves of the pair `lq'
#      writes and all four lanes of the quad `lo' writes.  A 128- or 256-bit
#      value lives in an aligned GPR tuple, so the dependency only exists if
#      the port expands the tuple operand into its member GPRs; one lane
#      missing from the expansion shows up as a sixth of the cost gone.
#
# Env: LVX_TOOLCHAIN_BIN  GEM5  LVX_STAGES (the generated lvx_stages.py)
#
set -u
here="$(cd "$(dirname "$0")" && pwd)"
gem5root="$(cd "$here/../../.." && pwd)"

BIN="${LVX_TOOLCHAIN_BIN:-$gem5root/../lvx-toolchain/bin}"
GEM5="${GEM5:-$gem5root/build/LVX/gem5.opt}"
RUNCFG="$gem5root/tests/lvx/run_lvx.py"
STAGES="${LVX_STAGES:-$gem5root/src/arch/lvx/generated/lvx_stages.py}"
export LVX_CPU=minor

for f in "$BIN/lvx-mbr-as" "$BIN/lvx-mbr-ld" "$GEM5" "$RUNCFG" "$STAGES"; do
    [ -e "$f" ] || { echo "missing: $f"; exit 2; }
done

# The description's own numbers.  Reading them here rather than hardcoding is
# the point: raise minTaken in Processor.yml and this test follows.
val() { grep -E "^$1 = " "$STAGES" | head -1 | sed 's/.*= //'; }
MIN_TAKEN="$(val LVX_MIN_TAKEN)"
STAGE_RR="$(val LVX_STAGE_RR)"
STAGE_CR="$(val LVX_STAGE_CR)"
STAGE_E2="$(val LVX_STAGE_E2)"
STAGE_E4="$(val LVX_STAGE_E4)"
STAGE_SR="$(val LVX_STAGE_SR)"
[ -n "$MIN_TAKEN" ] && [ -n "$STAGE_SR" ] || { echo "cannot read $STAGES"; exit 2; }
UNCACHED_EXTRA=$(( STAGE_SR - STAGE_CR ))     # 15 - 3: what the variant costs

TRIPS=1000      # must match .equ TRIPS in timing_alu.s
ALU_BUNDLES=8   # bundles per trip of timing_alu.s
COPIES=1000     # TRIPS * UNROLL in timing_ld.s: load/use pairs executed
TUPLES=3000     # TRIPS * UNROLL * 6 in timing_tuple.s: tuple-lane load/use pairs
HWTRIPS=1000    # .equ TRIPS in timing_hwloop.s
HWBUNDLES=7     # bundles in its loop body
STEPS=20000     # TRIPS * UNROLL in timing_bypass.s

work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

build() {   # build <name> <out> [as-flags...]
    local src="$1" out="$2"; shift 2
    "$BIN/lvx-mbr-as" "$@" "$here/$src.s" -o "$work/$out.o" || return 2
    "$BIN/lvx-mbr-ld" -e _start -Ttext=0x10000 -nostdlib \
        "$work/$out.o" -o "$work/$out.elf" || return 2
}

cycles() {  # cycles <out> <bubble>  -- the MinorCPU cycle count, or empty
    local out="$1" bubble="$2"
    LVX_TAKEN_BUBBLE="$bubble" timeout 600 "$GEM5" \
        --outdir="$work/m5-$out-$bubble" "$RUNCFG" "$work/$out.elf" 2>&1 \
      | sed -n 's/.*(\([0-9]\+\) cycles).*/\1/p' | head -1
}

rc=0
report() { # report <name> <got> <want>
    if [ "$2" = "$3" ]; then
        printf '%-46s PASS (%s)\n' "$1" "$2"
    else
        printf '%-46s FAIL (got %s, want %s)\n' "$1" "${2:-<none>}" "$3"; rc=1
    fi
}

# Cycles per trip, rounded.  Every figure here carries one TRIPS-independent
# overhead -- the prologue, and the cold instruction fetch of the loop body on
# its first pass, about two tenths of a cycle per trip at TRIPS=1000 -- so the
# per-trip figures are compared to the nearest cycle rather than exactly.  The
# quantities under test are whole cycles and several of them, so rounding
# cannot absorb a regression: getting minTaken or a load's write stage wrong is
# a change of one or of twelve, not of a fifth.
per_trip() { echo $(( ($1 + TRIPS / 2) / TRIPS )); }
per_copy() { echo $(( ($1 + COPIES / 2) / COPIES )); }

# Hundredths of a cycle per copy, for the one assertion stated as a bound.
hundredths_per_copy() { echo $(( ($1 * 100 + COPIES / 2) / COPIES )); }
report_under() { # report_under <name> <hundredths> <limit in hundredths>
    if [ "$2" -lt "$3" ]; then
        printf '%-46s PASS (%s.%02d/copy)\n' "$1" $(( $2 / 100 )) $(( $2 % 100 ))
    else
        printf '%-46s FAIL (%s.%02d/copy, want under %s.%02d)\n' "$1" \
            $(( $2 / 100 )) $(( $2 % 100 )) $(( $3 / 100 )) $(( $3 % 100 ))
        rc=1
    fi
}

build timing_alu alu           || { echo "timing_alu build failed";  exit 2; }
build timing_alu unc --defsym UNCOND=1 \
                               || { echo "timing_alu UNCOND build failed"; exit 2; }
build timing_alu ant --defsym NOTTAKEN=1 \
                               || { echo "timing_alu NOTTAKEN build failed"; exit 2; }
build timing_ld  ldc           || { echo "timing_ld build failed";   exit 2; }
build timing_ld  ldu --defsym UNCACHED=1 \
                               || { echo "timing_ld.u build failed"; exit 2; }
build timing_ld  ldb --defsym UNCACHED=1 --defsym BRANCH=1 \
                               || { echo "timing_ld.u+br build failed"; exit 2; }
build timing_ld  ldf --defsym UNCACHED=1 --defsym FILLERS=1 \
                               || { echo "timing_ld.u+fillers build failed"; exit 2; }
build timing_hwloop hwl        || { echo "timing_hwloop build failed"; exit 2; }
build timing_hwloop hwn --defsym NOBCU=1 \
                               || { echo "timing_hwloop NOBCU build failed"; exit 2; }
build timing_mixlen mix        || { echo "timing_mixlen build failed"; exit 2; }
build timing_bypass bpmul      || { echo "timing_bypass build failed"; exit 2; }
build timing_bypass bpmac --defsym MAC=1 \
                               || { echo "timing_bypass MAC build failed"; exit 2; }
build timing_bypass bpalu --defsym FMUL=1 \
                               || { echo "timing_bypass FMUL build failed"; exit 2; }
build timing_bypass bpst  --defsym FMUL=1 --defsym STORE=1 \
                               || { echo "timing_bypass STORE build failed"; exit 2; }
build timing_tuple tpc         || { echo "timing_tuple build failed"; exit 2; }
build timing_tuple tpu --defsym UNCACHED=1 \
                               || { echo "timing_tuple.u build failed"; exit 2; }

alu0="$(cycles alu 0)";  alu="$(cycles alu "$MIN_TAKEN")"
unc0="$(cycles unc 0)";  unc="$(cycles unc "$MIN_TAKEN")"
ant0="$(cycles ant 0)";  ant="$(cycles ant "$MIN_TAKEN")"
ldc0="$(cycles ldc 0)";  ldc="$(cycles ldc "$MIN_TAKEN")"
ldu0="$(cycles ldu 0)";  ldu="$(cycles ldu "$MIN_TAKEN")"
ldb0="$(cycles ldb 0)";  ldb="$(cycles ldb "$MIN_TAKEN")"
ldf="$(cycles ldf "$MIN_TAKEN")"
hwl0="$(cycles hwl 0)";  hwl="$(cycles hwl "$MIN_TAKEN")"
hwn="$(cycles hwn "$MIN_TAKEN")"
mix="$(cycles mix "$MIN_TAKEN")"
bpmul="$(cycles bpmul "$MIN_TAKEN")"; bpmac="$(cycles bpmac "$MIN_TAKEN")"
bpalu="$(cycles bpalu "$MIN_TAKEN")"; bpst="$(cycles bpst  "$MIN_TAKEN")"
tpc="$(cycles tpc "$MIN_TAKEN")"
tpu="$(cycles tpu "$MIN_TAKEN")"
for v in "$alu0" "$alu" "$unc0" "$unc" "$ant0" "$ant" \
         "$ldc0" "$ldc" "$ldu0" "$ldu" "$ldb0" "$ldb" \
         "$ldf" "$hwl0" "$hwl" "$hwn" "$mix" "$bpmul" "$bpmac" "$bpalu" "$bpst" \
         "$tpc" "$tpu"; do
    [ -n "$v" ] || { echo "a run produced no cycle count"; exit 2; }
done

# 1. what each kind of taken branch costs, once per trip.
report "taken conditional branch costs minTaken+1" \
       "$(per_trip $(( alu - alu0 )))" "$(( MIN_TAKEN + 1 ))"
report "taken unconditional branch costs minTaken" \
       "$(per_trip $(( unc - unc0 )))" "$MIN_TAKEN"

# 1a. and a branch that is not taken costs only the bundle it sits in: the
#     NOTTAKEN loop is the plain one plus one never-taken cb.
report "a not-taken branch costs only its bundle" \
       "$(( $(per_trip "$ant") - $(per_trip "$alu") ))" 1

# 2. and the loop it was measured in really does issue a bundle a cycle, so
#    that difference is the whole penalty and not part of it.
report "stall-free loop is one bundle per cycle" \
       "$(per_trip "$alu0")" "$ALU_BUNDLES"
report "the unconditional loop likewise, one bundle more" \
       "$(per_trip "$unc0")" "$(( ALU_BUNDLES + 1 ))"

# 3. the load stalls for its whole latency when its first use is the next
#    bundle -- the cached and the uncached figure, and so their difference.
report "ld: first use next bundle costs 1+(CR-RR)" \
       "$(per_copy "$ldc")" "$(( 1 + STAGE_CR - STAGE_RR ))"
report "ld.u: first use next bundle costs 1+(SR-RR)" \
       "$(per_copy "$ldu")" "$(( 1 + STAGE_SR - STAGE_RR ))"
report "ld.u costs SR-CR more per copy than ld" \
       "$(per_copy $(( ldu - ldc )))" "$UNCACHED_EXTRA"

# 4. a taken branch between the load and its first use costs nothing: its
#    stall elapses against the load's latency.  Same binary, two penalties.
report "taken branch before first use is free" "$ldb" "$ldb0"

# 5. and so is the latch branch of a loop whose recurrence is a load.  The
#    uncached loop absorbs it whole -- 16 cycles of recurrence against a
#    3-cycle penalty.  The cached one has only ~4.25 cycles of recurrence to
#    hide 3 in, so it absorbs nearly but not quite all of it, and the
#    assertion there is a bound: well under a cycle a trip, where no
#    absorption at all would be three.  (It absorbed exactly all of it until
#    2026-10-18, when a taken conditional branch went from costing minTaken
#    to minTaken + 1.)
hundredths_per_trip() { echo $(( ($1 * 100 + TRIPS / 2) / TRIPS )); }
report_under "branch stall mostly dangles against ld" \
       "$(hundredths_per_trip $(( ldc - ldc0 )))" 100
report "branch stall dangles against ld.u" "$ldu" "$ldu0"

# 6. the shadow: eight independent bundles between the load and its use cost
#    nothing, the load's latency covering them.
report_under "8 bundles fit in ld.u's shadow" \
       "$(hundredths_per_copy $(( ldf - ldu )))" 100

# 7. the loop engine's back edge is free, and the loop it is measured in
#    really does issue a bundle a cycle, so a penalty would have shown whole.
report "hw-loop body is one bundle per cycle" \
       "$(( (hwl0 + HWTRIPS / 2) / HWTRIPS ))" "$HWBUNDLES"
report "hw-loop back edge is free" "$hwl" "$hwl0"
report "hw-loop costs the same with no branch in it" \
       "$(( (hwn + HWTRIPS / 2) / HWTRIPS ))" "$HWBUNDLES"

# 7a. alternating bundle lengths cost nothing: ten copies of a two-cycle
#     recurrence, plus the latch and the taken conditional branch.
report "alternating bundle lengths cost nothing" \
       "$(per_trip "$mix")" "$(( 10 * 2 + 1 + MIN_TAKEN + 1 ))"

# 8. a late-read operand shortens the dependence into it, each pair measured
#    against its own control.
per_step() { echo $(( ($1 + STEPS / 2) / STEPS )); }
mul_lat="$(per_step "$bpmul")"
alu_lat="$(per_step "$bpalu")"
report "mulw chain costs E2-RR (control)" \
       "$mul_lat" "$(( STAGE_E2 - STAGE_RR ))"
report "maddw accumulator is read a stage later" \
       "$(per_step "$bpmac")" "$(( mul_lat - 1 ))"
report "fmulw to an RR consumer costs E4-RR (control)" \
       "$alu_lat" "$(( STAGE_E4 - STAGE_RR ))"
report "fmulw to a store is a cycle closer" \
       "$(per_step "$bpst")" "$(( alu_lat - 1 ))"

# 9. a wide load's lanes: a scalar read of any one register of the pair `lq'
#    wrote, or of the quad `lo' wrote, waits the whole latency.
per_tuple() { echo $(( ($1 + TUPLES / 2) / TUPLES )); }
report "lq/lo lane use costs 1+(CR-RR)" \
       "$(per_tuple "$tpc")" "$(( 1 + STAGE_CR - STAGE_RR ))"
report "lq.u/lo.u lane use costs 1+(SR-RR)" \
       "$(per_tuple "$tpu")" "$(( 1 + STAGE_SR - STAGE_RR ))"

echo "----"
printf 'minTaken=%s  RR=%s CR=%s SR=%s  (uncached load latency %s, cached %s)\n' \
    "$MIN_TAKEN" "$STAGE_RR" "$STAGE_CR" "$STAGE_SR" \
    $(( STAGE_SR - STAGE_RR )) $(( STAGE_CR - STAGE_RR ))
printf 'alu %s/%s  uncond %s/%s  nottaken %s/%s\n' \
    "$alu0" "$alu" "$unc0" "$unc" "$ant0" "$ant"
printf 'alu %s/%s   ld %s/%s   ld.u %s/%s   ld.u+br %s/%s   (bubble 0 / %s)\n' \
    "$alu0" "$alu" "$ldc0" "$ldc" "$ldu0" "$ldu" "$ldb0" "$ldb" "$MIN_TAKEN"
printf 'tuple lanes: lq/lo %s   lq.u/lo.u %s   shadow(8 fillers) %s\n' \
    "$tpc" "$tpu" "$ldf"
printf 'mixed bundle lengths %s\n' "$mix"
printf 'hw loop %s/%s  no-branch body %s   (bubble 0 / %s)\n' \
    "$hwl0" "$hwl" "$hwn" "$MIN_TAKEN"
printf 'late reads: mulw %s maddw %s   fmulw+alu %s fmulw+store %s\n' \
    "$bpmul" "$bpmac" "$bpalu" "$bpst"
[ $rc -eq 0 ] && echo "check_timing: PASS" || echo "check_timing: FAIL"
exit $rc
