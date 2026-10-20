/*
 * LVX ISA support for gem5 — LvxStaticInst (Layer C).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * One LvxStaticInst is a whole VLIW bundle, and it is a gem5 *macroop*: its
 * syllables are micro-ops (LvxMicroInst), one per issue slot used, which is
 * what lets a memory syllable be an isMemRef() of its own and go through the
 * load/store queue. gem5 executes micro-ops in sequence, so the bundle's
 * parallel semantics come from a snapshot: the first micro-op runs every
 * syllable's fetch phase (all the register reads) into the thread's BundleExec,
 * and each micro-op then executes and commits out of that -- so a write by one
 * syllable is invisible to the next. The last micro-op does the bundle-level
 * work: the next PC, the hardware-loop back edge, the redirect flags.
 *
 * The micro-op order is not slot order, and three of its properties are
 * load-bearing; see buildMicroOrder() in the implementation.
 */
#ifndef __ARCH_LVX_STATIC_INST_HH__
#define __ARCH_LVX_STATIC_INST_HH__

#include <vector>

#include "arch/lvx/shim.hh"
#include "arch/lvx/types.hh"
#include "mem/packet.hh"
#include "cpu/reg_class.hh"
#include "cpu/static_inst.hh"

namespace gem5
{
namespace LvxISA
{

// Max operand fields any single LVX instruction decodes into (observed <= ~6).
inline constexpr unsigned MaxOperandsPerInst = 12;

// A bundle's aggregated source/destination register lists for the timing CPUs'
// dependency graph. The bounds are the worst case rather than an estimate: one
// instruction names at most MaxInstSrcRegs/MaxInstDestRegs registers once its
// PGR/QGR tuple operands are expanded into their member GPRs (6 and 4 today,
// BSELQ and COPYO), and a bundle issues at most MaxBundleIssue of them, so a
// bundle cannot exceed the product. Dedup usually keeps the real count far
// below it; overflowing would silently drop a dependency, which understates
// the cycle count, so setUpRegs panics instead (see there).
//
// lvx_v2's register-buffer operands (up to 64 XVR regs for buffer64Reg) are not
// tracked yet and will raise MaxInstSrcRegs when they reach this port.
inline constexpr unsigned MaxInstSrcRegs = 6;
inline constexpr unsigned MaxInstDestRegs = 4;
inline constexpr unsigned MaxBundleSrcRegs = MaxBundleIssue * MaxInstSrcRegs;
inline constexpr unsigned MaxBundleDestRegs = MaxBundleIssue * MaxInstDestRegs;

// One decoded instruction within a bundle: its MDS opcode, the extracted
// operand values (decoded[] the behavior bodies read), and its byte offset from
// the bundle base (for PC-relative behavior).
struct SubInst
{
    unsigned opcode = 0;
    unsigned byteOffset = 0;
    // Issue slot this syllable was steered to, as an Exu value (BCU0, BCU1,
    // ALU0, ...).  GUARD's immediate is a mask of guarded execution units, so
    // predication needs to know which unit each syllable landed on.  The
    // numbering matches the assembler's LVX_EXU enum (gas tc-lvx.c), which is
    // what encodes the steering in the first place.
    unsigned exu = 0;
    uint64_t decoded[MaxOperandsPerInst] = {};
};

class LvxStaticInst;

// One syllable of a bundle, as a gem5 micro-op.
//
// It holds no decoded state of its own: everything comes from its bundle,
// which it keeps alive by reference. `index' is its position in the micro-op
// order, not the syllable's slot -- macro->microOrder[index] is the syllable.
//
// Only the head micro-op (index 0) carries source registers, and it carries
// the *bundle's*, deduplicated, exactly as the unsplit bundle did. That is
// what keeps a bundle's issue atomic: every later micro-op's sources are a
// subset of the head's with a read stage no earlier, so if the head's sources
// are ready they all are, and no micro-op can stall mid-bundle and split one
// bundle's issue across two cycles. Destinations are per syllable, each
// stamped with its own instruction's latency by its own functional unit.
class LvxMicroInst : public StaticInst
{
  protected:
    StaticInstPtr macro;
    unsigned index;

    // Sized for the two cases rather than for the worst of them: the head
    // needs the bundle's aggregated sources, the others need none.
    RegId srcRegIdxArr[MaxBundleSrcRegs];
    RegId destRegIdxArr[MaxInstDestRegs];

    std::vector<Cycles> srcRelLats;
    std::vector<Cycles> destRelLats;

  public:
    LvxMicroInst(const char *mnem, StaticInstPtr macroop, unsigned idx);

    const std::vector<Cycles> *
    srcRegsRelativeLats() const override
    {
        return srcRelLats.empty() ? nullptr : &srcRelLats;
    }

    const std::vector<Cycles> *
    destRegsRelativeLats() const override
    {
        return destRelLats.empty() ? nullptr : &destRelLats;
    }

    Fault execute(ExecContext *xc,
                  trace::InstRecord *traceData) const override;

    // A syllable that loads or stores is an isMemRef() of its own, so the
    // access goes through MinorCPU's load/store queue and the data cache
    // sees it. The queue is one access per instruction -- and a micro-op is
    // one -- which is why the bundle had to be split before this was
    // possible: LVX has two LSU slots.
    Fault initiateAcc(ExecContext *xc,
                      trace::InstRecord *traceData) const override;
    Fault completeAcc(PacketPtr pkt, ExecContext *xc,
                      trace::InstRecord *traceData) const override;

    void advancePC(PCStateBase &pc_state) const override;

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

class LvxStaticInst : public StaticInst
{
    friend class LvxMicroInst;

  protected:
    ExtMachInst machInst;
    unsigned bundleBytes;
    unsigned numSubInsts = 0;
    SubInst subInsts[MaxBundleIssue];

    // The macroop carries no register lists of its own: it is never issued
    // and never executed, so the dependency information belongs to the
    // micro-ops, each of which builds its own from the generated table.

    // The order the syllables execute in, and the micro-ops themselves.
    // microOrder[m] is the syllable index of micro-op m; there are
    // numSubInsts of each.
    unsigned char microOrder[MaxBundleIssue] = {};
    mutable StaticInstPtr microops[MaxBundleIssue];

    void buildMicroOrder();

  public:
    LvxStaticInst(const ExtMachInst &emi);

    // A bundle is always a macroop, even when it holds one syllable: one code
    // path rather than two, and the one-syllable case is a macroop with one
    // micro-op, which costs an indirection and nothing else. isMacroop() is
    // not virtual -- it reads flags[IsMacroop], set in the constructor.
    StaticInstPtr fetchMicroop(MicroPC upc) const override;

    // Run micro-op `mi': the operand snapshot if it is the head, then that
    // syllable's execute and commit phases, then the bundle-level work if it
    // is the last. LvxMicroInst::execute delegates here.
    Fault executeMicro(ExecContext *xc, trace::InstRecord *traceData,
                       unsigned mi) const;

    // The two halves of a micro-op whose syllable goes through the load/store
    // queue: issue the access, then consume the response and commit. `pkt' is
    // null when the access did not happen (a GUARD suppressed the syllable).
    Fault initiateMicro(ExecContext *xc, trace::InstRecord *traceData,
                        unsigned mi) const;
    Fault completeMicro(PacketPtr pkt, ExecContext *xc,
                        trace::InstRecord *traceData, unsigned mi) const;

  private:
    // Shared by all three: take the bundle's operand snapshot if `mi' is the
    // head, check it, and say whether a GUARD suppressed this syllable.
    bool prepareMicro(ExecContext *xc, unsigned mi) const;

  public:

    // Where execution goes next: the staged NPC, the hardware-loop back edge
    // and the redirect flags. Run by the last micro-op.
    void finishBundle(ExecContext *xc, BundleExec &be) const;

    Fault execute(ExecContext *xc,
                  trace::InstRecord *traceData) const override;

    void advancePC(PCStateBase &pc_state) const override;

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_STATIC_INST_HH__
