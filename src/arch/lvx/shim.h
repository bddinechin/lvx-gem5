/*
 * LVX ISA support for gem5 — core runtime helper prototypes (Layer B).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * These 17 "core" helpers are the fixed interface between the MDS-generated
 * behavior bodies (behavior.c, compiled as C) and the gem5 machine state. In
 * Kalray's ISS they live in helpers_core.h; here they are implemented in
 * shim.cc (C++) against a BehaviorContext / gem5 ThreadContext and exported
 * with C linkage so behavior.c can call them.
 *
 * This header is included by both behavior.c (as C) and shim.cc (as C++); it
 * must stay valid C. Parameter names are omitted so `this` (the first arg in
 * the generated bodies) does not clash with the C++ keyword.
 */
#ifndef __ARCH_LVX_SHIM_H__
#define __ARCH_LVX_SHIM_H__

#include <stdint.h>

#include "arch/lvx/int256.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Operand slot access (fetch fills source slots; execute reads them / stages
 * the result slot; commit writes result slots back). */
Int256_ Behavior_operandRead(void * /*this*/, int /*opnd_idx*/);
void    Behavior_operandFromValue(void * /*this*/, int /*rank*/, int /*opnd_idx*/,
                                  uint64_t /*mask*/, Int256_ /*value*/);

/* Register-file reads (fetch phase): load arch register `register_id` into the
 * operand slot. GPR is a single 64-bit reg; PGR/QGR are 2/4 consecutive GPRs. */
void Behavior_operandFromRegFile_GPR(void *, unsigned /*stage*/, int /*rank*/, int /*opnd_idx*/, int /*register_id*/);
void Behavior_operandFromRegFile_PGR(void *, unsigned, int, int, int);
void Behavior_operandFromRegFile_QGR(void *, unsigned, int, int, int);
void Behavior_operandFromRegFile_SFR(void *, unsigned, int, int, int);

/* Register-file writes (commit phase): store the operand slot to `register_id`
 * if the slot was written by execute. */
void Behavior_operandToRegFile_GPR(void *, unsigned, int, int, int);
void Behavior_operandToRegFile_PGR(void *, unsigned, int, int, int);
void Behavior_operandToRegFile_QGR(void *, unsigned, int, int, int);
void Behavior_operandToRegFile_SFR(void *, unsigned, int, int, int);

/* Drain any deferred register commits (no-op in the immediate-write model). */
void Behavior_commitRegFiles(void *);

/* Control/status storage. offset/extent/size address a run of `extent` fields
 * of `size` bits starting at `offset`. */
/* Memory access (functional, via the SE port proxy). opnd1 = address,
 * opnd2 = byte-mask (encodes the access size), opnd3 = modifier/coherency,
 * opnd4 = stored value (store only), last = destination-register info (unused
 * functionally). */
Int256_ Behavior_MEM_load (void *, uint64_t, Int256_, uint8_t, uint8_t);
void    Behavior_MEM_store(void *, uint64_t, Int256_, uint8_t, Int256_, uint8_t);

/* System-call trap (scall). opnd1 = syscall number; arguments are in r0..r7,
 * return value goes in r0 (kv4-v1 ABI). */
void Behavior_syscall(void *, uint64_t /*number*/);
/* Branch hint emitted by control-flow instructions; no architectural effect. */
void Behavior_branch_info(void *, uint8_t, uint64_t);
/* Invalidate the hardware-loop prefetch buffer (LOOPDO). A fetch-pipeline hint
 * with no functional effect in the SE-mode ISS. */
void Behavior_invalpfb(void *);
/* Conditional-branch / conditional-move predicate: opnd1 = 4-bit bcucond code,
 * opnd2 = the tested register value.  Returns whether the condition holds. */
bool Behavior_bcucond(void *, uint8_t /*condcode*/, uint64_t /*value*/);
/* SRHPC (privilege-level saved handler PC) update on RET/call return; no
 * architectural effect in SE-mode user execution. */
void Behavior_srhpc_update(void *);
/* SFR access permission checks (GET/SET/WFXL/WFXM). SE-mode user execution has
 * no privilege model, so every access is permitted. */
bool Behavior_get_check_access (void *, uint16_t, uint8_t);
bool Behavior_set_check_access (void *, uint16_t, uint64_t, uint8_t);
bool Behavior_wfxl_check_access(void *, uint16_t, uint8_t);
bool Behavior_wfxm_check_access(void *, uint16_t, uint8_t);
/* GET: return the already-loaded SFR value (opnd2), no privilege side effects. */
Int256_ Behavior_get(void *, uint16_t /*sfr*/, uint64_t /*value*/);
/* Integer comparison (COMP*): opnd1 = intcomp code, opnd2/opnd3 = the values;
 * returns the boolean result. */
bool Behavior_intcomp_32(void *, uint8_t /*code*/, uint64_t, uint64_t);
bool Behavior_intcomp_64(void *, uint8_t /*code*/, uint64_t, uint64_t);

Int256_ Behavior_readFromStorage_PC (void *, unsigned, unsigned, unsigned, unsigned);
Int256_ Behavior_readFromStorage_NPC(void *, unsigned, unsigned, unsigned, unsigned);
Int256_ Behavior_readFromStorage_PS (void *, unsigned, unsigned, unsigned, unsigned);
Int256_ Behavior_readFromStorage_CS (void *, unsigned, unsigned, unsigned, unsigned);
void    Behavior_writeToStorage_CS  (void *, unsigned, unsigned, unsigned, unsigned, Int256_);
Int256_ Behavior_readFromStorage_SFR(void *, unsigned, unsigned, unsigned, unsigned);
/* SRS: unified system-register storage (shares SFR numbering). Post SFR->SRS
 * refactor this is the main system-register access path (PS, CS, LS/LE/LC...). */
Int256_ Behavior_readFromStorage_SRS(void *, unsigned, unsigned, unsigned, unsigned);
void    Behavior_writeToStorage_NPC (void *, unsigned, unsigned, unsigned, unsigned, Int256_);
void    Behavior_writeToStorage_SFR (void *, unsigned, unsigned, unsigned, unsigned, Int256_);
void    Behavior_writeToStorage_SRS (void *, unsigned, unsigned, unsigned, unsigned, Int256_);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __ARCH_LVX_SHIM_H__
