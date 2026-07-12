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

namespace
{
// Empty placeholders for register classes LVX does not model yet (scalar
// milestone). gem5 indexes _regClasses by RegClassType, so every type must have
// an entry, in enum order: Int, Float, Vec, VecElem, VecPred, Mat, CC, Misc.
constexpr RegClass floatRegClass(FloatRegClass, FloatRegClassName, 0, debug::LvxRegs);
constexpr RegClass vecRegClass(VecRegClass, VecRegClassName, 0, debug::LvxRegs);
constexpr RegClass vecElemClass(VecElemClass, VecElemClassName, 0, debug::LvxRegs);
constexpr RegClass vecPredRegClass(VecPredRegClass, VecPredRegClassName, 0, debug::LvxRegs);
constexpr RegClass matRegClass(MatRegClass, MatRegClassName, 0, debug::LvxRegs);
constexpr RegClass ccRegClass(CCRegClass, CCRegClassName, 0, debug::LvxRegs);
} // anonymous namespace

ISA::ISA(const Params &p) : BaseISA(p, "lvx")
{
    _regClasses.push_back(&intRegClass);
    _regClasses.push_back(&floatRegClass);
    _regClasses.push_back(&vecRegClass);
    _regClasses.push_back(&vecElemClass);
    _regClasses.push_back(&vecPredRegClass);
    _regClasses.push_back(&matRegClass);
    _regClasses.push_back(&ccRegClass);
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
