/*
 * LVX ISA support for gem5 — LvxStaticInst (Layer C).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * One LvxStaticInst represents a whole VLIW bundle. execute() runs the bundle's
 * instructions with VLIW parallel semantics — every instruction's fetch phase
 * (register reads), then every execute phase, then every commit phase (register
 * writes) — dispatching each through the MDS behavior via the Layer B shim.
 */
#ifndef __ARCH_LVX_STATIC_INST_HH__
#define __ARCH_LVX_STATIC_INST_HH__

#include "arch/lvx/types.hh"
#include "cpu/reg_class.hh"
#include "cpu/static_inst.hh"

namespace gem5
{
namespace LvxISA
{

// Max operand fields any single LVX instruction decodes into (observed <= ~6).
inline constexpr unsigned MaxOperandsPerInst = 12;

// A bundle's aggregated source/destination register lists (across its up-to-8
// issued instructions) for the timing CPUs' dependency graph. Sized with
// headroom for lvx_v1; lvx_v2's register-buffer operands (up to 64 XVR regs for
// buffer64Reg) will need a larger source bound when they reach this port.
inline constexpr unsigned MaxBundleSrcRegs = 48;
inline constexpr unsigned MaxBundleDestRegs = 24;

// One decoded instruction within a bundle: its MDS opcode, the extracted
// operand values (decoded[] the behavior bodies read), and its byte offset from
// the bundle base (for PC-relative behavior).
struct SubInst
{
    unsigned opcode = 0;
    unsigned byteOffset = 0;
    uint64_t decoded[MaxOperandsPerInst] = {};
};

class LvxStaticInst : public StaticInst
{
  protected:
    ExtMachInst machInst;
    unsigned bundleBytes;
    unsigned numSubInsts = 0;
    SubInst subInsts[MaxBundleSyllables];

    // Backing storage for the base class's _srcRegIdxPtr / _destRegIdxPtr,
    // populated in the constructor from the generated lvx_reg_deps table.
    RegId srcRegIdxArr[MaxBundleSrcRegs];
    RegId destRegIdxArr[MaxBundleDestRegs];

    // Populate the src/dest register lists from the generated lvx_reg_deps table.
    void setUpRegs();

  public:
    LvxStaticInst(const ExtMachInst &emi);

    Fault execute(ExecContext *xc,
                  trace::InstRecord *traceData) const override;

    void advancePC(PCStateBase &pc_state) const override;

    std::string generateDisassembly(
            Addr pc, const loader::SymbolTable *symtab) const override;
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_STATIC_INST_HH__
