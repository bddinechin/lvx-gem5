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
#include "arch/lvx/generated/MDT/MDT_.h"

#define HELPER(routine) Behavior_##routine

/* Helper definitions (panic stubs for now — see file banner / #8). These 137
 * are the "operator" helpers, derived from Behavior.tuple's declarations. */
#include "arch/lvx/generated/helper_stubs.inc"

/*
 * Core runtime helpers. Unlike the operator helpers above these are NOT
 * declared in Behavior.tuple — in Kalray's ISS they live in helpers_core.h and
 * form the fixed interface between the generated bodies and the engine
 * (operand decode, register-file and storage access). They are the LVX gem5
 * runtime shim's real job (#8): each will read/write gem5 ExecContext state.
 * For the Phase 1 link they panic; when the shim lands these move to shim.cc
 * (compiled as C++, extern "C") and are deleted from here.
 *
 * `Int256 mask` in Kalray's operandFromValue is only ever passed 0 by the
 * generated code, so we take it as a scalar.
 */
void HELPER(commitRegFiles)(void *this) { lvx_behavior_unimpl(); }

void HELPER(operandFromRegFile_GPR)(void *this, unsigned stage, int rank, int opnd_idx, int register_id) { lvx_behavior_unimpl(); }
void HELPER(operandFromRegFile_PGR)(void *this, unsigned stage, int rank, int opnd_idx, int register_id) { lvx_behavior_unimpl(); }
void HELPER(operandFromRegFile_QGR)(void *this, unsigned stage, int rank, int opnd_idx, int register_id) { lvx_behavior_unimpl(); }
void HELPER(operandFromRegFile_SFR)(void *this, unsigned stage, int rank, int opnd_idx, int register_id) { lvx_behavior_unimpl(); }

void HELPER(operandToRegFile_GPR)(void *this, unsigned stage, int rank, int opnd_idx, int register_id) { lvx_behavior_unimpl(); }
void HELPER(operandToRegFile_PGR)(void *this, unsigned stage, int rank, int opnd_idx, int register_id) { lvx_behavior_unimpl(); }
void HELPER(operandToRegFile_QGR)(void *this, unsigned stage, int rank, int opnd_idx, int register_id) { lvx_behavior_unimpl(); }
void HELPER(operandToRegFile_SFR)(void *this, unsigned stage, int rank, int opnd_idx, int register_id) { lvx_behavior_unimpl(); }

void HELPER(operandFromValue)(void *this, int rank, int opnd_idx, uint64_t mask, Int256_ value) { lvx_behavior_unimpl(); }
Int256_ HELPER(operandRead)(void *this, int opnd_idx) { lvx_behavior_unimpl(); }

Int256_ HELPER(readFromStorage_NPC)(void *this, unsigned stage, unsigned offset, unsigned extent, unsigned size) { lvx_behavior_unimpl(); }
Int256_ HELPER(readFromStorage_PC)(void *this, unsigned stage, unsigned offset, unsigned extent, unsigned size) { lvx_behavior_unimpl(); }
Int256_ HELPER(readFromStorage_PS)(void *this, unsigned stage, unsigned offset, unsigned extent, unsigned size) { lvx_behavior_unimpl(); }
Int256_ HELPER(readFromStorage_SFR)(void *this, unsigned stage, unsigned offset, unsigned extent, unsigned size) { lvx_behavior_unimpl(); }

void HELPER(writeToStorage_NPC)(void *this, unsigned stage, unsigned offset, unsigned extent, unsigned size, Int256_ value) { lvx_behavior_unimpl(); }
void HELPER(writeToStorage_SFR)(void *this, unsigned stage, unsigned offset, unsigned extent, unsigned size, Int256_ value) { lvx_behavior_unimpl(); }

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
