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

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "arch/lvx/regs/int.hh"
#include "arch/lvx/regs/misc.hh"
#include "arch/lvx/regs/vec.hh"
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

// --- LVX vector file (XVR/XBR/XCR), all views of the XRS 64-bit cells ----------
// XVR reg i is XRS cells [4i..4i+3] = one 256-bit VecRegContainer; XBR i is cells
// [2i..2i+1]; XCR i is cell [i]. Lane order is little-endian (cell 4i == dword 0).

// Read/write a whole 256-bit XVR register (index i, 0..63).
static inline int256_t
readXvr(ThreadContext *tc, int i)
{
    VecRegContainer c;
    tc->getReg(vecRegClass[i], &c);
    const uint64_t *l = c.as<uint64_t>();
    int256_t v;
    for (int k = 0; k < 4; k++) v.dwords[k] = l[k];
    return v;
}

static inline void
writeXvr(ThreadContext *tc, int i, int256_t v)
{
    VecRegContainer c;
    uint64_t *l = c.as<uint64_t>();
    for (int k = 0; k < 4; k++) l[k] = v.dwords[k];
    tc->setReg(vecRegClass[i], &c);
}

// Read/write a single 64-bit XRS cell (the XCR granularity), addressing the
// containing XVR register and its lane. Writes are read-modify-write so the
// other lanes of the container are preserved.
static inline uint64_t
readXrsCell(ThreadContext *tc, unsigned cell)
{
    VecRegContainer c;
    tc->getReg(vecRegClass[cell / vec_reg::LanesPerReg], &c);
    return c.as<uint64_t>()[cell % vec_reg::LanesPerReg];
}

static inline void
writeXrsCell(ThreadContext *tc, unsigned cell, uint64_t val)
{
    VecRegContainer c;
    tc->getReg(vecRegClass[cell / vec_reg::LanesPerReg], &c);
    c.as<uint64_t>()[cell % vec_reg::LanesPerReg] = val;
    tc->setReg(vecRegClass[cell / vec_reg::LanesPerReg], &c);
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

// --- lvx_v2 vector file access (XVR/XBR/XCR) -----------------------------------
// These load an operand slot from / commit it to the LVX vector file, mirroring
// the GPR/PGR/QGR helpers above: register_id is the file-relative index (the
// behavior bodies already subtracted the file base). XVR is 256-bit (4 XRS
// cells), XBR 128-bit (2 cells), XCR 64-bit (1 cell); lane order little-endian.

void
Behavior_operandFromRegFile_XVR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->operands[opnd_idx].value = readXvr(ctx->tc, register_id);
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandFromRegFile_XBR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    unsigned cell = 2u * register_id;
    ctx->operands[opnd_idx].value = int256_make(
        readXrsCell(ctx->tc, cell), readXrsCell(ctx->tc, cell + 1), 0, 0);
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandFromRegFile_XCR(void *self, unsigned /*stage*/, int /*rank*/,
                                int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ctx->operands[opnd_idx].value =
        int256_fromUInt64(readXrsCell(ctx->tc, register_id));
    ctx->operands[opnd_idx].flags = AccessNone;
}

void
Behavior_operandToRegFile_XVR(void *self, unsigned /*stage*/, int /*rank*/,
                              int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    if (!(ctx->operands[opnd_idx].flags & AccessWrite))
        return;
    writeXvr(ctx->tc, register_id, ctx->operands[opnd_idx].value);
}

void
Behavior_operandToRegFile_XBR(void *self, unsigned /*stage*/, int /*rank*/,
                              int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    if (!(ctx->operands[opnd_idx].flags & AccessWrite))
        return;
    unsigned cell = 2u * register_id;
    writeXrsCell(ctx->tc, cell,     ctx->operands[opnd_idx].value.dwords[0]);
    writeXrsCell(ctx->tc, cell + 1, ctx->operands[opnd_idx].value.dwords[1]);
}

void
Behavior_operandToRegFile_XCR(void *self, unsigned /*stage*/, int /*rank*/,
                              int opnd_idx, int register_id)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    if (!(ctx->operands[opnd_idx].flags & AccessWrite))
        return;
    writeXrsCell(ctx->tc, register_id, ctx->operands[opnd_idx].value.dwords[0]);
}

// Run-time-indexed XRS access (the xlo/xso qindex forms, XPL* byte-lane ops):
// `offset` is an XRS cell index, and the access spans (extent*size) bits ==
// (extent*size)/64 cells from there, little-endian into the int256_t limbs.
int256_t
Behavior_readFromStorage_XVR(void *self, unsigned /*stage*/, unsigned offset,
                             unsigned extent, unsigned size)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    unsigned ncells = (extent * size + 63) / 64;
    int256_t v = int256_zero;
    for (unsigned i = 0; i < ncells && i < 4; i++)
        v.dwords[i] = readXrsCell(ctx->tc, offset + i);
    return v;
}

void
Behavior_writeToStorage_XVR(void *self, unsigned /*stage*/, unsigned offset,
                            unsigned extent, unsigned size, int256_t value)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    unsigned ncells = (extent * size + 63) / 64;
    for (unsigned i = 0; i < ncells && i < 4; i++)
        writeXrsCell(ctx->tc, offset + i, value.dwords[i]);
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

// --- 8x8 bit-matrix operators -------------------------------------------------
//
// The SBMM8* / SBMT8* family treats a 64-bit value as an 8x8 matrix of bits and
// multiplies or transposes it over GF(2), where "multiply" is AND and "add" is
// XOR.  Bit i*8+j of the matrix is row i, column j.
//
// Small, but not optional: newlib's memset uses sbmm8d to splat a byte across a
// word, and crt0 reaches memset through _REENT_INIT_PTR, so until these existed
// every hosted program died in startup on the panic stub.

// c[i] = XOR over the j where row i of A has a bit set, of b[j].  That is, the
// bits of A select which bytes of B are XORed into each byte of the result.
static inline uint64_t
lvxBitMatrixMultiply8 (uint64_t a, uint64_t b)
{
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            if (a & 0x1)
                c ^= ((b >> (j * 8)) & 0xFF) << (i * 8);
            a >>= 1;
        }
    return c;
}

// Reflect the matrix about its diagonal: bit i*8+j moves to bit j*8+i.
static inline uint64_t
lvxBitMatrixTranspose8 (uint64_t a)
{
    uint64_t b = 0;
    for (int i = 0; i < 8; i++)
        for (int j = 0; j < 8; j++) {
            if (a & 0x1)
                b ^= UINT64_C(1) << ((j * 8) + i);
            a >>= 1;
        }
    return b;
}

int256_t
Behavior__BMM_8(void * /*self*/, uint64_t opnd1, uint64_t opnd2)
{
    return int256_fromUInt64(lvxBitMatrixMultiply8(opnd1, opnd2));
}

int256_t
Behavior__BMT_8(void * /*self*/, uint64_t opnd1)
{
    return int256_fromUInt64(lvxBitMatrixTranspose8(opnd1));
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

// --- SE-mode system calls -----------------------------------------------------
//
// The syscall numbers and the open()-flag encoding are the target's, defined by
// lvx-newlib's libgloss in
//   newlib/libc/sys/mbr/include/mbr/lvx/scall_no.h
// and issued by libgloss/lvx-mbr/asm_syscalls.S.  Keep the two in sync: the
// numbering is deliberately kv4-v1's, and that header says so.
//
// Calling convention: arguments in r0..r7, result in r0, syscall number in the
// scall operand.  Errors are reported the way libgloss expects -- the negated
// errno as the result -- because every wrapper does
//   if (ret < 0) { errno = ret * -1; ret = -1; }
// Returning a bare -1 would set errno to 1 (EPERM) for every failure.

// Target open()/fcntl() flags (the S_* set in scall_no.h).  These are the
// target's own encoding, not the host's, and libgloss has already translated
// from newlib's O_* into them before the scall -- so translate back here.
enum LvxOpenFlags
{
    LVX_S_RDONLY   = 0x001,
    LVX_S_WRONLY   = 0x002,
    LVX_S_RDWR     = 0x004,
    LVX_S_APPEND   = 0x008,
    LVX_S_CREAT    = 0x010,
    LVX_S_TRUNC    = 0x020,
    LVX_S_EXCL     = 0x040,
    LVX_S_SYNC     = 0x080,
    LVX_S_NDELAY   = 0x100,
    LVX_S_NONBLOCK = 0x200,
    LVX_S_NOCTTY   = 0x400,
};

static int
lvxToHostOpenFlags(uint64_t f)
{
    // O_RDONLY is 0 on the host, so the access mode has to be decided rather
    // than or-ed together: RDWR wins, then WRONLY, then RDONLY.
    int hf = (f & LVX_S_RDWR)   ? O_RDWR
           : (f & LVX_S_WRONLY) ? O_WRONLY
                                : O_RDONLY;
    if (f & LVX_S_APPEND)   hf |= O_APPEND;
    if (f & LVX_S_CREAT)    hf |= O_CREAT;
    if (f & LVX_S_TRUNC)    hf |= O_TRUNC;
    if (f & LVX_S_EXCL)     hf |= O_EXCL;
    if (f & LVX_S_SYNC)     hf |= O_SYNC;
    if (f & LVX_S_NDELAY)   hf |= O_NDELAY;
    if (f & LVX_S_NONBLOCK) hf |= O_NONBLOCK;
    if (f & LVX_S_NOCTTY)   hf |= O_NOCTTY;
    return hf;
}

// Result of a host call, as libgloss wants to see it.
static int64_t
sysResult(int64_t hostRet)
{
    return hostRet < 0 ? -(int64_t)errno : hostRet;
}

static std::string
readTargetString(ThreadContext *tc, Addr addr)
{
    SETranslatingPortProxy proxy(tc);
    std::string s;
    proxy.readString(s, addr);
    return s;
}

// libgloss's stat/fstat/lstat wrappers do not pass a struct: they pass a
// uint64_t[13] and unpack it themselves (see libgloss/lvx-mbr/fstat.c), which
// keeps the target's struct stat layout out of the ISS entirely.
static void
writeStatArray(ThreadContext *tc, Addr addr, const struct stat &st)
{
    uint64_t r[13];
    r[0]  = st.st_dev;
    r[1]  = st.st_ino;
    r[2]  = st.st_mode;
    r[3]  = st.st_nlink;
    r[4]  = st.st_uid;
    r[5]  = st.st_gid;
    r[6]  = st.st_rdev;
    r[7]  = st.st_size;
    r[8]  = st.st_blksize;
    r[9]  = st.st_blocks;
    r[10] = st.st_atime;
    r[11] = st.st_mtime;
    r[12] = st.st_ctime;
    SETranslatingPortProxy proxy(tc);
    proxy.writeBlob(addr, (const uint8_t *)r, sizeof(r));
}

void
Behavior_syscall(void *self, uint64_t number)
{
    BehaviorContext *ctx = static_cast<BehaviorContext *>(self);
    ThreadContext *tc = ctx->tc;
    uint64_t n = number;
    // kv4-v1 argument registers r0..r7.
    auto arg = [&](int i) { return (uint64_t)tc->getReg(intRegClass[i]); };
    auto ret = [&](int64_t v) { tc->setReg(intRegClass[0], (uint64_t)v); };

    switch (n) {
      case 1: { // __NR_exit
        exitSimLoop("target exited", (int)arg(0));
        break;
      }

      // --- file descriptors ---
      case 4: { // __NR_close(fd)
        ret(sysResult(::close((int)arg(0))));
        break;
      }
      case 9: { // __NR_lseek(fd, offset, whence)
        ret(sysResult(::lseek((int)arg(0), (off_t)arg(1), (int)arg(2))));
        break;
      }
      case 10: { // __NR_open(path, flags, mode)
        std::string path = readTargetString(tc, (Addr)arg(0));
        ret(sysResult(::open(path.c_str(), lvxToHostOpenFlags(arg(1)),
                             (mode_t)arg(2))));
        break;
      }
      case 11: { // __NR_read(fd, buf, count)
        uint64_t count = arg(2);
        std::vector<uint8_t> data(count);
        ssize_t r = ::read((int)arg(0), data.data(), count);
        if (r > 0) {
            SETranslatingPortProxy proxy(tc);
            proxy.writeBlob((Addr)arg(1), data.data(), r);
        }
        ret(sysResult(r));
        break;
      }
      case 17: { // __NR_write(fd, buf, count)
        int fd = (int)arg(0);
        uint64_t count = arg(2);
        std::vector<uint8_t> data(count);
        if (count) {
            SETranslatingPortProxy proxy(tc);
            proxy.readBlob((Addr)arg(1), data.data(), count);
        }
        ret(sysResult(::write(fd, data.data(), count)));
        break;
      }
      case 19: { // __NR_isatty(fd)
        // Never fails from libgloss's point of view: 0 just means "not a tty".
        ret(::isatty((int)arg(0)));
        break;
      }
      case 28: { // __NR_dup(fd)
        ret(sysResult(::dup((int)arg(0))));
        break;
      }
      case 29: { // __NR_dup2(oldfd, newfd)
        ret(sysResult(::dup2((int)arg(0), (int)arg(1))));
        break;
      }
      case 48: { // __NR_fcntl(fd, cmd, arg)
        // F_SETFL/F_GETFL carry the target's S_* flag encoding; the rest of
        // the commands libgloss issues take a plain integer.
        int cmd = (int)arg(1);
        long a = (long)arg(2);
        if (cmd == F_SETFL)
            a = lvxToHostOpenFlags((uint64_t)a);
        ret(sysResult(::fcntl((int)arg(0), cmd, a)));
        break;
      }

      // --- stat family: results go back as uint64_t[13] ---
      case 6: { // __NR_fstat(fd, res)
        struct stat st;
        int r = ::fstat((int)arg(0), &st);
        if (r == 0)
            writeStatArray(tc, (Addr)arg(1), st);
        ret(sysResult(r));
        break;
      }
      case 14: { // __NR_stat(path, res)
        std::string path = readTargetString(tc, (Addr)arg(0));
        struct stat st;
        int r = ::stat(path.c_str(), &st);
        if (r == 0)
            writeStatArray(tc, (Addr)arg(1), st);
        ret(sysResult(r));
        break;
      }
      case 107: { // __NR_lstat(path, res)
        std::string path = readTargetString(tc, (Addr)arg(0));
        struct stat st;
        int r = ::lstat(path.c_str(), &st);
        if (r == 0)
            writeStatArray(tc, (Addr)arg(1), st);
        ret(sysResult(r));
        break;
      }

      // --- name space ---
      case 7: { // __NR_link(existing, new)
        std::string oldp = readTargetString(tc, (Addr)arg(0));
        std::string newp = readTargetString(tc, (Addr)arg(1));
        ret(sysResult(::link(oldp.c_str(), newp.c_str())));
        break;
      }
      case 8: { // __NR_unlink(path)
        std::string path = readTargetString(tc, (Addr)arg(0));
        ret(sysResult(::unlink(path.c_str())));
        break;
      }
      case 18: { // __NR_chmod(path, mode)
        std::string path = readTargetString(tc, (Addr)arg(0));
        ret(sysResult(::chmod(path.c_str(), (mode_t)arg(1))));
        break;
      }
      case 39: { // __NR_mkdir(path, mode)
        std::string path = readTargetString(tc, (Addr)arg(0));
        ret(sysResult(::mkdir(path.c_str(), (mode_t)arg(1))));
        break;
      }
      case 40: { // __NR_rmdir(path)
        std::string path = readTargetString(tc, (Addr)arg(0));
        ret(sysResult(::rmdir(path.c_str())));
        break;
      }
      case 52: { // __NR_access(path, mode)
        std::string path = readTargetString(tc, (Addr)arg(0));
        ret(sysResult(::access(path.c_str(), (int)arg(1))));
        break;
      }
      case 54: { // __NR_chdir(path)
        std::string path = readTargetString(tc, (Addr)arg(0));
        ret(sysResult(::chdir(path.c_str())));
        break;
      }
      case 0xfe9: { // __NR_iss_mkfifo(path, mode)
        std::string path = readTargetString(tc, (Addr)arg(0));
        ret(sysResult(::mkfifo(path.c_str(), (mode_t)arg(1))));
        break;
      }

      // --- time ---
      case 16: { // __NR_gettimeofday(tv, tz)
        // libgloss's own _gettimeofday reads the cluster timestamp SFR instead
        // of calling this, but asm_syscalls.S still exports sc_gettimeofday.
        struct timeval tv;
        int r = ::gettimeofday(&tv, nullptr);
        if (r == 0 && arg(0)) {
            uint64_t out[2] = { (uint64_t)tv.tv_sec, (uint64_t)tv.tv_usec };
            SETranslatingPortProxy proxy(tc);
            proxy.writeBlob((Addr)arg(0), (const uint8_t *)out, sizeof(out));
        }
        ret(sysResult(r));
        break;
      }

      default:
        warn("LVX: unhandled scall #%llu (returning -ENOSYS)\n",
             (unsigned long long)n);
        ret(-ENOSYS);
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

// CCB fused compare-and-branch (Modifier.yml `ccbcomp`): a single 4-bit field
// packs both the relation (same LT/GE/LTU/GEU/EQ/NE/ANY/NONE order as
// intcomp's first 8 codes) and the operand width (codes 0-7 = double/64-bit,
// 8-15 = word/32-bit, i.e. code & 8 selects width, code & 7 selects relation).
bool
Behavior_ccbcomp(void * /*self*/, uint8_t code, uint64_t a, uint64_t b)
{
    if (code & 8)
        return lvxIntcomp(code & 7, (int32_t)a, (int32_t)b, (uint32_t)a, (uint32_t)b);
    return lvxIntcomp(code & 7, (int64_t)a, (int64_t)b, a, b);
}

} // extern "C"
