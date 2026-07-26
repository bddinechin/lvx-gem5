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

#include <unistd.h>

#include <cstring>
#include <vector>

#include "arch/lvx/regs/int.hh"
#include "arch/lvx/regs/misc.hh"
#include "arch/lvx/shim.h"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/thread_context.hh"
#include "debug/LvxDecode.hh"
#include "mem/se_translating_port_proxy.hh"
#include "sim/sim_exit.hh"

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

int256_t
Behavior_operandRead(void *self, int opnd_idx)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->operands[opnd_idx].flags |= AccessRead;
    return ctx->operands[opnd_idx].value;
}

void
Behavior_operandFromValue(void *self, int /*rank*/, int opnd_idx,
                          uint64_t mask, int256_t value)
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
    ctx->operands[opnd_idx].value = int256_fromUInt64(readGpr(ctx->tc, register_id));
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandFromRegFile_PGR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    register_id *= 2;
    ctx->operands[opnd_idx].value = int256_make(
        readGpr(ctx->tc, register_id + 0), readGpr(ctx->tc, register_id + 1), 0, 0);
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandFromRegFile_QGR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    register_id *= 4;
    ctx->operands[opnd_idx].value = int256_make(
        readGpr(ctx->tc, register_id + 0), readGpr(ctx->tc, register_id + 1),
        readGpr(ctx->tc, register_id + 2), readGpr(ctx->tc, register_id + 3));
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandFromRegFile_SFR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->operands[opnd_idx].value = int256_fromUInt64(readSfr(ctx->tc, register_id));
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

int256_t
Behavior_readFromStorage_PC(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned /*size*/)
{
    // Only the whole PC is addressable.
    assert(extent <= 1 && offset == 0);
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    return int256_fromUInt64(ctx->instPC);
}

int256_t
Behavior_readFromStorage_NPC(void *self, unsigned /*stage*/, unsigned offset,
                             unsigned extent, unsigned /*size*/)
{
    assert(extent <= 1 && offset == 0);
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    return int256_fromUInt64(ctx->nextPC);
}

int256_t
Behavior_readFromStorage_PS(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned size)
{
    // Processor Status bitfield read: `offset` is a *bit* position within PS and
    // (size * extent) the field width. SE mode models a single live bit, PS.HLE
    // (bit 5), so hardware loops are enabled and LOOPDO does not trap; every
    // other field reads as 0. See misc_reg::ps. TODO(#8+): back PS with a misc
    // reg once privileged code observes more of it.
    (void)self;
    unsigned width = size * extent;
    uint64_t mask = width >= 64 ? ~uint64_t{0} : ((uint64_t{1} << width) - 1);
    uint64_t field = (misc_reg::ps::SE_MODE_VALUE >> offset) & mask;
    return int256_fromUInt64(field);
}

// Compute Status (CS) — per-EXU status/mode bits (e.g. CS.XMF, set by the
// register-buffer instructions). SE-mode user code observes none of it: reads
// return 0 and writes are dropped, mirroring PS above.
int256_t
Behavior_readFromStorage_CS(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned /*size*/)
{
    (void)self; (void)offset; (void)extent;
    return int256_zero;
}

void
Behavior_writeToStorage_CS(void *self, unsigned /*stage*/, unsigned /*offset*/,
                           unsigned /*extent*/, unsigned /*size*/, int256_t /*value*/)
{
    (void)self;
}

int256_t
Behavior_readFromStorage_SFR(void *self, unsigned /*stage*/, unsigned offset,
                             unsigned extent, unsigned size)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    assert((extent * size) < 128);
    uint64_t v = 0;
    for (unsigned i = 0; i < extent; ++i)
        v = (v << (size * i)) | readSfr(ctx->tc, offset + i);
    return int256_fromUInt64(v);
}

void
Behavior_writeToStorage_NPC(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned /*size*/, int256_t value)
{
    assert(extent <= 1 && offset == 0);
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->nextPC = int256_toUInt64(value) & ~UINT64_C(0x3); // clear low 2 bits
    ctx->npcWritten = true;
}

void
Behavior_writeToStorage_SFR(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned size, int256_t value)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    uint64_t v = int256_toUInt64(value);
    unsigned bits = size * extent;
    if (bits < 64)
        v &= (UINT64_C(1) << bits) - 1;
    writeSfr(ctx->tc, offset, v);
}

// SRS is the unified system-register storage introduced by the SFR->SRS
// refactor; it shares the SFR numbering (PS=1, CS=4, LS=7, LE=8, LC=9, ...) and
// therefore maps onto the same misc-reg file as the _SFR helpers above. The one
// SE-mode exception is PS (SRS 1): it reports PS.HLE set (see misc_reg::ps) so
// hardware loops are enabled and LOOPDO does not throw. Everything else --
// notably CS / FP status (SRS 4) -- reads its backing store, which defaults to
// 0, matching the pre-refactor readFromStorage_CS behavior.
int256_t
Behavior_readFromStorage_SRS(void *self, unsigned /*stage*/, unsigned offset,
                             unsigned extent, unsigned size)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    assert((extent * size) < 128);
    uint64_t v = 0;
    for (unsigned i = 0; i < extent; ++i) {
        uint64_t reg = readSfr(ctx->tc, offset + i);
        if (offset + i == misc_reg::PS)
            reg |= misc_reg::ps::SE_MODE_VALUE; // force PS.HLE in SE mode
        v = (v << (size * i)) | reg;
    }
    return int256_fromUInt64(v);
}

void
Behavior_writeToStorage_SRS(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned size, int256_t value)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    uint64_t v = int256_toUInt64(value);
    unsigned bits = size * extent;
    if (bits < 64)
        v &= (UINT64_C(1) << bits) - 1;
    writeSfr(ctx->tc, offset, v);
}

// Access byte count from the load/store byte-mask (mirrors Kalray common_load).
static unsigned
lvxAccessSize(uint32_t byteMask)
{
    if (byteMask & 0xffff0000u) return 32;
    if (byteMask & 0x0000ff00u) return 16;
    if (byteMask & 0x000000f0u) return 8;
    if (byteMask & 0x0000000cu) return 4;
    if (byteMask & 0x00000002u) return 2;
    return 1;
}

int256_t
Behavior_MEM_load(void *self, uint64_t addr, int256_t byteMask,
                  uint8_t /*modifier*/, uint8_t /*dri*/)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    Addr address = (Addr)addr;
    unsigned size = lvxAccessSize(byteMask.words[0]);
    int256_t result = int256_zero;
    // Functional SE-mode read (AtomicSimpleCPU). TODO: route through the CPU
    // memory system (ExecContext::readMem) for timing models.
    SETranslatingPortProxy proxy(ctx->tc);
    proxy.readBlob(address, result.bytes, size);
    return result;
}

void
Behavior_MEM_store(void *self, uint64_t addr, int256_t byteMask,
                   uint8_t /*modifier*/, int256_t value, uint8_t /*dri*/)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    Addr address = (Addr)addr;
    unsigned size = lvxAccessSize(byteMask.words[0]);
    SETranslatingPortProxy proxy(ctx->tc);
    proxy.writeBlob(address, value.bytes, size);
}

// Minimal SE-mode system-call handling. The kv4-v1 ABI passes arguments in
// r0..r7 and returns in r0; the syscall number is the scall operand. This is a
// milestone-scoped subset (exit, write) done inline rather than through gem5's
// SyscallDesc machinery — TODO(#10+): route to a proper LvxISA EmuLinux table.
void
Behavior_syscall(void *self, uint64_t number)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ThreadContext *tc = ctx->tc;
    uint64_t n = number;
    // kv4-v1 argument registers r0..r7.
    auto arg = [&](int i) { return (uint64_t)tc->getReg(intRegClass[i]); };

    switch (n) {
      case 1: { // __NR_exit
        exitSimLoop("target exited", (int)arg(0));
        break;
      }
      case 17: { // __NR_write(fd, buf, count)
        int fd = (int)arg(0);
        Addr buf = (Addr)arg(1);
        uint64_t count = arg(2);
        std::vector<uint8_t> data(count);
        SETranslatingPortProxy proxy(tc);
        if (count)
            proxy.readBlob(buf, data.data(), count);
        ssize_t ret = ::write(fd == 1 || fd == 2 ? fd : 1, data.data(), count);
        tc->setReg(intRegClass[0], (uint64_t)ret);
        break;
      }
      default:
        warn("LVX: unhandled scall #%llu (ignored)\n", (unsigned long long)n);
        tc->setReg(intRegClass[0], (uint64_t)-1);
        break;
    }
}

void
Behavior_branch_info(void * /*self*/, uint8_t /*a*/, uint64_t /*b*/)
{
    // Branch-prediction hint from control-flow instructions; no effect here.
}

void
Behavior_invalpfb(void * /*self*/)
{
    // LOOPDO's hardware-loop prefetch-buffer invalidate. The ISS has no such
    // buffer (the loop-back is driven from LS/LE/LC in static_inst.cc), so this
    // is a functional no-op.
}

// Conditional-branch / conditional-move predicate (CB/CBX/CMOVE...).  opnd1 is
// the 4-bit `bcucond` modifier code, opnd2 the register value it tests against
// zero.  Semantics mirror ../epi-csw/lao/LAO/kvx/Behavior.c:Behavior_bcucond_;
// the code order is the Modifier.yml `bcucond` member order (D* are 64-bit
// tests, W* are 32-bit tests of the low word).
bool
Behavior_bcucond(void * /*self*/, uint8_t opnd1, uint64_t opnd2)
{
    int64_t v = (int64_t)opnd2;
    switch (opnd1) {
      case  0: return v <  0;            // DLTZ
      case  1: return v >= 0;            // DGEZ
      case  2: return v <= 0;            // DLEZ
      case  3: return v >  0;            // DGTZ
      case  4: return v == 0;            // DEQZ
      case  5: return v != 0;            // DNEZ
      case  6: return (v & 1) != 0;      // ODD
      case  7: return (v & 1) == 0;      // EVEN
      case  8: return (int32_t)v <  0;   // WLTZ
      case  9: return (int32_t)v >= 0;   // WGEZ
      case 10: return (int32_t)v <= 0;   // WLEZ
      case 11: return (int32_t)v >  0;   // WGTZ
      case 12: return (int32_t)v == 0;   // WEQZ
      case 13: return (int32_t)v != 0;   // WNEZ
      default:
        panic("LVX: unknown bcucond code %d", (int)opnd1);
    }
}

// SRHPC is a privilege-level saved-PC register updated on return; it has no
// effect on SE-mode user execution (cf. branch_info).
void
Behavior_srhpc_update(void * /*self*/)
{
}

// System-register (SFR) access permission checks.  The full model gates these
// on the current privilege level (../epi-csw/iss_core/.../helpers_core.h); in
// SE-mode user execution there is no privilege model, so every access the
// program makes is permitted.  GET/SET of $ra in every function prologue/
// epilogue go through get_check_access / set_check_access.
bool Behavior_get_check_access (void *, uint16_t, uint8_t)            { return true; }
bool Behavior_set_check_access (void *, uint16_t, uint64_t, uint8_t) { return true; }
bool Behavior_wfxl_check_access(void *, uint16_t, uint8_t)           { return true; }
bool Behavior_wfxm_check_access(void *, uint16_t, uint8_t)           { return true; }

// GET reads an SFR: the value is already loaded from the SFR file by the
// behavior (readFromStorage_SFR); `get` returns it, with no per-bit privilege
// masking or clear-on-read side effects in SE mode.  opnd2 is that value.
int256_t
Behavior_get(void * /*self*/, uint16_t /*sfr*/, uint64_t value)
{
    return int256_fromUInt64(value);
}

// Integer comparison (COMP*).  opnd1 is the `intcomp` modifier code, opnd2/opnd3
// the two values.  Semantics mirror ../epi-csw/lao/LAO/kvx/Behavior.c:
// Behavior_intcomp_NN_; the code order is the Modifier.yml `intcomp` member
// order.  Signed vs unsigned per code; width per the _32/_64 entry point.
static inline bool
lvxIntcomp(int code, int64_t sa, int64_t sb, uint64_t ua, uint64_t ub)
{
    switch (code) {
      case  0: return sa <  sb;             // LT
      case  1: return sa >= sb;             // GE
      case  2: return ua <  ub;             // LTU
      case  3: return ua >= ub;             // GEU
      case  4: return sa == sb;             // EQ
      case  5: return sa != sb;             // NE
      case  6: return (ua & ub) != 0;       // ANY
      case  7: return (ua & ub) == 0;       // NONE
      case  8: return sa <= sb;             // LE
      case  9: return sa >  sb;             // GT
      case 10: return ua <= ub;             // LEU
      case 11: return ua >  ub;             // GTU
      default: panic("LVX: unknown intcomp code %d", code);
    }
}

bool
Behavior_intcomp_64(void * /*self*/, uint8_t code, uint64_t a, uint64_t b)
{
    return lvxIntcomp(code, (int64_t)a, (int64_t)b, a, b);
}

bool
Behavior_intcomp_32(void * /*self*/, uint8_t code, uint64_t a, uint64_t b)
{
    return lvxIntcomp(code, (int32_t)a, (int32_t)b, (uint32_t)a, (uint32_t)b);
}

} // extern "C"
