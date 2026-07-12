/*
 * LVX ISA support for gem5 — LvxStaticInst (Layer C) implementation.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "arch/lvx/static_inst.hh"

#include <sstream>

#include "arch/lvx/behavior_iface.hh"
#include "arch/lvx/operands.hh"
#include "arch/lvx/pcstate.hh"
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

// Steering field (syllable bits 30..29): 0 BCU / 1 LSU / 2 ALU / 3 EXT.
inline unsigned steering(uint32_t s) { return (s >> 29) & 0x3; }
// Parallel bit (bit 31): 1 = more syllables follow in the bundle.
inline bool parallel(uint32_t s) { return (s >> 31) & 0x1; }

// Decode one syllable as a simple (single-syllable) instruction. Layer C is
// currently scalar-only: multi-syllable (double/triple) instructions and IMMX
// extension syllables are TODO — see the note in the ctor.
Opcode
decodeSimple(uint32_t syllable)
{
    return Decode_Decoding_lvx_v1_simple(&syllable);
}

} // anonymous namespace

LvxStaticInst::LvxStaticInst(const ExtMachInst &emi)
    : StaticInst("lvx_bundle", No_OpClass), machInst(emi)
{
    bundleBytes = emi.nsyll * sizeof(uint32_t);
    _size = bundleBytes;

    // MVP bundle split: treat each syllable as one simple instruction. This is
    // correct for scalar -O0 code (one single-syllable instruction per bundle,
    // no IMMX). TODO(Layer C): honor the steering order (BCU0,BCU1,ALU0,ALU1,
    // LSU0,LSU1,EXT0,EXT1), attach trailing IMMX syllables (steering 0, at the
    // bundle end) to their target ALU/LSU instruction, and decode double/triple
    // encodings — see lvx_VLIWInstructionBundling.tex.
    for (unsigned i = 0; i < emi.nsyll && numSubInsts < MaxBundleSyllables; i++) {
        uint32_t syll = emi.syllables[i];
        Opcode op = decodeSimple(syll);
        SubInst &si = subInsts[numSubInsts++];
        si.opcode = (unsigned)op;
        si.byteOffset = i * sizeof(uint32_t);
        lvx_decode_operands(si.opcode, &emi.syllables[i], si.decoded);
        // A BCU (steering 0) slot may redirect the PC; mark the bundle as
        // control so the CPU honors the next-PC that execute() computes.
        if (steering(syll) == 0)
            flags[IsControl] = true;
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
