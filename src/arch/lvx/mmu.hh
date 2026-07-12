/*
 * LVX ISA support for gem5 — minimal MMU (Layer D, milestone).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * BaseMMU delegates atomic/timing/functional translation of a request to the
 * itb/dtb TLBs; the only piece we must supply is the region translation
 * generator used by functional accesses.
 */
#ifndef __ARCH_LVX_MMU_HH__
#define __ARCH_LVX_MMU_HH__

#include "arch/generic/mmu.hh"
#include "params/LvxMMU.hh"

namespace gem5
{
namespace LvxISA
{

class MMU : public BaseMMU
{
  public:
    static constexpr Addr PageBytes = 4096;

    MMU(const LvxMMUParams &p) : BaseMMU(p) {}

    TranslationGenPtr
    translateFunctional(Addr start, Addr size, ThreadContext *tc,
                        Mode mode, Request::Flags flags) override
    {
        return TranslationGenPtr(new MMUTranslationGen(
                PageBytes, start, size, tc, this, mode, flags));
    }
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_MMU_HH__
