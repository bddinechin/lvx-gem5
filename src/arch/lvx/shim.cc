/*
 * LVX ISA support for gem5 — Layer B runtime shim (implementation).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Implements the 17 core helpers (declared C-linkage in shim.h) that the
 * MDS-generated behavior bodies call, mapping the operand-slot / register-file
 * / control-storage model onto a gem5 ThreadContext. Semantics mirror Kalray's
 * ISS helpers_core.h; the system-register privilege-level read/write effects
 * are intentionally dropped (SE-mode user code — marked TODO).
 *
 * Verification: this file links into gem5 and is correct by reference to the
 * KVX helpers; end-to-end execution is exercised once the Layer C operand
 * decoder + LvxStaticInst land (#9) and the hello-world milestone runs (#10).
 */
#include "arch/lvx/shim.hh"

#include "arch/lvx/regs/int.hh"
#include "arch/lvx/regs/misc.hh"
#include "arch/lvx/shim.h"
#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "debug/LvxDecode.hh"

namespace gem5
{
namespace LvxISA
{

// --- small helpers over the gem5 ThreadContext --------------------------------

static inline uint64_t
readGpr(ThreadContext *tc, int id)
{
    return tc->getReg(intRegClass[id]);
}

static inline void
writeGpr(ThreadContext *tc, int id, uint64_t val)
{
    tc->setReg(intRegClass[id], val);
}

static inline uint64_t
readSfr(ThreadContext *tc, unsigned idx)
{
    return tc->readMiscRegNoEffect(idx);
}

static inline void
writeSfr(ThreadContext *tc, unsigned idx, uint64_t val)
{
    tc->setMiscReg(idx, val);
}

// --- phase driver -------------------------------------------------------------

void
runPhase(BehaviorContext &ctx, unsigned opcode, BehaviorPhase phase,
         const OperandDecoded *decoded)
{
    Behavior fn = lvxOpcodeBehavior(opcode, phase);
    if (fn)
        fn(&ctx, const_cast<OperandDecoded *>(decoded), nullptr);
}

Addr
runInstruction(BehaviorContext &ctx, unsigned opcode,
               const OperandDecoded *decoded)
{
    runPhase(ctx, opcode, BehaviorFetch, decoded);
    runPhase(ctx, opcode, BehaviorExecute, decoded);
    runPhase(ctx, opcode, BehaviorCommit, decoded);
    return ctx.nextPC;
}

} // namespace LvxISA
} // namespace gem5

// --- core helpers (C linkage; called from the generated behavior.c) -----------

using namespace gem5;
using namespace gem5::LvxISA;

extern "C" {

Int256_
Behavior_operandRead(void *self, int opnd_idx)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->operands[opnd_idx].flags |= AccessRead;
    return ctx->operands[opnd_idx].value;
}

void
Behavior_operandFromValue(void *self, int /*rank*/, int opnd_idx,
                          uint64_t mask, Int256_ value)
{
    // The generated LVX bodies only ever pass mask == 0 (full write); a partial
    // (masked) blend would go here if that ever changes.
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    (void)mask;
    ctx->operands[opnd_idx].value = value;
    ctx->operands[opnd_idx].flags = AccessWrite;
}

void
Behavior_operandFromRegFile_GPR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->operands[opnd_idx].value = Int256_fromUInt64(readGpr(ctx->tc, register_id));
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandFromRegFile_PGR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    register_id *= 2;
    ctx->operands[opnd_idx].value = Int256_make(
        readGpr(ctx->tc, register_id + 0), readGpr(ctx->tc, register_id + 1), 0, 0);
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandFromRegFile_QGR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    register_id *= 4;
    ctx->operands[opnd_idx].value = Int256_make(
        readGpr(ctx->tc, register_id + 0), readGpr(ctx->tc, register_id + 1),
        readGpr(ctx->tc, register_id + 2), readGpr(ctx->tc, register_id + 3));
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandFromRegFile_SFR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->operands[opnd_idx].value = Int256_fromUInt64(readSfr(ctx->tc, register_id));
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandToRegFile_GPR(void *self, unsigned /*stage*/, int /*rank*/,
                              int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    if (!(ctx->operands[opnd_idx].flags & AccessWrite))
        return; // not written by execute: nothing to commit
    writeGpr(ctx->tc, register_id, ctx->operands[opnd_idx].value.dwords[0]);
}

void
Behavior_operandToRegFile_PGR(void *self, unsigned /*stage*/, int /*rank*/,
                              int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    if (!(ctx->operands[opnd_idx].flags & AccessWrite))
        return;
    register_id *= 2;
    writeGpr(ctx->tc, register_id + 0, ctx->operands[opnd_idx].value.dwords[0]);
    writeGpr(ctx->tc, register_id + 1, ctx->operands[opnd_idx].value.dwords[1]);
}

void
Behavior_operandToRegFile_QGR(void *self, unsigned /*stage*/, int /*rank*/,
                              int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    if (!(ctx->operands[opnd_idx].flags & AccessWrite))
        return;
    register_id *= 4;
    for (int i = 0; i < 4; i++)
        writeGpr(ctx->tc, register_id + i, ctx->operands[opnd_idx].value.dwords[i]);
}

void
Behavior_operandToRegFile_SFR(void *self, unsigned /*stage*/, int /*rank*/,
                              int opnd_idx, int register_id)
{
    // TODO(#8+): system-register privilege-level write effects (register_write_
    // effect / reg->write) are dropped for SE mode.
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    if (!(ctx->operands[opnd_idx].flags & AccessWrite))
        return;
    writeSfr(ctx->tc, register_id, ctx->operands[opnd_idx].value.dwords[0]);
}

void
Behavior_commitRegFiles(void * /*self*/)
{
    // No-op: register/storage writes are applied immediately by the
    // operandToRegFile_* / writeToStorage_* helpers (we do not use a deferred
    // commit queue). Bundle-level parallelism is provided by running all
    // instructions' fetch, then execute, then commit phases (#9), not here.
}

Int256_
Behavior_readFromStorage_PC(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned /*size*/)
{
    // Only the whole PC is addressable.
    assert(extent <= 1 && offset == 0);
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    return Int256_fromUInt64(ctx->instPC);
}

Int256_
Behavior_readFromStorage_NPC(void *self, unsigned /*stage*/, unsigned offset,
                             unsigned extent, unsigned /*size*/)
{
    assert(extent <= 1 && offset == 0);
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    return Int256_fromUInt64(ctx->nextPC);
}

Int256_
Behavior_readFromStorage_PS(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned /*size*/)
{
    // Processor Status bitfield read. SE mode: status bits are not modeled;
    // return 0. TODO(#8+): back PS with a misc reg if any user code reads it.
    (void)self; (void)offset; (void)extent;
    return Int256_zero;
}

Int256_
Behavior_readFromStorage_SFR(void *self, unsigned /*stage*/, unsigned offset,
                             unsigned extent, unsigned size)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    assert((extent * size) < 128);
    uint64_t v = 0;
    for (unsigned i = 0; i < extent; ++i)
        v = (v << (size * i)) | readSfr(ctx->tc, offset + i);
    return Int256_fromUInt64(v);
}

void
Behavior_writeToStorage_NPC(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned /*size*/, Int256_ value)
{
    assert(extent <= 1 && offset == 0);
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->nextPC = Int256_toUInt64(value) & ~UINT64_C(0x3); // clear low 2 bits
    ctx->npcWritten = true;
}

void
Behavior_writeToStorage_SFR(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned size, Int256_ value)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    uint64_t v = Int256_toUInt64(value);
    unsigned bits = size * extent;
    if (bits < 64)
        v &= (UINT64_C(1) << bits) - 1;
    writeSfr(ctx->tc, offset, v);
}

} // extern "C"
