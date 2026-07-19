/*
 * LVX ISA support for gem5 — register-operand table (C++ view).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The MDS BE/GEM5 back-end generates generated/reg_operands.inc: for every
 * opcode, the source and destination register operands, derived from the
 * behavior bodies' operand{From,To}RegFile / {read,write}FromStorage calls.
 * That .inc is compiled as C in generated/behavior.c (so its Opcode-indexed
 * designated initializer is standard); the resulting `lvx_reg_deps` table has
 * external C linkage. This header is the C++ view LvxStaticInst reads to build
 * its _srcRegIdx / _destRegIdx lists for the timing CPUs' dependency graph.
 *
 * The two POD structs mirror the ones reg_operands.inc defines on the C side;
 * they are trivial and layout-compatible by construction.
 */
#ifndef __ARCH_LVX_REG_OPERANDS_HH__
#define __ARCH_LVX_REG_OPERANDS_HH__

#include <cstdint>

namespace gem5
{
namespace LvxISA
{

// Register-file codes used in the generated table (must match reg_operands.inc).
enum LvxRegFile : unsigned char { LVX_RF_GPR = 0, LVX_RF_SFR = 1 };

// One register operand: `slot >= 0` means the register is decoded[slot] (index =
// decoded[slot] - file base); `slot < 0` means a fixed register numbered `fixed`
// (e.g. $ra = SFR 3, read implicitly by RET).
struct LvxRegOperand
{
    unsigned char  file;
    signed char    slot;
    unsigned short fixed;
};

// Per-opcode source and destination register operands, plus the result's
// dependence latency in cycles (write stage - read stage, from the Behavior
// pipeline annotations): 1 for an ALU result, 3 for a load, 15 for divide/sqrt,
// 23 for an atomic read-modify-write. A bundle takes the max over its syllables.
struct LvxRegDeps
{
    const LvxRegOperand *src;
    unsigned char        nsrc;
    const LvxRegOperand *dst;
    unsigned char        ndst;
    unsigned char        lat;
};

// Defined in generated/behavior.c (compiled as C). Indexed by the Opcode enum.
extern "C" {
extern const LvxRegDeps lvx_reg_deps[];
}

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_REG_OPERANDS_HH__
