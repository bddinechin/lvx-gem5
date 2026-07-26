/*
 * LVX ISA support for gem5 — runtime types for the MDS-generated behavior C.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Plain C (included by behavior.c). Defines the types the generated
 * behavior_bodies.inc bodies reference and the dispatch-entry type.
 */
#ifndef __ARCH_LVX_BEHAVIOR_RT_H__
#define __ARCH_LVX_BEHAVIOR_RT_H__

#include <stdint.h>

#include "arch/lvx/int256.h"

// An operand slot value (register number / immediate / modifier), as produced
// by operand decoding and consumed by HELPER(operandRead) etc.
typedef uint64_t OperandDecoded;

// Immediate operand value as used in fetch bodies.
typedef int64_t ImmediateValue;

// Opaque engine handle. TODO(#8): this becomes a gem5 ExecContext bridge.
typedef void *Processor;

// Modifier operand value accessor used by fetch bodies.
#define ModifierMember_value(x) ((uint64_t)(x))

// Dispatch-entry type (cf. MDS MDT/Behavior.h). Param names omitted so the type
// is usable from C++ too (the generated bodies name the first param `this`).
typedef void (*Behavior)(void *, OperandDecoded *, Processor);

// Panic target for not-yet-implemented HELPERs (Phase 1 link; real impls #8).
// noreturn lets one stub body satisfy every helper return type.
void lvx_behavior_unimpl(void) __attribute__((noreturn));

#endif // __ARCH_LVX_BEHAVIOR_RT_H__
