/*
 * LVX ISA support for gem5 — interrupt controller (SE-mode stub).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * SE mode never delivers interrupts; this is the minimal BaseInterrupts a CPU
 * needs to be constructed. TODO(FS): real interrupt/trap delivery.
 */
#ifndef __ARCH_LVX_INTERRUPTS_HH__
#define __ARCH_LVX_INTERRUPTS_HH__

#include "arch/generic/interrupts.hh"
#include "params/LvxInterrupts.hh"
#include "sim/faults.hh"

namespace gem5
{
namespace LvxISA
{

class Interrupts : public BaseInterrupts
{
  public:
    using Params = LvxInterruptsParams;
    Interrupts(const Params &p) : BaseInterrupts(p) {}

    bool checkInterrupts() const override { return false; }
    Fault getInterrupt() override { return NoFault; }
    void updateIntrInfo() override {}
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_INTERRUPTS_HH__
