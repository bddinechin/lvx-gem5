/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — minimal BaseISA implementation to reach a first link.
 */

#ifndef __ARCH_LVX_ISA_HH__
#define __ARCH_LVX_ISA_HH__

#include <vector>

#include "arch/generic/isa.hh"
#include "arch/lvx/pcstate.hh"
#include "arch/lvx/regs/int.hh"
#include "arch/lvx/regs/misc.hh"
#include "base/types.hh"
#include "cpu/reg_class.hh"

namespace gem5
{

struct LvxISAParams;

namespace LvxISA
{

class ISA : public BaseISA
{
  protected:
    std::vector<RegVal> miscRegFile;

  public:
    using Params = LvxISAParams;

    ISA(const Params &p);

    PCStateBase *
    newPCState(Addr new_inst_addr = 0) const override
    {
        return new PCState(new_inst_addr);
    }

    void clear() override;

    // The loop engine's back edge: when the sequentially-next fetch address
    // reaches LE and the count has further trips to run, fetch continues at
    // LS.  No instruction branches -- LOOPDO merely loaded LS/LE/LC -- so
    // the fetch engine takes this edge itself and it costs nothing, which is
    // the whole point of a hardware loop against a rotated branch loop.
    //
    // LvxStaticInst::execute performs the same redirect architecturally;
    // this one only tells *fetch* in advance, so the stream is already where
    // it belongs and the timing model sees a correctly predicted branch
    // rather than a surprise worth a pipeline refill.
    //
    // LC here is the committed count, which lags fetch: near the end of a
    // loop fetch may take the edge once or twice too often and be squashed
    // on exit.  That is one refill per loop execution instead of one per
    // trip, and erring this way is right -- the alternative is a fetch-side
    // copy of LC that has to be unwound on every squash.
    bool fetchRedirect(PCStateBase &next_pc) const override;

    RegVal readMiscRegNoEffect(RegIndex idx) const override;
    RegVal readMiscReg(RegIndex idx) override;
    void setMiscRegNoEffect(RegIndex idx, RegVal val) override;
    void setMiscReg(RegIndex idx, RegVal val) override;

    bool inUserMode() const override { return true; } // SE mode
    void copyRegsFrom(ThreadContext *src) override;
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_ISA_HH__
