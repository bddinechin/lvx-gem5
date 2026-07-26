/*
 * LVX ISA support for gem5 — vector register file (XVR / XBR / XCR).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The LVX vector registers are all views of one physical storage, XRS: a flat
 * array of 256 x 64-bit cells. The MDS Register.table names the views:
 *
 *   XCR (C0..C255)  256 x  64-bit   C_i  = XRS[i]              ($a0.x/.y/.z/.t)
 *   XBR (B0..B127)  128 x 128-bit   B_i  = XRS[2i .. 2i+1]     ($a0.lo/.hi)
 *   XVR (A0..A63)    64 x 256-bit   A_i  = XRS[4i .. 4i+3]     ($a0)         <-- modeled here
 *   XTR (T0..T31)    32 x 512-bit                              (not operand-accessed yet)
 *   XMR (M0..M15)    16 x 1024-bit                             (not operand-accessed yet)
 *
 * We model the file at XVR granularity: 64 registers of 256 bits (VecRegContainer
 * <32>), the natural SIMD width. The narrower XBR/XCR views and the run-time-
 * indexed XRS-cell storage accesses index lanes of these containers -- lane order
 * is little-endian (A_i lane 0 == XRS[4i] == int256_t.dwords[0]), verified against
 * the register-table storage addresses. The shim (shim.cc) does the lane math.
 */
#ifndef __ARCH_LVX_REGS_VEC_HH__
#define __ARCH_LVX_REGS_VEC_HH__

#include "arch/generic/vec_reg.hh"
#include "cpu/reg_class.hh"
#include "debug/LvxRegs.hh"

namespace gem5
{
namespace LvxISA
{

namespace vec_reg
{
// XVR A0..A63 (256-bit each). XRS = 256 x 64-bit cells = these 64 regs x 4 lanes.
enum : RegIndex { NumRegs = 64 };
constexpr size_t RegBytes = 32;    // 256 bits
constexpr unsigned LanesPerReg = RegBytes / sizeof(uint64_t);   // 4
} // namespace vec_reg

using VecRegContainer = gem5::VecRegContainer<vec_reg::RegBytes>;

inline TypedRegClassOps<VecRegContainer> vecRegClassOps;

inline constexpr RegClass vecRegClass =
    RegClass(VecRegClass, VecRegClassName, vec_reg::NumRegs, debug::LvxRegs)
        .ops(vecRegClassOps)
        .regType<VecRegContainer>();

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_REGS_VEC_HH__
