/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — foundational header, not yet compile-verified.
 */

#ifndef __ARCH_LVX_REGS_INT_HH__
#define __ARCH_LVX_REGS_INT_HH__

#include "cpu/reg_class.hh"
#include "debug/LvxRegs.hh" // DebugFlag('LvxRegs') to be declared in SConscript

namespace gem5
{
namespace LvxISA
{

namespace int_reg
{

// LVX general-purpose registers r0..r63, 64-bit (LP64). Unlike RISC-V there is
// no hardwired zero register. The 6-bit register field of the encoding indexes
// this class directly, so indices == architectural register numbers.
//
// (SFR240..SFR255 alias as extra GPRs at the ISA level; that aliasing, if
// needed, is handled in the misc/SFR storage, not here.)
enum : RegIndex
{
    NumArchRegs = 64,
    NumRegs = NumArchRegs
};

} // namespace int_reg

inline constexpr RegClass intRegClass(IntRegClass, IntRegClassName,
        int_reg::NumRegs, debug::LvxRegs);

// ABI register roles (SP/FP/return-value/...) are defined at the reg_abi layer
// per the kv4-v1 ABI; kept out of this foundational header intentionally.

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_REGS_INT_HH__
