/*
 * LVX ISA support for gem5 — MDS behavior integration (Layer A).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * This translation unit is compiled as C (the generated bodies use `this` as a
 * parameter name). It mirrors Kalray's ISS_Behavior.c integration pattern:
 *
 *   1. Provide HELPER(name) -> Behavior_name and the helper definitions.
 *      For now every helper is a panic stub (helper_stubs.inc); real
 *      implementations that touch gem5 state arrive with the runtime shim (#8).
 *   2. Include Behavior.tuple with the phase macros defined so the
 *      fetch_/execute_/commit_ bodies are emitted as static functions.
 *   3. Include Behavior.tuple again to build the [Opcode][phase] dispatch table.
 *
 * Unlike the KVX ISS the table is keyed by the Opcode enum via designated
 * initializers: LVX's Opcode.tuple has 13 pseudo-opcodes (ADJUST, MOVP,
 * SYSCALL, ...) with no Behavior entry, so a positional table would misalign.
 */
#include <stdbool.h>
#include <stdint.h>

#include "arch/lvx/int256.h"
#include "arch/lvx/behavior_rt.h"
#include "arch/lvx/shim.h"
#include "arch/lvx/generated/MDT/MDT_.h"

#define HELPER(routine) Behavior_##routine

/* Operator-helper definitions: panic stubs for now (the SIMD/float/atomic
 * operators, ~Phase 3). These 137 are derived from Behavior.tuple's
 * declarations by regen.sh. The 17 *core* runtime helpers (operand/regfile/
 * storage access) are NOT here — they are implemented in shim.cc (Layer B, #8)
 * and declared C-linkage in shim.h, included above. */
#include "arch/lvx/generated/helper_stubs.inc"

/* Emit the fetch_/execute_/commit_ bodies as static functions. */
#define Behavior_FETCH
#define Behavior_EXECUTE
#define Behavior_COMMIT
#define Behavior_STATIC
#include "arch/lvx/generated/Behavior.tuple"
#undef Behavior_STATIC
#undef Behavior_COMMIT
#undef Behavior_EXECUTE
#undef Behavior_FETCH

/* Dispatch table: lvx_Opcode_Behavior[opcode][phase], phase in {0:fetch,
 * 1:execute, 2:commit}. Opcodes without a body stay {0,0,0}. */
const Behavior lvx_Opcode_Behavior[Opcode__NUM][3] = {
#define FETCH(ID)   ID
#define EXECUTE(ID) ID
#define COMMIT(ID)  ID
#define Behavior(OPCODE, F, E, C) [Opcode_##OPCODE] = { F, E, C },
#include "arch/lvx/generated/Behavior.tuple"
#undef Behavior
#undef COMMIT
#undef EXECUTE
#undef FETCH
};

/* Number of rows == number of Opcode enum values. */
const unsigned lvx_Opcode_Behavior_num = Opcode__NUM;

/* Panic target for not-yet-implemented helpers (Phase 1 link). */
void
lvx_behavior_unimpl(void)
{
    __builtin_trap();
}
