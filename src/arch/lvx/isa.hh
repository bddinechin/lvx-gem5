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
