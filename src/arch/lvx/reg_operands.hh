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
// PGR and QGR are not files of their own: a 128-bit value lives in an aligned
// GPR *pair* and a 256-bit one in an aligned *quad* (Storage.yml), so an
// operand of either expands here into its 2 or 4 member GPRs.  Everything a
// 128- or 256-bit SIMD instruction writes lands in one, which is why they have
// to be tracked: until 2026-10-16 they were not, and every packed result was a
// destination gem5 could not see -- no RAW stall and no latency, so a
// vectorised loop's dependent chain cost nothing.
enum LvxRegFile : unsigned char
{
    LVX_RF_GPR = 0, LVX_RF_SFR = 1, LVX_RF_PGR = 2, LVX_RF_QGR = 3
};

// GPRs per operand of each file, indexed by LvxRegFile (SFR is a single
// register of the misc class; the tuples are 2 and 4 general ones).
inline constexpr unsigned lvxRegFileTupleSize[] = { 1, 1, 2, 4 };

// One register operand: `slot >= 0` means the register is decoded[slot] (index =
// decoded[slot] - file base); `slot < 0` means a fixed register numbered `fixed`
// (e.g. $ra = SFR 3, read implicitly by RET).
struct LvxRegOperand
{
    unsigned char  file;
    signed char    slot;
    unsigned short fixed;
    // On a source: cycles before its producer's result is ready that this
    // operand may be read -- its read stage minus RR. 0 for an operand read
    // at RR, which is nearly all of them; 1 for a store's stored value, an
    // atomic's, and the integer MAC's accumulator, which are read at E1. So
    // an `ffmaw' (writing at E4) feeding a store is three cycles away, not
    // four, which is what lvx-gcc states as a define_bypass. Always 0 on a
    // destination.
    unsigned char  relLat;
};

// Per-opcode source and destination register operands, plus the result's
// dependence latency in cycles (write stage - read stage, from the Behavior
// pipeline annotations): 1 for an ALU result, 3 for a cached load, 11 for
// integer divide, 13 for FP divide/sqrt, 15 for an atomic read-modify-write
// and for an uncached load. A bundle takes the max over its syllables.
//
// `varLat` is that latency as a function of a modifier operand's decoded
// value, for the loads whose write stage the `variant` modifier moves: `ld'
// returns from the L1 at stage CR, `ld.u'/`ld.us' bypass it and return at SR,
// which is a latency of 15 rather than 3 (Modifier.yml's LoadedAt, folded into
// Opcode@timing by MDE and resolved per encoded value by BE/GEM5). `varSlot`
// is the decoded[] slot holding that modifier, or -1 when the opcode has one
// latency -- in which case `varLat` is null and `lat` is the whole story.
struct LvxRegDeps
{
    const LvxRegOperand *src;
    unsigned char        nsrc;
    const LvxRegOperand *dst;
    unsigned char        ndst;
    unsigned char        lat;
    signed char          varSlot;
    const unsigned char *varLat;
    unsigned char        nVarLat;
};

// Defined in generated/behavior.c (compiled as C). Indexed by the Opcode enum.
extern "C" {
extern const LvxRegDeps lvx_reg_deps[];
}

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_REG_OPERANDS_HH__
