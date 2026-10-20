/*
 * LVX ISA support for gem5 — LvxStaticInst (Layer C) implementation.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "arch/lvx/static_inst.hh"

#include <algorithm>
#include <cstring>
#include <sstream>

#include <type_traits>

#include "arch/lvx/behavior_iface.hh"
#include "arch/lvx/generated/lvx_enums.h"
#include "arch/lvx/isa.hh"
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
    // A bundle is a macroop and its syllables are its micro-ops; gem5 reads
    // this flag rather than calling a virtual, so it has to be set here.
    flags[IsMacroop] = true;
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
        buildMicroOrder();
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

    buildMicroOrder();
}

// ---------------------------------------------------------------------------
// Register lists
//
// One instruction's sources and destinations, as gem5 wants them, built from
// the generated lvx_reg_deps table. Two callers want different subsets of a
// bundle -- the head micro-op takes the whole bundle's sources, every micro-op
// takes its own syllable's destinations -- so this is a builder rather than a
// method: `add' is called once per syllable with what is wanted from it.
//
// A GPR operand's index is decoded[slot] - R0; an SFR operand's is
// decoded[slot] - PC; a fixed register carries its number directly.
// XACCESSO's run-time-indexed XVR reads (lvx_v2, not in this port yet) are why
// the operand path had to carry the block operand: a bufferNReg source expands
// here into its N aligned XVR RegIds -- the dependency the direct storage path
// used to hide.
namespace
{

// One table entry names one operand, and an operand is one register -- except
// PGR and QGR, which are an aligned GPR *pair* and *quad* (a 128- or 256-bit
// value has no file of its own), and expand into their member GPRs. That
// expansion is the granule the scoreboard tracks: a scalar read of one half of
// a vector result has to see the dependency, and a packed write has to be
// visible to a later scalar read of either register.
template <typename F>
void
forEachReg(const LvxRegOperand &o, const uint64_t *decoded, F &&fn)
{
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
}

// This syllable's own result latency, which its destinations are stamped with.
// A load's is not a property of its opcode alone: the `variant' modifier moves
// the loaded operand's write stage, so `ld' returns from the L1 at CR (latency
// 3) while `ld.u'/`ld.us' bypass it and return at SR (latency 15). `lq' and
// `lq.u' are one Opcode with one Behavior, so the description carries this per
// modifier *member* (Modifier.yml's LoadedAt, folded into Opcode@timing);
// BE/GEM5 resolves it to a latency per encoded value, which is what the
// decoded modifier field indexes. Before 2026-10-16 the ISS costed every load
// as an L1 hit -- twelve cycles short on exactly the access software
// pipelining and prefetching exist to cover.
unsigned
syllableLatency(const SubInst &si)
{
    if (si.opcode >= (unsigned)Opcode__NUM)
        return 0;
    const LvxRegDeps &d = lvx_reg_deps[si.opcode];
    if (d.varSlot >= 0) {
        uint64_t v = si.decoded[d.varSlot];
        if (v < d.nVarLat)
            return d.varLat[v];
    }
    return d.lat;
}

struct RegLists
{
    RegId src[MaxBundleSrcRegs];
    // Per-source relative latency. A register read by two instructions of the
    // bundle is one dependency, and the binding constraint is the *earliest*
    // of their read stages -- so the relaxation is the minimum, not the
    // maximum: a register that is some store's value but also somebody's
    // address relaxes by nothing.
    unsigned char srcRel[MaxBundleSrcRegs] = {};
    int nsrc = 0;

    RegId dst[MaxBundleDestRegs];
    // And per destination, the latency of the instruction that writes it.
    // Where two instructions of one bundle write the same register the later
    // write is what a consumer must wait for, so this takes the max.
    unsigned char dstLat[MaxBundleDestRegs] = {};
    int ndst = 0;

    unsigned lat = 0;   // max over the syllables added

    // Dedup: a register read or written by two instructions of the bundle is
    // one dependency, and dropping the duplicate keeps a valid WAW off the
    // list. Overflow panics rather than truncating: a dropped dependency is a
    // missing stall, so the symptom would be a cycle count that is quietly too
    // low. The bounds are the architectural worst case (static_inst.hh), so
    // this is unreachable unless an operand kind grows.
    void
    addSrc(const RegId &r, unsigned char rel)
    {
        for (int i = 0; i < nsrc; ++i)
            if (src[i] == r) {
                if (rel < srcRel[i]) srcRel[i] = rel;
                return;
            }
        panic_if(nsrc >= (int)MaxBundleSrcRegs,
                 "LVX bundle has more than %u source registers",
                 MaxBundleSrcRegs);
        srcRel[nsrc] = rel;
        src[nsrc++] = r;
    }

    void
    addDst(const RegId &r, unsigned char rel)
    {
        for (int i = 0; i < ndst; ++i)
            if (dst[i] == r) {
                if (rel > dstLat[i]) dstLat[i] = rel;
                return;
            }
        panic_if(ndst >= (int)MaxBundleDestRegs,
                 "LVX bundle has more than %u destination registers",
                 MaxBundleDestRegs);
        dstLat[ndst] = rel;
        dst[ndst++] = r;
    }

    void
    add(const SubInst &si, bool wantSrc, bool wantDst)
    {
        if (si.opcode >= (unsigned)Opcode__NUM)
            return;
        const LvxRegDeps &d = lvx_reg_deps[si.opcode];
        const unsigned opLat = syllableLatency(si);
        if (opLat > lat) lat = opLat;

        if (wantSrc) {
            for (unsigned k = 0; k < d.nsrc; ++k) {
                const unsigned char rel = d.src[k].relLat;
                forEachReg(d.src[k], si.decoded,
                           [&](const RegId &r) { addSrc(r, rel); });
            }
        }
        if (wantDst) {
            for (unsigned k = 0; k < d.ndst; ++k)
                forEachReg(d.dst[k], si.decoded,
                           [&](const RegId &r) { addDst(r, opLat); });
        }
    }
};

// Route an instruction to the MinorCPU functional unit whose opLat equals its
// result latency, so a dependent instruction stalls the right number of
// cycles. A result written at pipeline stage S has latency S - RR
// (lvx_stages.h); the distinct write stages are E1..E4, SF, FD, SR, so the
// buckets are exactly those latencies. The OpClasses are latency-bucket
// routing keys matched by LvxFUPool (LvxCPU.py), not semantics -- deliberately
// non-memory while the shim still serves loads and stores functionally inside
// the behaviour. The per-opcode lat already distinguishes e.g. FP16 (E3) from
// FP32/FP64 (E4) FADD/FMUL/FFMA, so no per-class label is needed.
//
// There must be a bucket for every latency the description produces: a lat
// with no exact bucket silently falls into a lower one and is modelled too
// cheap, which is what happened to FD (13) until 2026-10-19 -- the
// FDIV*/FSQRT* family landed in SF's and ran two cycles short. effLat is what
// the FU will actually charge, so the two can no longer disagree unnoticed.
struct FuBucket
{
    OpClass opClass;
    unsigned effLat;
};

FuBucket
bucketFor(unsigned lat)
{
    if (lat >= LVX_STAGE_SR - LVX_STAGE_RR)
        return { IntDivOp,    LVX_STAGE_SR - LVX_STAGE_RR };
    if (lat >= LVX_STAGE_FD - LVX_STAGE_RR)
        return { FloatSqrtOp, LVX_STAGE_FD - LVX_STAGE_RR };
    if (lat >= LVX_STAGE_SF - LVX_STAGE_RR)
        return { FloatDivOp,  LVX_STAGE_SF - LVX_STAGE_RR };
    if (lat >= LVX_STAGE_E4 - LVX_STAGE_RR)
        return { FloatMultOp, LVX_STAGE_E4 - LVX_STAGE_RR };
    if (lat >= LVX_STAGE_E3 - LVX_STAGE_RR)
        return { FloatAddOp,  LVX_STAGE_E3 - LVX_STAGE_RR };
    if (lat >= LVX_STAGE_E2 - LVX_STAGE_RR)
        return { IntMultOp,   LVX_STAGE_E2 - LVX_STAGE_RR };
    return { IntAluOp,        LVX_STAGE_E1 - LVX_STAGE_RR };
}

// Does this syllable modify memory? The atomics count here rather than with
// the loads because their read and their write are one indivisible access: a
// plain load beside one has to observe memory before it, and there is no order
// that also puts the load before a second atomic's read -- which is why a
// bundle holding two accesses to the same address, at least one of them
// atomic, is not a legal bundle. Cache maintenance (LVX_MEM_MAINT) writes no
// data and stays with the readers.
bool
writesMem(unsigned opcode)
{
    if (opcode >= (unsigned)Opcode__NUM)
        return false;
    unsigned m = lvx_reg_deps[opcode].mem;
    return m == LVX_MEM_STORE || m == LVX_MEM_ATOMIC;
}

bool isBcuSlot(unsigned exu) { return exu == EXU_BCU0 || exu == EXU_BCU1; }

// Does this syllable transfer control -- i.e. write NPC? Not a question
// `xfer' can answer: it is the *cycle* of the transfer, so its 0 means both
// "transfers in ID" and "does not transfer", and `goto' and `guard' are both
// BCU opcodes with xfer 0. The generator reads this out of the bodies'
// writeToStorage_NPC calls for exactly this caller.
bool
transfersControl(unsigned opcode)
{
    if (opcode >= (unsigned)Opcode__NUM)
        return false;
    return lvx_reg_deps[opcode].ctl != 0;
}

} // anonymous namespace

// The order the bundle's syllables execute in. It is not slot order, and all
// three boundaries are load-bearing.
//
//   1. non-transfer BCU slots first. GUARD is one of them, and the union of
//      the bundle's GUARD predicates has to be known before any syllable it
//      suppresses runs. (A BCU slot is itself never guarded.)
//   2. then the syllables that do not modify memory, the loads among them,
//      before those that do: a bundle's two LSU slots can hold a load and a
//      store to the same address and they issue together, so the load must
//      read what memory held before the bundle. Without this the answer
//      depended on which slot the assembler picked. tests/lvx/diff/memorder.s.
//   3. the control transfer last. Minor squashes younger instructions when a
//      branch is taken, and a micro-op of the same bundle is younger than the
//      branch -- so a transfer anywhere but last would kill the rest of its
//      own bundle. Its own syllable's state is unaffected either way, every
//      register read having happened in the head's snapshot.
void
LvxStaticInst::buildMicroOrder()
{
    unsigned m = 0;
    // group, in order: non-transfer BCU, non-mem-writer, mem-writer, transfer
    for (unsigned g = 0; g < 4; ++g) {
        for (unsigned i = 0; i < numSubInsts; ++i) {
            const unsigned op = subInsts[i].opcode;
            const bool bcu = isBcuSlot(subInsts[i].exu);
            const bool xfer = bcu && transfersControl(op);
            unsigned mine;
            if (xfer)                        mine = 3;
            else if (bcu)                    mine = 0;
            else if (writesMem(op))          mine = 2;
            else                             mine = 1;
            if (mine == g)
                microOrder[m++] = i;
        }
    }
    panic_if(m != numSubInsts,
             "LVX bundle at micro-op ordering: %u of %u syllables placed",
             m, numSubInsts);
}

StaticInstPtr
LvxStaticInst::fetchMicroop(MicroPC upc) const
{
    panic_if(upc >= numSubInsts,
             "LVX bundle has %u micro-ops, asked for %u", numSubInsts, upc);
    // Built on first use rather than in the constructor: a bundle that is
    // decoded and then squashed never pays for them, and the decode cache
    // means a bundle that does run pays once.
    if (!microops[upc])
        microops[upc] = new LvxMicroInst(mnemonic,
            const_cast<LvxStaticInst *>(this), upc);
    return microops[upc];
}

// The macroop itself never executes: both CPU models replace it with
// fetchMicroop before anything runs (BaseSimpleCPU::preExecute,
// Decode::evaluate). Say so loudly rather than quietly doing nothing, since
// the alternative -- a bundle whose syllables never ran -- would look like a
// miscompile.
Fault
LvxStaticInst::execute(ExecContext *xc, trace::InstRecord *traceData) const
{
    panic("LVX: a bundle macroop was executed directly; gem5 should have "
          "fetched its micro-ops (isMacroop/fetchMicroop)");
}

// Run micro-op `mi' of this bundle.
//
// The bundle's parallel semantics live in the head micro-op's snapshot: it
// runs *every* syllable's fetch phase, which is every register read the
// bundle makes, into the thread's BundleExec. Each micro-op then executes and
// commits out of that, so a register written by one syllable is invisible to
// the next -- which is what makes `copyd $r1 = $r2 ; copyd $r2 = $r1' an
// exchange (tests/lvx/diff/parallel.s).
//
// Why a snapshot and not deferred writes: both work, and this one leaves the
// behaviour code exactly as it was. Note that functional correctness does not
// depend on the timing model at all -- gem5 executes at *commit* and commits
// in order, so every older instruction has already written its registers
// before the head micro-op snapshots them, whatever the scoreboard said.
// Take the bundle's operand snapshot if this is the head micro-op, check that
// the snapshot in hand is this bundle's, and say whether a GUARD in the bundle
// suppressed this syllable.
//
// The snapshot is the whole of the bundle's parallel read semantics: it runs
// *every* syllable's fetch phase, which is every register read the bundle
// makes, so a register written by one syllable is invisible to the next.
//
// Why a snapshot and not deferred writes: both work, and this one leaves the
// behaviour code exactly as it was. Note that functional correctness does not
// depend on the timing model at all -- gem5 executes at *commit* and commits
// in order, so every older instruction has already written its registers
// before the head micro-op snapshots them, whatever the scoreboard said.
bool
LvxStaticInst::prepareMicro(ExecContext *xc, unsigned mi) const
{
    ThreadContext *tc = xc->tcBase();
    BundleExec &be = static_cast<ISA *>(tc->getIsaPtr())->bundleExec();
    const Addr base = xc->pcState().instAddr();

    if (mi == 0) {
        const Addr fallThrough = base + bundleBytes;
        be.predication.reset();
        be.writeLog.reset();
        for (unsigned k = 0; k < numSubInsts; k++) {
            be.ctx[k].reset(tc, base + subInsts[k].byteOffset, fallThrough);
            be.ctx[k].predication = &be.predication;
            // GUARD's immediate is a mask of guarded execution units, so a
            // masked instruction has to know which unit it landed on. Bit 0
            // is ALU0, the first unit after the two BCUs, matching what the
            // assembler encodes; a BCU slot gets ~0u, no prefix naming one.
            be.ctx[k].maskUnit = subInsts[k].exu >= EXU_ALU0
                               ? subInsts[k].exu - EXU_ALU0 : ~0u;
            be.ctx[k].writeLog = &be.writeLog;
        }
        be.base = base;
        be.n = numSubInsts;
        be.live = true;
        for (unsigned k = 0; k < numSubInsts; k++)
            runPhase(be.ctx[k], subInsts[k].opcode, BehaviorFetch,
                     subInsts[k].decoded);
    }

    // The snapshot has to be this bundle's. It cannot not be -- commit is in
    // order and a micro-op sequence is never interleaved -- so this is here to
    // make a future violation a panic instead of a wrong answer, which is the
    // one failure mode of the whole scheme.
    panic_if(!be.live || be.base != base || be.n != numSubInsts,
             "LVX bundle at %#x micro-op %u: operand snapshot is for %#x "
             "(%u syllables, live=%d)", base, mi, be.base, be.n, be.live);

    // A syllable is suppressed when a GUARD in this bundle evaluated false and
    // named that syllable's unit (either GUARD: the bundle keeps the union).
    // Every GUARD has run by now -- buildMicroOrder puts the non-transfer BCU
    // slots first, and a BCU slot is itself never guarded.
    const int bit = (int)subInsts[microOrder[mi]].exu - (int)EXU_ALU0;
    return bit >= 0 && bit < 8 &&
        ((be.predication.suppressMask >> bit) & 1u) != 0;
}

// Run micro-op `mi' of this bundle: its syllable's execute and commit phases
// out of the snapshot, then the bundle-level work if it is the last.
Fault
LvxStaticInst::executeMicro(ExecContext *xc, trace::InstRecord *traceData,
                            unsigned mi) const
{
    BundleExec &be =
        static_cast<ISA *>(xc->tcBase()->getIsaPtr())->bundleExec();
    const bool suppressed = prepareMicro(xc, mi);
    const unsigned i = microOrder[mi];

    if (!suppressed) {
        runPhase(be.ctx[i], subInsts[i].opcode, BehaviorExecute,
                 subInsts[i].decoded);
        runPhase(be.ctx[i], subInsts[i].opcode, BehaviorCommit,
                 subInsts[i].decoded);
    }

    if (mi + 1 == numSubInsts)
        finishBundle(xc, be);

    return NoFault;
}

// First half of a micro-op served through the load/store queue: compute the
// address and issue the request. Nothing is committed here, and nothing may
// be: Execute::executeMemRefInst restores the thread's PC afterwards, so the
// bundle-level work has to wait for completeMicro.
//
// A suppressed syllable issues nothing and says so through the memory-access
// predicate, which rides on the MinorDynInst (ExecContext's constructor loads
// it from there and its destructor stores it back). Minor then pushes a failed
// request and calls completeAcc with a null packet, which is where the
// bundle's remaining work gets done.
Fault
LvxStaticInst::initiateMicro(ExecContext *xc, trace::InstRecord *traceData,
                             unsigned mi) const
{
    BundleExec &be =
        static_cast<ISA *>(xc->tcBase()->getIsaPtr())->bundleExec();
    const bool suppressed = prepareMicro(xc, mi);
    const unsigned i = microOrder[mi];

    if (suppressed) {
        xc->setMemAccPredicate(false);
        return NoFault;
    }

    be.ctx[i].xc = xc;
    be.ctx[i].memPhase = MemIssue;
    runPhase(be.ctx[i], subInsts[i].opcode, BehaviorExecute,
             subInsts[i].decoded);
    be.ctx[i].memPhase = MemFunctional;
    return NoFault;
}

// Second half: the response has arrived, so re-run the execute phase with the
// data in hand and commit for real. The two passes differ only in what the
// memory helper returns -- a load or store body touches nothing else (see
// MemPhase, and the check in BE/GEM5's reg-operands.pl).
Fault
LvxStaticInst::completeMicro(PacketPtr pkt, ExecContext *xc,
                             trace::InstRecord *traceData, unsigned mi) const
{
    BundleExec &be =
        static_cast<ISA *>(xc->tcBase()->getIsaPtr())->bundleExec();
    const unsigned i = microOrder[mi];
    // prepareMicro is not called again: the snapshot is the one initiateMicro
    // took, and re-running the head's fetch phase here would read registers
    // that older syllables of this same bundle have since committed.
    const bool suppressed = !xc->readMemAccPredicate();

    if (!suppressed) {
        if (pkt && pkt->getSize() > 0) {
            const unsigned n = std::min<unsigned>(pkt->getSize(),
                                                  sizeof(be.ctx[i].memData));
            std::memcpy(be.ctx[i].memData.bytes,
                        pkt->getConstPtr<uint8_t>(), n);
        }
        be.ctx[i].xc = xc;
        be.ctx[i].memPhase = MemComplete;
        runPhase(be.ctx[i], subInsts[i].opcode, BehaviorExecute,
                 subInsts[i].decoded);
        be.ctx[i].memPhase = MemFunctional;
        runPhase(be.ctx[i], subInsts[i].opcode, BehaviorCommit,
                 subInsts[i].decoded);
    }

    if (mi + 1 == numSubInsts)
        finishBundle(xc, be);

    return NoFault;
}

// The bundle-level work, done by the last micro-op: where execution goes next.
// It is the last and not the first because a taken branch squashes everything
// younger, and a micro-op of this bundle is younger than the branch -- so the
// transferring syllable is ordered last (buildMicroOrder) and this runs with
// it.
void
LvxStaticInst::finishBundle(ExecContext *xc, BundleExec &be) const
{
    ThreadContext *tc = xc->tcBase();
    const Addr base = be.base;
    const Addr fallThrough = base + bundleBytes;

    // Next PC: fall-through unless a (BCU) instruction wrote NPC.
    Addr next = fallThrough;
    bool branched = false;
    unsigned xfer = 0;
    for (unsigned i = 0; i < numSubInsts; i++)
        if (be.ctx[i].npcWritten) {
            next = be.ctx[i].nextPC;
            branched = true;
            // The cycle *this* instruction settled the next PC at, which is
            // what its taken branch costs beyond minTaken. A bundle may hold
            // two BCU slots with different answers, so it has to be the one
            // that actually transferred, not a property of the bundle.
            xfer = subInsts[i].opcode < (unsigned)Opcode__NUM
                 ? lvx_reg_deps[subInsts[i].opcode].xfer : 0;
        }

    // Hardware-loop back-edge (the fetch-engine mechanism, not any
    // instruction's behavior). LOOPDO merely loaded LS/LE/LC; the loop-back
    // happens here, when the *sequential* next bundle PC reaches LE. A branch
    // taken in this bundle wins over the loop-back (and jumps out of the
    // loop). Mirrors the LVX rule: if LC-1 != 0 -> continue at LS, else fall
    // through past LE; decrement LC unless already zero. With LC = trip count,
    // count 0 is skipped at LOOPDO setup, so the zero-count infinite loop of
    // KVX cannot arise here.
    bool loopBack = false;
    if (misc_reg::ps::hwLoopEnabled() && !branched &&
        fallThrough == tc->readMiscRegNoEffect(misc_reg::LE)) {
        uint64_t lc = tc->readMiscRegNoEffect(misc_reg::LC);
        if (lc > 1) {
            next = tc->readMiscRegNoEffect(misc_reg::LS); // back to loop top
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

    be.live = false;

    DPRINTF(LvxDecode, "execute bundle @ %#x: %u insn(s), next=%#x\n",
            base, numSubInsts, next);
}

// ---------------------------------------------------------------------------
// LvxMicroInst

LvxMicroInst::LvxMicroInst(const char *mnem, StaticInstPtr macroop,
                           unsigned idx)
    : StaticInst(mnem, No_OpClass), macro(macroop), index(idx)
{
    const LvxStaticInst &b = *static_cast<LvxStaticInst *>(macroop.get());
    const SubInst &si = b.subInsts[b.microOrder[idx]];

    setRegIdxArrays(
        reinterpret_cast<RegIdArrayPtr>(
            &std::remove_pointer_t<decltype(this)>::srcRegIdxArr),
        reinterpret_cast<RegIdArrayPtr>(
            &std::remove_pointer_t<decltype(this)>::destRegIdxArr));

    flags[IsMicroop] = true;
    flags[IsFirstMicroop] = (idx == 0);
    flags[IsLastMicroop] = (idx + 1 == b.numSubInsts);
    // Every micro-op but the last is mid-instruction: the bundle is not an
    // architectural state the machine may be interrupted in. Minor does not
    // consult this today (SE mode takes no interrupts), the simple CPUs do.
    flags[IsDelayedCommit] = !flags[IsLastMicroop];
    // Only the syllable that writes NPC is a control instruction -- which is
    // why it is ordered last. The taken-branch penalty and the squash both
    // key off this.
    flags[IsControl] = transfersControl(si.opcode);
    // Does this access go through the load/store queue? The generator
    // decides, because the answer needs more than the memory class: the port
    // runs a split access's execute phase twice, so only one that touches
    // nothing else qualifies. See lvx_reg_deps[].lsq. Everything excluded --
    // the atomics, lvx_v2's extension accesses -- keeps the functional path,
    // as does every access under the simple CPU models, which have no queue.
    const bool lsq = si.opcode < (unsigned)Opcode__NUM
                   && lvx_reg_deps[si.opcode].lsq;
    const unsigned mem = si.opcode < (unsigned)Opcode__NUM
                       ? lvx_reg_deps[si.opcode].mem : LVX_MEM_NONE;
    flags[IsLoad]  = lsq && (mem == LVX_MEM_LOAD);
    flags[IsStore] = lsq && (mem == LVX_MEM_STORE);

    RegLists lists;
    // The head carries the *bundle's* sources, so a bundle cannot stall
    // mid-issue: every later micro-op's sources are a subset of these with a
    // read stage no earlier. The others carry none.
    if (idx == 0)
        for (unsigned k = 0; k < b.numSubInsts; ++k)
            lists.add(b.subInsts[k], true, false);
    // Destinations are this syllable's own, stamped with its own latency.
    lists.add(si, false, true);

    _numSrcRegs = 0;
    _numDestRegs = 0;
    for (int k = 0; k < lists.nsrc; ++k)
        setSrcRegIdx(_numSrcRegs++, lists.src[k]);
    for (int k = 0; k < lists.ndst; ++k)
        setDestRegIdx(_numDestRegs++, lists.dst[k]);

    // This micro-op's own functional unit, from its own syllable's latency --
    // not the bundle's. The head's aggregated sources say *when* the bundle
    // may issue; this says what each result costs, so a 1-cycle copyd beside
    // a 15-cycle uncached load is readable after one cycle.
    const unsigned ownLat = syllableLatency(si);
    const FuBucket bucket = bucketFor(ownLat);
    _opClass = bucket.opClass;

    // Publish the relaxations only when there are any, so an ordinary
    // micro-op keeps the default (null) path and allocates nothing.
    bool anyRelaxed = false;
    for (int k = 0; k < lists.nsrc; ++k)
        if (lists.srcRel[k]) anyRelaxed = true;
    if (anyRelaxed) {
        srcRelLats.resize(lists.nsrc);
        for (int k = 0; k < lists.nsrc; ++k)
            srcRelLats[k] = Cycles(lists.srcRel[k]);
    }

    // And per destination, how much *earlier* than this micro-op's FU end the
    // result is readable. Mostly nothing now that the FU is per syllable; it
    // still matters where one syllable writes two registers at different
    // stages. Measured against effLat, not against the raw latency: the retire
    // time the scoreboard is given comes from the FU.
    bool anyEarly = false;
    for (int k = 0; k < lists.ndst; ++k)
        if (lists.dstLat[k] < bucket.effLat) anyEarly = true;
    if (anyEarly) {
        destRelLats.resize(lists.ndst);
        for (int k = 0; k < lists.ndst; ++k)
            destRelLats[k] = Cycles(bucket.effLat -
                std::min<unsigned>(lists.dstLat[k], bucket.effLat));
    }

    if (debug::LvxRegs) {
        std::stringstream ss;
        for (int k = 0; k < _numSrcRegs; ++k)
            ss << " " << srcRegIdxArr[k];
        ss << " ->";
        for (int k = 0; k < _numDestRegs; ++k)
            ss << " " << destRegIdxArr[k];
        DPRINTF(LvxRegs, "micro-op %u of bundle: src[%d]%s\n",
                index, _numSrcRegs, ss.str());
    }
}

Fault
LvxMicroInst::execute(ExecContext *xc, trace::InstRecord *traceData) const
{
    return static_cast<LvxStaticInst *>(macro.get())
        ->executeMicro(xc, traceData, index);
}

Fault
LvxMicroInst::initiateAcc(ExecContext *xc, trace::InstRecord *traceData) const
{
    return static_cast<LvxStaticInst *>(macro.get())
        ->initiateMicro(xc, traceData, index);
}

Fault
LvxMicroInst::completeAcc(PacketPtr pkt, ExecContext *xc,
                          trace::InstRecord *traceData) const
{
    return static_cast<LvxStaticInst *>(macro.get())
        ->completeMicro(pkt, xc, traceData, index);
}

// All but the last micro-op step the micro-PC; the last ends the bundle and
// takes the next PC finishBundle staged.
//
// uEnd(), not advance(): advance() is SimplePCState's and moves only the PC,
// leaving the micro-PC where the last uAdvance() put it -- so the next bundle
// would be asked for the micro-op index this one stopped at. uEnd() is the
// one that both advances and resets.
void
LvxMicroInst::advancePC(PCStateBase &pcState) const
{
    if (isLastMicroop())
        pcState.as<PCState>().uEnd();
    else
        pcState.as<PCState>().uAdvance();
}

std::string
LvxMicroInst::generateDisassembly(Addr pc,
                                  const loader::SymbolTable *symtab) const
{
    const LvxStaticInst &b = *static_cast<LvxStaticInst *>(macro.get());
    std::stringstream ss;
    ss << mnemonic << " op" << b.subInsts[b.microOrder[index]].opcode
       << " [slot " << index + 1 << "/" << b.numSubInsts << "]";
    return ss.str();
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
