/*
 * LVX ISA support for gem5 — minimal SE-mode TLB (Layer D, milestone).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * SE-only translation: virtual addresses are resolved through the process's
 * emulation page table (demand-mapped via fixupFault). No hardware page walk /
 * TLB caching is modeled. TODO(FS): a real MMU for full-system mode.
 */
#ifndef __ARCH_LVX_TLB_HH__
#define __ARCH_LVX_TLB_HH__

#include "arch/generic/tlb.hh"
#include "base/logging.hh"
#include "cpu/thread_context.hh"
#include "mem/page_table.hh"
#include "mem/request.hh"
#include "params/LvxTLB.hh"
#include "sim/faults.hh"
#include "sim/process.hh"

namespace gem5
{
namespace LvxISA
{

class TLB : public BaseTLB
{
  public:
    using Params = LvxTLBParams;
    TLB(const Params &p) : BaseTLB(p) {}

    void demapPage(Addr, uint64_t) override {}
    void flushAll() override {}
    void takeOverFrom(BaseTLB *) override {}

    Fault
    finalizePhysical(const RequestPtr &req, ThreadContext *tc,
                     BaseMMU::Mode mode) const override
    {
        return NoFault;
    }

    Fault
    translateAtomic(const RequestPtr &req, ThreadContext *tc,
                    BaseMMU::Mode mode) override
    {
        return translateSe(req, tc);
    }

    void
    translateTiming(const RequestPtr &req, ThreadContext *tc,
                    BaseMMU::Translation *translation,
                    BaseMMU::Mode mode) override
    {
        Fault fault = translateSe(req, tc);
        translation->finish(fault, req, tc, mode);
    }

    Fault
    translateFunctional(const RequestPtr &req, ThreadContext *tc,
                        BaseMMU::Mode mode)
    {
        return translateSe(req, tc);
    }

  private:
    Fault
    translateSe(const RequestPtr &req, ThreadContext *tc)
    {
        Process *p = tc->getProcessPtr();
        Addr vaddr = req->getVaddr();
        Addr paddr = 0;
        if (!p->pTable->translate(vaddr, paddr)) {
            if (!p->fixupFault(vaddr))
                return std::make_shared<GenericPageTableFault>(vaddr);
            p->pTable->translate(vaddr, paddr);
        }
        req->setPaddr(paddr);
        return NoFault;
    }
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_TLB_HH__
