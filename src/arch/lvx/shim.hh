/*
 * LVX ISA support for gem5 — Layer B runtime shim (C++ side).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Defines the BehaviorContext (the opaque `this`/Processor handed to the
 * MDS-generated behavior bodies) and the phase driver used to run a decoded
 * LVX instruction against gem5 machine state. The core helpers declared in
 * shim.h are implemented in shim.cc in terms of this context.
 */
#ifndef __ARCH_LVX_SHIM_HH__
#define __ARCH_LVX_SHIM_HH__

#include "arch/lvx/behavior_iface.hh"
#include "arch/lvx/int256.h"
#include "base/types.hh"

namespace gem5
{

class ThreadContext;

namespace LvxISA
{

// Max operand slots addressed by a single instruction's bodies (observed max
// opnd_idx is 5; round up for headroom).
inline constexpr unsigned MaxOperands = 16;

// Operand-slot access flags (our own values; only the WRITE bit is inspected,
// to decide whether a destination slot is committed to its register).
enum OperandFlags : unsigned
{
    AccessNone       = 0,
    AccessRead       = 1u << 0,
    AccessWrite      = 1u << 1,
    AccessUnmodified = 1u << 2,
};

// One operand slot: the staged 256-bit value plus its access flags. Sources are
// loaded here in fetch; the result is staged here in execute; commit stores
// written slots back to registers.
struct OperandSlot
{
    Int256_ value;
    unsigned flags;
};

// The per-instruction execution context. This is what the generated bodies see
// as `void *this`. It is deliberately a plain aggregate so it can live on the
// stack of LvxStaticInst::execute (#9).
//
// VLIW note: register writes take effect in the commit phase. To get bundle
// parallel semantics (#9) run every instruction's FETCH, then every
// instruction's EXECUTE, then every instruction's COMMIT — each instruction
// keeps its own context, so all sources are read before any result is written.
struct BehaviorContext
{
    ThreadContext *tc = nullptr;

    // PC of this instruction (bundle base + this syllable's offset). Read by
    // readFromStorage_PC and used as the fall-through base for nextPC.
    Addr instPC = 0;

    // Staged architectural next PC. Defaults to fall-through (set by the
    // driver); a taken branch overrides it via writeToStorage_NPC.
    Addr nextPC = 0;
    bool npcWritten = false;

    OperandSlot operands[MaxOperands] = {};

    void
    reset(ThreadContext *t, Addr inst_pc, Addr fall_through)
    {
        tc = t;
        instPC = inst_pc;
        nextPC = fall_through;
        npcWritten = false;
        for (auto &s : operands) { s.value = Int256_zero; s.flags = AccessNone; }
    }
};

// Run one behavior phase (fetch/execute/commit) of `opcode` against `ctx`.
// `decoded` holds the instruction's extracted operand fields (filled by the
// Layer C operand decoder, #9). Missing bodies (e.g. pseudo-opcodes) are no-ops.
void runPhase(BehaviorContext &ctx, unsigned opcode, BehaviorPhase phase,
              const OperandDecoded *decoded);

// Convenience: run fetch+execute+commit for a single standalone instruction and
// return the resulting next PC. (Bundles are orchestrated phase-by-phase by #9
// instead of calling this per instruction.)
Addr runInstruction(BehaviorContext &ctx, unsigned opcode,
                    const OperandDecoded *decoded);

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_SHIM_HH__
