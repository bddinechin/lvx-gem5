/*
 * LVX ISA support for gem5 — LvxStaticInst (Layer C) implementation.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "arch/lvx/static_inst.hh"

#include <algorithm>
#include <sstream>

#include <type_traits>

#include "arch/lvx/behavior_iface.hh"
#include "arch/lvx/generated/lvx_enums.h"
#include "arch/lvx/generated/lvx_stages.h"
#include "arch/lvx/operands.hh"
#include "arch/lvx/pcstate.hh"
#include "arch/lvx/reg_operands.hh"
#include "arch/lvx/regs/int.hh"
#include "arch/lvx/regs/misc.hh"
#include "arch/lvx/shim.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/exec_context.hh"
#include "cpu/thread_context.hh"
#include "debug/LvxDecode.hh"

namespace gem5
{
namespace LvxISA
{

namespace
{

// Syllable fields. Steering is bits 30..29, IMMX tag is bits 28..27, and the
// parallel bit (bit 31) is 1 while more syllables follow in the bundle.
//
// NOTE: the steering values are those of the real encoding / binutils
// disassembler (0=BCU, 1=LSU, 2=EXT, 3=ALU). The lvx_VLIWInstructionBundling.tex
// prose has ALU and EXT swapped (says 2=ALU/3=EXT) — it is wrong; verified
// against actual lvx-mbr-gcc output (e.g. addw/make encode steering 3).
enum Steering { Steer_BCU = 0, Steer_LSU = 1, Steer_EXT = 2, Steer_ALU = 3 };

// Issue slots, in bundle dispatch / binary order.
enum Exu
{
    EXU_BCU0, EXU_BCU1, EXU_ALU0, EXU_ALU1, EXU_LSU0, EXU_LSU1,
    EXU_EXT0, EXU_EXT1, EXU_EXT2, EXU_EXT3, EXU__
};

// This enum and types.hh's MaxBundleIssue are the same ten issue slots counted
// twice.  MaxBundleSyllables is derived from that count, so letting them drift
// is how a bundle-size bound goes stale -- which is exactly what happened to
// the 16 that MaxBundleSyllables used to be.
static_assert(EXU__ == MaxBundleIssue,
              "the Exu enum and MaxBundleIssue must agree: both are the "
              "LVX issue slots, and the bundle syllable bound derives from it");

inline unsigned steering(uint32_t s) { return (s >> 29) & 0x3; }
inline unsigned exuTag(uint32_t s)   { return (s >> 27) & 0x3; }
inline bool     parallelBit(uint32_t s) { return (s >> 31) & 0x1; }

// One instruction being assembled from the bundle: its main syllable plus up
// to two IMMX extension syllables, and the bundle index of the main syllable
// (for PC-relative operands, which are based on the instruction's first
// syllable's PC).
struct IssuedInsn
{
    uint32_t opcode = 0;
    uint32_t immx[2] = {0, 0};
    unsigned immxCount = 0;
    unsigned nsyll = 0;      // 0 == slot unused
    unsigned opcodeIndex = 0;
};

} // anonymous namespace

// Split a fetched bundle into its constituent instructions and decode each.
// Mirrors binutils' lvx_v1_steer_bundle_insns (opcodes/lvx-dis.c): walk the
// syllables in binary order, assign main syllables to issue slots by steering
// (ALU overflow spills into the LSU slots — the TINY ops), and attach IMMX
// syllables (steering 0, beyond the leading BCU pair) to their target ALU/LSU
// instruction by tag; a steering-0 tag-0 syllable right after a lone BCU is
// that branch's offset extension. Instructions are then emitted in issue order.
LvxStaticInst::LvxStaticInst(const ExtMachInst &emi)
    // Placeholder OpClass; setUpRegs() overrides it per bundle to the latency
    // bucket derived from the MDS pipeline stages (see there). IntAluOp here so
    // the object is well-formed before decode.
    : StaticInst("lvx_bundle", IntAluOp), machInst(emi)
{
    bundleBytes = emi.nsyll * sizeof(uint32_t);
    _size = bundleBytes;

    // RISC-V (PS.RV) mode: the "bundle" is a single fixed 32-bit instruction
    // with no VLIW steering. Route the word to the riscv decode space and emit
    // exactly one sub-instruction (see decoder.cc moreBytes / ADR-0001).
    if (emi.rv) {
        bundleBytes = sizeof(uint32_t);
        _size = bundleBytes;
        uint32_t words[MaxInstSyllables] = { emi.syllables[0] };
        Opcode op = Decode_Decoding_riscv(words);
        SubInst &si = subInsts[numSubInsts++];
        si.opcode = (unsigned)op;
        si.exu = (unsigned)EXU_ALU0;
        si.byteOffset = 0;
        lvx_decode_operands(si.opcode, words, si.decoded);
        setUpRegs();
        return;
    }

    IssuedInsn issued[EXU__];
    unsigned bcuInuse = 0, aluInuse = 0, lsuInuse = 0, extInuse = 0;

    auto assign = [&](Exu exu, uint32_t syll, unsigned index) {
        issued[exu].opcode = syll;
        issued[exu].nsyll = 1;
        issued[exu].opcodeIndex = index;
    };

    for (unsigned i = 0; i < emi.nsyll; i++) {
        uint32_t syll = emi.syllables[i];
        switch (steering(syll)) {
          case Steer_BCU:
            if (i == 0) {
                assign(EXU_BCU0, syll, i);       // first BCU -> BCU0
                bcuInuse++;
                flags[IsControl] = true;         // a BCU slot may redirect the PC
            } else if (i == 1 && bcuInuse == 1) {
                if (exuTag(syll) == 0) {         // BCU offset extension
                    issued[EXU_BCU0].immx[0] = syll;
                    issued[EXU_BCU0].immxCount = 1;
                    issued[EXU_BCU0].nsyll = 2;
                } else {
                    assign(EXU_BCU1, syll, i);
                    flags[IsControl] = true;
                }
                bcuInuse++;
            } else {                             // IMMX for an ALU/LSU insn
                Exu tgt = (Exu)(EXU_ALU0 + exuTag(syll));
                IssuedInsn &ins = issued[tgt];
                if (ins.immxCount < 2) {
                    ins.immx[ins.immxCount++] = syll;
                    ins.nsyll++;
                }
            }
            // IsControl marks a bundle holding a BCU *instruction*, and only
            // that: steering 0 is also how an IMMX extension syllable for an
            // ALU/LSU instruction is tagged, and this used to set the flag for
            // those too -- so any bundle with a wide immediate looked like a
            // branch. It costs correctness nowhere (the PC is whatever the
            // behavior wrote) but it is what the taken-branch penalty and the
            // branch predictor key off, so a bundle that cannot branch must not
            // claim it can.
            break;

          case Steer_ALU:                        // ALU spills ALU0,ALU1,LSU0,LSU1
            if (aluInuse == 0)       assign(EXU_ALU0, syll, i), aluInuse++;
            else if (aluInuse == 1)  assign(EXU_ALU1, syll, i), aluInuse++;
            else if (lsuInuse == 0)  assign(EXU_LSU0, syll, i), lsuInuse++;
            else if (lsuInuse == 1)  assign(EXU_LSU1, syll, i), lsuInuse++;
            break;

          case Steer_LSU:
            if (lsuInuse == 0)       assign(EXU_LSU0, syll, i), lsuInuse++;
            else if (lsuInuse == 1)  assign(EXU_LSU1, syll, i), lsuInuse++;
            break;

          case Steer_EXT:
            if (extInuse < 4) assign((Exu)(EXU_EXT0 + extInuse), syll, i), extInuse++;
            break;
        }
        if (!parallelBit(syll))
            break;
    }

    // Emit instructions in issue order; assemble each one's syllable buffer
    // (opcode then its IMMX words) and decode opcode + operands.
    for (int exu = 0; exu < EXU__ && numSubInsts < MaxBundleIssue; exu++) {
        IssuedInsn &ins = issued[exu];
        if (!ins.nsyll)
            continue;
        uint32_t words[MaxInstSyllables] = {};
        unsigned n = 0;
        words[n++] = ins.opcode;
        for (unsigned j = 0; j < ins.immxCount && n < MaxInstSyllables; j++)
            words[n++] = ins.immx[j];

        Opcode op;
        switch (n) {
          case 1:  op = Decode_Decoding_simple(words); break;
          case 2:  op = Decode_Decoding_double(words); break;
          default: op = Decode_Decoding_triple(words); break;
        }

        SubInst &si = subInsts[numSubInsts++];
        si.opcode = (unsigned)op;
        si.exu = (unsigned)exu;
        si.byteOffset = ins.opcodeIndex * sizeof(uint32_t);
        lvx_decode_operands(si.opcode, words, si.decoded);
    }

    setUpRegs();
}

// Build the bundle's aggregated source/destination register lists from the
// generated lvx_reg_deps table, so the timing CPUs (Minor/O3) can compute
// dependencies. A GPR operand's index is decoded[slot] - R0; an SFR operand's is
// decoded[slot] - PC; a fixed register carries its number directly. XACCESSO's
// run-time-indexed XVR reads (lvx_v2, not in this port yet) are why the operand
// path had to carry the block operand: a bufferNReg source expands here into its
// N aligned XVR RegIds -- the dependency the direct storage path used to hide.
void
LvxStaticInst::setUpRegs()
{
    setRegIdxArrays(
        reinterpret_cast<RegIdArrayPtr>(
            &std::remove_pointer_t<decltype(this)>::srcRegIdxArr),
        reinterpret_cast<RegIdArrayPtr>(
            &std::remove_pointer_t<decltype(this)>::destRegIdxArr));
    _numSrcRegs = 0;
    _numDestRegs = 0;

    // One table entry names one operand, and an operand is one register --
    // except PGR and QGR, which are an aligned GPR *pair* and *quad* (a 128-
    // or 256-bit value has no file of its own), and expand into their member
    // GPRs. That expansion is the granule the scoreboard tracks: a scalar read
    // of one half of a vector result has to see the dependency, and a packed
    // write has to be visible to a later scalar read of either register.
    auto forEachReg = [](const LvxRegOperand &o, const uint64_t *decoded,
                         auto &&fn) {
        if (o.file == LVX_RF_SFR) {
            RegIndex i = o.slot >= 0
                ? (RegIndex)(decoded[o.slot] - Register_lvx_PC) : o.fixed;
            fn(miscRegClass[i]);
            return;
        }
        if (o.file == LVX_RF_PGR || o.file == LVX_RF_QGR) {
            const unsigned n = lvxRegFileTupleSize[o.file];
            const unsigned fileBase = o.file == LVX_RF_PGR
                                    ? Register_lvx_P0 : Register_lvx_Q0;
            RegIndex t = o.slot >= 0
                ? (RegIndex)(decoded[o.slot] - fileBase) : o.fixed;
            for (unsigned j = 0; j < n; ++j)
                fn(intRegClass[t * n + j]);
            return;
        }
        RegIndex i = o.slot >= 0
            ? (RegIndex)(decoded[o.slot] - Register_lvx_R0) : o.fixed;
        fn(intRegClass[i]);
    };
    // Per-source relative latency, built alongside the source list. A
    // register read by two instructions of the bundle is one dependency, and
    // the binding constraint is the *earliest* of their read stages -- so the
    // relaxation is the minimum, not the maximum: a register that is some
    // store's value but also somebody's address relaxes by nothing.
    unsigned char relLat[MaxBundleSrcRegs] = {};

    // And per destination, the latency of the instruction that writes it --
    // not of the bundle. A bundle retires at the slowest of its
    // instructions, but each writes its result at its own stage, so a
    // 1-cycle copyd sharing a bundle with a 3-cycle ld is readable after 1.
    // Where two instructions of one bundle write the same register the later
    // write is what a consumer must wait for, so this takes the max.
    unsigned char destLat[MaxBundleDestRegs] = {};

    auto add = [&](const RegId &r, bool dest, unsigned char rel = 0) {
        // Dedup: a register read/written by two instructions of the bundle is one
        // dependency, and dropping the duplicate keeps a valid WAW off the list.
        // Overflow panics rather than truncating: a dropped dependency is a
        // missing stall, so the symptom would be a cycle count that is quietly
        // too low. The bounds are the architectural worst case (static_inst.hh),
        // so this is unreachable unless an operand kind grows.
        if (dest) {
            for (int i = 0; i < _numDestRegs; ++i)
                if (destRegIdxArr[i] == r) {
                    if (rel > destLat[i]) destLat[i] = rel;
                    return;
                }
            panic_if(_numDestRegs >= (int)MaxBundleDestRegs,
                     "LVX bundle has more than %u destination registers",
                     MaxBundleDestRegs);
            destLat[_numDestRegs] = rel;
            setDestRegIdx(_numDestRegs++, r);
        } else {
            for (int i = 0; i < _numSrcRegs; ++i)
                if (srcRegIdxArr[i] == r) {
                    if (rel < relLat[i]) relLat[i] = rel;
                    return;
                }
            panic_if(_numSrcRegs >= (int)MaxBundleSrcRegs,
                     "LVX bundle has more than %u source registers",
                     MaxBundleSrcRegs);
            relLat[_numSrcRegs] = rel;
            setSrcRegIdx(_numSrcRegs++, r);
        }
    };

    unsigned lat = 0;
    for (unsigned i = 0; i < numSubInsts; ++i) {
        const SubInst &si = subInsts[i];
        if (si.opcode >= (unsigned)Opcode__NUM)
            continue;
        const LvxRegDeps &d = lvx_reg_deps[si.opcode];

        // This instruction's own latency, which its destinations are stamped
        // with. A load's is not a property of its opcode alone: the `variant'
        // modifier moves the loaded operand's write stage, so `ld' returns from
        // the L1 at CR (latency 3) while `ld.u'/`ld.us' bypass it and return at
        // SR (latency 15). `lq' and `lq.u' are one Opcode with one Behavior, so
        // the description carries this per modifier *member* (Modifier.yml's
        // LoadedAt, folded into Opcode@timing); BE/GEM5 resolves it to a
        // latency per encoded value, which is what the decoded modifier field
        // indexes. Before 2026-10-16 the ISS costed every load as an L1 hit --
        // twelve cycles short on exactly the access software pipelining and
        // prefetching exist to cover.
        unsigned opLat = d.lat;
        if (d.varSlot >= 0) {
            uint64_t v = si.decoded[d.varSlot];
            if (v < d.nVarLat)
                opLat = d.varLat[v];
        }
        if (opLat > lat) lat = opLat;   // bundle latency = max over syllables

        for (unsigned k = 0; k < d.nsrc; ++k) {
            const unsigned char rel = d.src[k].relLat;
            forEachReg(d.src[k], si.decoded,
                       [&](const RegId &r) { add(r, false, rel); });
        }
        for (unsigned k = 0; k < d.ndst; ++k)
            forEachReg(d.dst[k], si.decoded,
                       [&](const RegId &r) { add(r, true, opLat); });
    }

    // Route the bundle to the MinorCPU functional unit whose opLat equals its
    // result latency, so a dependent bundle stalls the right number of cycles.
    // A result written at pipeline stage S has latency S - RR (lvx_stages.h);
    // the distinct write stages are E1..E4, SF, SR, so the buckets are exactly
    // those latencies. The OpClasses are latency-bucket routing keys matched by
    // LvxFUPool (LvxCPU.py), not semantics -- deliberately non-memory, so
    // MinorCPU expects no LSQ request (the shim serves loads/stores atomically
    // inside execute()). The per-opcode lat already distinguishes e.g. FP16
    // (E3) from FP32/FP64 (E4) FADD/FMUL/FFMA, so no per-class label is needed.
    // One bucket per distinct write stage, highest first. There must be a
    // bucket for every latency the description produces: a lat with no exact
    // bucket silently falls into a lower one and is modelled too cheap, which
    // is what happened to FD (13) until 2026-10-19 -- the FDIV*/FSQRT* family
    // landed in SF's and ran two cycles short. effLat below is what the FU
    // will actually charge, so the two can no longer disagree unnoticed.
    unsigned effLat;
    if      (lat >= LVX_STAGE_SR - LVX_STAGE_RR)
        { _opClass = IntDivOp;     effLat = LVX_STAGE_SR - LVX_STAGE_RR; }
    else if (lat >= LVX_STAGE_FD - LVX_STAGE_RR)
        { _opClass = FloatSqrtOp;  effLat = LVX_STAGE_FD - LVX_STAGE_RR; }
    else if (lat >= LVX_STAGE_SF - LVX_STAGE_RR)
        { _opClass = FloatDivOp;   effLat = LVX_STAGE_SF - LVX_STAGE_RR; }
    else if (lat >= LVX_STAGE_E4 - LVX_STAGE_RR)
        { _opClass = FloatMultOp;  effLat = LVX_STAGE_E4 - LVX_STAGE_RR; }
    else if (lat >= LVX_STAGE_E3 - LVX_STAGE_RR)
        { _opClass = FloatAddOp;   effLat = LVX_STAGE_E3 - LVX_STAGE_RR; }
    else if (lat >= LVX_STAGE_E2 - LVX_STAGE_RR)
        { _opClass = IntMultOp;    effLat = LVX_STAGE_E2 - LVX_STAGE_RR; }
    else
        { _opClass = IntAluOp;     effLat = LVX_STAGE_E1 - LVX_STAGE_RR; }

    // Publish the relaxations only if there are any, so an ordinary bundle
    // keeps the default (null) path and allocates nothing.
    bool anyRelaxed = false;
    for (int i = 0; i < _numSrcRegs; ++i)
        if (relLat[i]) anyRelaxed = true;
    if (anyRelaxed) {
        srcRelLats.resize(_numSrcRegs);
        for (int i = 0; i < _numSrcRegs; ++i)
            srcRelLats[i] = Cycles(relLat[i]);
    } else {
        srcRelLats.clear();
    }

    // Likewise for the destinations, as how much *earlier* than the bundle
    // each is readable. A bundle whose instructions all have the same
    // latency -- which is most of them -- publishes nothing.
    // Measured against effLat, not against lat: the retire time the scoreboard
    // is given comes from the FU, so a reduction computed from anything else
    // would move every result in the bundle by their difference.
    bool anyEarly = false;
    for (int i = 0; i < _numDestRegs; ++i)
        if (destLat[i] < effLat) anyEarly = true;
    if (anyEarly) {
        destRelLats.resize(_numDestRegs);
        for (int i = 0; i < _numDestRegs; ++i)
            destRelLats[i] = Cycles(effLat -
                                    std::min<unsigned>(destLat[i], effLat));
    } else {
        destRelLats.clear();
    }

    if (debug::LvxRegs) {
        std::stringstream ss;
        for (int i = 0; i < _numSrcRegs; ++i)
            ss << " " << srcRegIdxArr[i];
        ss << " ->";
        for (int i = 0; i < _numDestRegs; ++i)
            ss << " " << destRegIdxArr[i];
        DPRINTF(LvxRegs, "bundle deps: src[%d]%s\n", _numSrcRegs, ss.str());
    }
}

Fault
LvxStaticInst::execute(ExecContext *xc, trace::InstRecord *traceData) const
{
    ThreadContext *tc = xc->tcBase();
    Addr base = xc->pcState().instAddr();
    Addr fallThrough = base + bundleBytes;

    // One context per sub-instruction so all source reads (fetch) happen before
    // any register write (commit) — VLIW parallel semantics.
    BundlePredication predication;
    BundleWriteLog writeLog;
    BehaviorContext ctx[MaxBundleIssue];
    for (unsigned i = 0; i < numSubInsts; i++) {
        ctx[i].reset(tc, base + subInsts[i].byteOffset, fallThrough);
        ctx[i].predication = &predication;
        ctx[i].maskUnit = subInsts[i].exu >= EXU_ALU0
                        ? subInsts[i].exu - EXU_ALU0 : ~0u;
        ctx[i].writeLog = &writeLog;
    }

    // A syllable is suppressed when a GUARD in this bundle evaluated false and
    // named that syllable's unit (either GUARD: the bundle keeps the union).
    // Mask bit 0 is ALU0, the first unit after the two BCUs, matching what the
    // assembler encodes.  The BCU slots themselves are never guarded -- GUARD
    // lives in one.
    auto suppressed = [&](unsigned exu) {
        int bit = (int)exu - (int)EXU_ALU0;
        return bit >= 0 && bit < 8 && ((predication.suppressMask >> bit) & 1u) != 0;
    };
    auto isBcu = [](unsigned exu) { return exu == EXU_BCU0 || exu == EXU_BCU1; };
    // Whether a syllable *modifies* memory, from the generated memory
    // classification. The atomics are counted here rather than with the loads
    // because their read and their write are one indivisible access: a plain
    // load beside one has to observe memory before it, and there is no order
    // that also puts the load before a second atomic's read -- which is why a
    // bundle holding two accesses to the same address, at least one of them
    // atomic, is not a legal bundle. Cache maintenance (LVX_MEM_MAINT) writes
    // no data and stays with the readers.
    auto writesMem = [](unsigned opcode) {
        if (opcode >= (unsigned)Opcode__NUM)
            return false;
        unsigned m = lvx_reg_deps[opcode].mem;
        return m == LVX_MEM_STORE || m == LVX_MEM_ATOMIC;
    };

    for (unsigned i = 0; i < numSubInsts; i++)
        runPhase(ctx[i], subInsts[i].opcode, BehaviorFetch, subInsts[i].decoded);

    // The execute pass runs in three groups, and both boundaries are load-
    // bearing.
    //
    // The BCU slots first: GUARD is one of them, and its predicate has to be
    // known before the units it guards run.
    //
    // Then everything that does not modify memory -- the loads among it --
    // before everything that does. A bundle's two LSU slots can hold a load
    // and a store to the same address, and they issue together, so the load
    // must read what memory held before the bundle: the same rule the fetch
    // pass gives the registers, one level down. Without the split the answer
    // depended on which slot the assembler picked, `sd ... ; ld ...' letting
    // the load see the stored value and `ld ... ; sd ...' not. Pinned by
    // tests/lvx/diff/memorder.s.
    //
    // Register reads all happened in the fetch pass above, so neither
    // reordering is observable through a register.
    for (unsigned i = 0; i < numSubInsts; i++)
        if (isBcu(subInsts[i].exu))
            runPhase(ctx[i], subInsts[i].opcode, BehaviorExecute, subInsts[i].decoded);
    for (unsigned pass = 0; pass < 2; pass++)
        for (unsigned i = 0; i < numSubInsts; i++)
            if (!isBcu(subInsts[i].exu) && !suppressed(subInsts[i].exu) &&
                writesMem(subInsts[i].opcode) == (pass == 1))
                runPhase(ctx[i], subInsts[i].opcode, BehaviorExecute,
                         subInsts[i].decoded);

    for (unsigned i = 0; i < numSubInsts; i++)
        if (!suppressed(subInsts[i].exu))
            runPhase(ctx[i], subInsts[i].opcode, BehaviorCommit, subInsts[i].decoded);

    // Next PC: fall-through unless a (BCU) instruction wrote NPC.
    Addr next = fallThrough;
    bool branched = false;
    unsigned xfer = 0;
    for (unsigned i = 0; i < numSubInsts; i++)
        if (ctx[i].npcWritten) {
            next = ctx[i].nextPC;
            branched = true;
            // The cycle *this* instruction settled the next PC at, which is
            // what its taken branch costs beyond minTaken. A bundle may hold
            // two BCU slots with different answers, so it has to be the one
            // that actually transferred, not a property of the bundle.
            xfer = subInsts[i].opcode < (unsigned)Opcode__NUM
                 ? lvx_reg_deps[subInsts[i].opcode].xfer : 0;
        }

    // Hardware-loop back-edge (the fetch-engine mechanism, not any instruction's
    // behavior). LOOPDO merely loaded LS/LE/LC; the loop-back happens here, when
    // the *sequential* next bundle PC reaches LE. A branch taken in this bundle
    // wins over the loop-back (and jumps out of the loop). Mirrors the LVX rule:
    // if LC-1 != 0 -> continue at LS, else fall through past LE; decrement LC
    // unless already zero. With LC = trip count, count 0 is skipped at LOOPDO
    // setup, so the zero-count infinite loop of KVX cannot arise here.
    bool loopBack = false;
    if (misc_reg::ps::hwLoopEnabled() && !branched &&
        fallThrough == tc->readMiscRegNoEffect(misc_reg::LE)) {
        uint64_t lc = tc->readMiscRegNoEffect(misc_reg::LC);
        if (lc > 1) {
            next = tc->readMiscRegNoEffect(misc_reg::LS); // back-edge to loop top
            loopBack = true;
        }
        if (lc != 0)
            tc->setMiscReg(misc_reg::LC, lc - 1);
    }

    std::unique_ptr<PCStateBase> pcp(xc->pcState().clone());
    PCState &pc = pcp->as<PCState>();
    pc.bundleSize(bundleBytes);
    pc.npc(next);
    // The loop engine's back edge costs nothing, so a timing model must not
    // charge it a taken-branch penalty -- and must not let it run down a
    // latency still in flight either, since no cycles pass. Set on every
    // bundle, not only the looping one, so the flag never reads stale.
    pc.freeRedirect(loopBack);
    // goto/call/ret decide in ID and cost minTaken; cb/ccb/igoto/icall wait
    // for a comparison or a register, decide in RR, and cost one more.
    pc.redirectExtraCycles(branched ? xfer : 0);
    xc->pcState(*pcp);

    DPRINTF(LvxDecode, "execute bundle @ %#x: %u insn(s), next=%#x\n",
            base, numSubInsts, next);
    return NoFault;
}

void
LvxStaticInst::advancePC(PCStateBase &pcState) const
{
    pcState.as<PCState>().advance();
}

std::string
LvxStaticInst::generateDisassembly(Addr pc,
                                   const loader::SymbolTable *symtab) const
{
    std::stringstream ss;
    ss << mnemonic << " [";
    for (unsigned i = 0; i < numSubInsts; i++)
        ss << (i ? ", " : "") << "op" << subInsts[i].opcode;
    ss << "]";
    return ss.str();
}

} // namespace LvxISA
} // namespace gem5
