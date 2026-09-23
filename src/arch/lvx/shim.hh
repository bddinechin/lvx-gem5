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
    int256_t value;
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
// Conditional execution within a bundle.
//
// GUARD occupies a BCU slot and evaluates a predicate; its immediate is a mask
// of the execution units that predicate guards, numbered from the first unit
// after the two BCUs, so bit 0 is ALU0, bit 1 is ALU1, and so on.  The
// assembler builds exactly this mask -- gas tc-lvx.c computes
// `1 << (target_exu - LVX_EXU_ALU0)` for the instruction being predicated,
// and ORs the bits of every syllable guarded on the same condition into one
// GUARD (lvx_cond_insn_merge).
//
// A bundle holds up to two GUARDs, one per BCU slot, on different conditions:
// the compiler schedules a then-side and an else-side instruction together
// and gas puts their guards in BCU0 and BCU1.  So what a bundle keeps is not
// the last predicate seen but the union of the units every false GUARD named;
// a true GUARD contributes nothing.  Each GUARD names its own units, so the
// two masks never overlap and the union is exact.  Until 2026-09-20 this held
// one predicate and one mask, and the second GUARD overwrote the first.
struct BundlePredication
{
    unsigned suppressMask = 0;   // units whose GUARD was false, bit b == ALU0 + b

    // What a MASKS or MASKM of this bundle told each unit, in the same
    // numbering as suppressMask.  A prefix names its units in `activate` and
    // supplies one register of enables; the unit reads them back through
    // maskbytes()/lanemask() when it commits, at ITS OWN granularity -- bytes
    // for an access, lanes for an arithmetic instruction (lvx-mds/docs/
    // Lane-masking-design.md §1.4).  So the enables are kept raw here, with
    // the polarity of `lanetodo` already applied, and the consumer takes the
    // part that is its own.
    static constexpr unsigned MaxUnits = 8;   // `activate` is 8 bits wide
    bool masked[MaxUnits] = {};      // a prefix of this bundle named this unit
    uint64_t enables[MaxUnits] = {}; // its enables, already complemented for .mf
    unsigned slice[MaxUnits] = {};   // under .mtd/.mfd, which slice is this
                                     // unit's: its index among the activated
                                     // units, ascending.  Zero otherwise.

    void
    reset()
    {
        suppressMask = 0;
        for (unsigned i = 0; i < MaxUnits; i++) {
            masked[i] = false;
            enables[i] = 0;
            slice[i] = 0;
        }
    }
};

// Register writes within a bundle.
//
// A bundle may carry two syllables that write the same register under
// disjoint predicates -- the if-converted diamond `cmoved.weqz $r3? $r5 = $r63`
// / `cmoved.wnez $r3? $r5 = $r62`, which the compiler emits and the KVX
// allows.  The description spells a conditional move as an unconditional
// SELECT of the new or the old value (a real read of the destination, so the
// tied operand is an ACCESS -- lvx-mds ac8c58e), so the syllable whose
// predicate is false still commits: it writes the register's own pre-bundle
// value back.  Committing in syllable order would then let that no-op write
// land after the real one and undo it.
//
// This log, shared by the syllables of one bundle like BundlePredication,
// applies the architectural rule at commit: at most one syllable effectively
// writes a register per bundle.  A write of the pre-bundle value is not a
// write and never overrides another; two writes of different new values are
// an illegal bundle and panic, since nothing can order them.
struct BundleWriteLog
{
    static constexpr unsigned MaxEntries = 32;
    struct Entry
    {
        int reg;         // GPR number
        uint64_t old;    // its value before the bundle
        uint64_t value;  // what has been committed so far
    };
    Entry entries[MaxEntries];
    unsigned count = 0;

    void reset() { count = 0; }
};

struct BehaviorContext
{
    ThreadContext *tc = nullptr;

    // Shared with the other syllables of the bundle; owned by the caller.
    BundlePredication *predication = nullptr;
    BundleWriteLog *writeLog = nullptr;   // null outside a bundle: plain writes

    // This syllable's unit in the numbering `activate` uses -- 0 is ALU0, the
    // first unit after the two BCUs -- or ~0u for a BCU slot, which no prefix
    // can name.  GUARD needs the unit only in the driver, since suppression
    // skips a whole syllable; a masked instruction asks for its OWN enables,
    // so the helper it calls has to know who is asking.
    unsigned maskUnit = ~0u;

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
        for (auto &s : operands) { s.value = int256_zero; s.flags = AccessNone; }
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
