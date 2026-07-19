/*
 * LVX ISA support for gem5 — LvxStaticInst (Layer C) implementation.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "arch/lvx/static_inst.hh"

#include <sstream>

#include <type_traits>

#include "arch/lvx/behavior_iface.hh"
#include "arch/lvx/generated/MDT/MDT_.h"
#include "arch/lvx/operands.hh"
#include "arch/lvx/pcstate.hh"
#include "arch/lvx/reg_operands.hh"
#include "arch/lvx/regs/int.hh"
#include "arch/lvx/regs/misc.hh"
#include "arch/lvx/shim.hh"
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
    // IntAluOp so a MinorCPU FU accepts the whole bundle (No_OpClass matches
    // none). A non-memory class is deliberate: the bundle's loads/stores are
    // served atomically inside execute() via the shim, so MinorCPU must not
    // route it to the LSQ expecting a memory request. This gives the bundle a
    // uniform issue latency (MinorDefaultIntFU); per-op-class / per-result
    // latency calibration from the MDS Scheduling tables is a follow-up.
    : StaticInst("lvx_bundle", IntAluOp), machInst(emi)
{
    bundleBytes = emi.nsyll * sizeof(uint32_t);
    _size = bundleBytes;

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
            } else if (i == 1 && bcuInuse == 1) {
                if (exuTag(syll) == 0) {         // BCU offset extension
                    issued[EXU_BCU0].immx[0] = syll;
                    issued[EXU_BCU0].immxCount = 1;
                    issued[EXU_BCU0].nsyll = 2;
                } else {
                    assign(EXU_BCU1, syll, i);
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
            flags[IsControl] = true;             // a BCU slot may redirect the PC
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
    for (int exu = 0; exu < EXU__ && numSubInsts < MaxBundleSyllables; exu++) {
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
          case 1:  op = Decode_Decoding_lvx_v1_simple(words); break;
          case 2:  op = Decode_Decoding_lvx_v1_double(words); break;
          default: op = Decode_Decoding_lvx_v1_triple(words); break;
        }

        SubInst &si = subInsts[numSubInsts++];
        si.opcode = (unsigned)op;
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

    auto regId = [](const LvxRegOperand &o, const uint64_t *decoded) -> RegId {
        if (o.file == LVX_RF_GPR) {
            RegIndex i = o.slot >= 0
                ? (RegIndex)(decoded[o.slot] - Register_lvx_v1_R0) : o.fixed;
            return intRegClass[i];
        }
        RegIndex i = o.slot >= 0
            ? (RegIndex)(decoded[o.slot] - Register_lvx_v1_PC) : o.fixed;
        return miscRegClass[i];
    };
    auto add = [&](const RegId &r, bool dest) {
        // Dedup: a register read/written by two instructions of the bundle is one
        // dependency, and dropping the duplicate keeps a valid WAW off the list.
        if (dest) {
            for (int i = 0; i < _numDestRegs; ++i)
                if (destRegIdxArr[i] == r) return;
            if (_numDestRegs < (int)MaxBundleDestRegs)
                setDestRegIdx(_numDestRegs++, r);
        } else {
            for (int i = 0; i < _numSrcRegs; ++i)
                if (srcRegIdxArr[i] == r) return;
            if (_numSrcRegs < (int)MaxBundleSrcRegs)
                setSrcRegIdx(_numSrcRegs++, r);
        }
    };

    for (unsigned i = 0; i < numSubInsts; ++i) {
        const SubInst &si = subInsts[i];
        if (si.opcode >= (unsigned)Opcode__NUM)
            continue;
        const LvxRegDeps &d = lvx_reg_deps[si.opcode];
        for (unsigned k = 0; k < d.nsrc; ++k) add(regId(d.src[k], si.decoded), false);
        for (unsigned k = 0; k < d.ndst; ++k) add(regId(d.dst[k], si.decoded), true);
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
    BehaviorContext ctx[MaxBundleSyllables];
    for (unsigned i = 0; i < numSubInsts; i++)
        ctx[i].reset(tc, base + subInsts[i].byteOffset, fallThrough);

    for (unsigned i = 0; i < numSubInsts; i++)
        runPhase(ctx[i], subInsts[i].opcode, BehaviorFetch, subInsts[i].decoded);
    for (unsigned i = 0; i < numSubInsts; i++)
        runPhase(ctx[i], subInsts[i].opcode, BehaviorExecute, subInsts[i].decoded);
    for (unsigned i = 0; i < numSubInsts; i++)
        runPhase(ctx[i], subInsts[i].opcode, BehaviorCommit, subInsts[i].decoded);

    // Next PC: fall-through unless a (BCU) instruction wrote NPC.
    Addr next = fallThrough;
    for (unsigned i = 0; i < numSubInsts; i++)
        if (ctx[i].npcWritten)
            next = ctx[i].nextPC;

    std::unique_ptr<PCStateBase> pcp(xc->pcState().clone());
    PCState &pc = pcp->as<PCState>();
    pc.bundleSize(bundleBytes);
    pc.npc(next);
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
