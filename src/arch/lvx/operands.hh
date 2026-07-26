/*
 * LVX ISA support for gem5 — operand decoder interface (Layer C).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * C++ view of the operand extractor implemented in operands.c (compiled as C).
 */
#ifndef __ARCH_LVX_OPERANDS_HH__
#define __ARCH_LVX_OPERANDS_HH__

#include <cstdint>

#include "arch/lvx/behavior_rt.h"

extern "C" {

// Fill decoded[] (in operand-set order) for `opcode` from the instruction's
// syllables `words`. decoded[] must have room for the opcode's operand count
// (<= 8 for scalar LVX). Register operands become Register enum values; the
// behavior bodies recover the file index via `decoded[i] - Register_lvx_*`.
void lvx_decode_operands(unsigned opcode, const uint32_t *words,
                         OperandDecoded *decoded);

} // extern "C"

#endif // __ARCH_LVX_OPERANDS_HH__
