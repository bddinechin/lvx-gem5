/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — minimal BaseISA implementation to reach a first link.
 */

#include "arch/lvx/isa.hh"

#include "cpu/thread_context.hh"
#include "params/LvxISA.hh"

namespace gem5
{
namespace LvxISA
{

ISA::ISA(const Params &p) : BaseISA(p, "lvx")
{
    _regClasses.push_back(&intRegClass);
    _regClasses.push_back(&miscRegClass);

    miscRegFile.resize(misc_reg::NumRegs, 0);
    clear();
}

void
ISA::clear()
{
    std::fill(miscRegFile.begin(), miscRegFile.end(), 0);
}

RegVal
ISA::readMiscRegNoEffect(RegIndex idx) const
{
    return miscRegFile[idx];
}

RegVal
ISA::readMiscReg(RegIndex idx)
{
    return readMiscRegNoEffect(idx);
}

void
ISA::setMiscRegNoEffect(RegIndex idx, RegVal val)
{
    miscRegFile[idx] = val;
}

void
ISA::setMiscReg(RegIndex idx, RegVal val)
{
    setMiscRegNoEffect(idx, val);
}

void
ISA::copyRegsFrom(ThreadContext *src)
{
    for (auto &id : intRegClass)
        tc->setReg(id, src->getReg(id));

    for (RegIndex i = 0; i < misc_reg::NumRegs; i++)
        tc->setMiscRegNoEffect(i, src->readMiscRegNoEffect(i));

    tc->pcState(src->pcState());
}

} // namespace LvxISA
} // namespace gem5
