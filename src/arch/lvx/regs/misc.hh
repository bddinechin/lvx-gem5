/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — foundational header, not yet compile-verified.
 */

#ifndef __ARCH_LVX_REGS_MISC_HH__
#define __ARCH_LVX_REGS_MISC_HH__

#include "cpu/reg_class.hh"
#include "debug/LvxRegs.hh"

namespace gem5
{
namespace LvxISA
{

namespace misc_reg
{

// The LVX Special Function Registers (SFRs). In the MDS behavior these are the
// "SFR" Register storage, addressed by number (e.g. the generated RET reads
// SFR #3 = $ra). We model the whole SFR address space as one MiscRegClass and
// let the shim map readFromStorage_SFR/writeToStorage_SFR -> read/setMiscReg.
//
// PC and NPC are *Control* storage in the behavior, not SFRs — the shim maps
// readFromStorage_PC / writeToStorage_NPC onto the gem5 PCState, not here.
//
// TODO(Phase 1): replace the placeholder indices below with the authoritative
// SFR numbering from the MDS-generated registers.h (lvx-newlib) /
// Register.table. Only RA is confirmed so far (observed in generated RET).
enum : RegIndex
{
    PS = 1, // $s1 processor status  (bitfield; SE mode models PS.HLE only)
    RA = 3, // $ra return address (confirmed from generated RET behavior)
    CS = 4, // $s4 compute status  (FP status/rounding; SE mode reads default 0)
    LS = 7, // $s7 hardware-loop start PC   (LOOPDO, confirmed from generated behavior)
    LE = 8, // $s8 hardware-loop end PC     (LOOPDO)
    LC = 9, // $s9 hardware-loop iteration count (LOOPDO)

    // Full SFR address space SFR0..SFR255 (architectural SFR64..SFR255 plus
    // low reserved/aliased range); most are unused in SE mode.
    NumRegs = 256
};

// Processor Status (PS) is a bitfield *Control* storage in the MDS behavior,
// not an SFR. SE-mode user code observes almost none of it, but PS.HLE (bit 5,
// hardware-loop enable) gates LOOPDO: with it clear the instruction throws an
// OPCODE trap. We model SE mode as "hardware loops always enabled" — HLE set,
// everything else zero. Both the PS-read shim (Layer B) and the bundle
// loop-back engine (Layer C) key off this single value.
namespace ps
{
enum : unsigned
{
    HLE_BIT = 5, // PS.HLE — hardware-loop enable (offset from generated LOOPDO)
};

inline constexpr uint64_t SE_MODE_VALUE = (uint64_t{1} << HLE_BIT);

inline constexpr bool hwLoopEnabled() { return (SE_MODE_VALUE >> HLE_BIT) & 1; }

} // namespace ps

} // namespace misc_reg

inline constexpr RegClass miscRegClass(MiscRegClass, MiscRegClassName,
        misc_reg::NumRegs, debug::LvxRegs);

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_REGS_MISC_HH__
