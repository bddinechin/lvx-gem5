/*
 * LVX ISA support for gem5 — operand decoder (Layer C, compiled as C).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The MDS Decode.c returns only an Opcode; the per-instruction operand fields
 * that the behavior bodies read as decoded[i] are extracted here. This mirrors
 * what Kalray's LAO instruction decoder does, reusing the MDS tuples directly:
 *
 *   Opcode.tuple    opcode -> operand-set id (its OPERANDS(...) field)
 *   Operand.tuple   Operands(set): ordered list of operand ids
 *                   Operand(op):   METHOD + a DECODE expr over WORDS[]
 *   RegClass.tuple  regclass -> ordered register list (index -> Register)
 *   Immediate.tuple immediate type -> DECODE expr (sign-extend / <<s shift)
 *
 * For each operand we run its field DECODE over the instruction's syllables,
 * then apply the METHOD: register operands map the raw index through the
 * regclass register list to a Register enum value (so the bodies'
 * `decoded[i] - Register_lvx_v1_R0` recovers the file index); immediates run
 * the Immediate DECODE (the bodies still re-apply SX, which is idempotent);
 * modifiers pass the raw field through.
 *
 * NOTE: the enum values here are biased +1 vs the MDS's own numbering (our
 * Register enum has a leading Register_ = 0), but the bias cancels in the
 * bodies' base subtraction, so it is internally consistent.
 */
#include <stdint.h>

#include "arch/lvx/behavior_rt.h"          /* OperandDecoded */
#include "arch/lvx/generated/MDT/MDT_.h"   /* Opcode, Register enums */

/* The tuples wrap each extraction expression as DECODE(<stmts>); unwrap it to
 * the bare statements. (ENCODE(...) fields are never used here.) */
#define DECODE(...) __VA_ARGS__

/* ------------------------------------------------------------------ *
 * 1. Register-class tables: index -> Register enum value.            *
 * ------------------------------------------------------------------ */
#define REGISTER(r)          Register_##r,
#define REGISTERS(count, rs) rs
#define RegClass(ID, REGFILE, REGS, ENC, DEC, MRS) \
    static const int rc_##ID[] = { REGS };
#include "arch/lvx/generated/RegClass.tuple"
#undef RegClass
#undef REGISTERS
#undef REGISTER

/* ------------------------------------------------------------------ *
 * 2. Immediate transforms: raw field -> sign-extended / shifted.     *
 * ------------------------------------------------------------------ */
#define Immediate(ID, MN, MX, EX, RL, EN, DE) \
    static uint64_t immdec_##ID(uint64_t VALUE) { DE; return VALUE; }
#include "arch/lvx/generated/Immediate.tuple"
#undef Immediate

/* ------------------------------------------------------------------ *
 * 3. Per-operand field extractors: WORDS[] -> raw field.             *
 * ------------------------------------------------------------------ */
#define Operand(ID, MTH, WT, ENC, DEC) \
    static uint64_t opfield_##ID(const uint32_t *WORDS) \
    { uint64_t VALUE = 0; (void)WORDS; DEC; return VALUE; }
#define Operands(ID, OPS, REL, ENC, DEC) /* handled below */
#include "arch/lvx/generated/Operand.tuple"
#undef Operand
#undef Operands

/* ------------------------------------------------------------------ *
 * 4. Operand id enum + per-operand descriptor table.                 *
 * ------------------------------------------------------------------ */
enum { OPM_REG, OPM_IMM, OPM_MOD };

typedef struct
{
    uint64_t (*field)(const uint32_t *);
    int method;
    const int *rctab;
    int rccount;
    uint64_t (*immdec)(uint64_t);
} OpDesc;

typedef enum
{
#define Operand(ID, MTH, WT, ENC, DEC) OperandId_##ID,
#define Operands(ID, OPS, REL, ENC, DEC)
#include "arch/lvx/generated/Operand.tuple"
#undef Operand
#undef Operands
    OperandId__NUM
} OperandId;

/* METHOD(kind, sub) fills { method, rctab, rccount, immdec }. */
#define METHOD_RegClass(sub)  OPM_REG, rc_##sub, (int)(sizeof(rc_##sub) / sizeof(int)), 0
#define METHOD_Immediate(sub) OPM_IMM, 0, 0, immdec_##sub
#define METHOD_Modifier(sub)  OPM_MOD, 0, 0, 0
#define METHOD(kind, sub)     METHOD_##kind(sub)

static const OpDesc operand_desc[OperandId__NUM] = {
#define Operand(ID, MTH, WT, ENC, DEC) [OperandId_##ID] = { opfield_##ID, MTH },
#define Operands(ID, OPS, REL, ENC, DEC)
#include "arch/lvx/generated/Operand.tuple"
#undef Operand
#undef Operands
};

#undef METHOD
#undef METHOD_Modifier
#undef METHOD_Immediate
#undef METHOD_RegClass

/* ------------------------------------------------------------------ *
 * 5. Operand sets: opcode's OPERANDS(set) -> ordered operand ids.    *
 *    Arrays are terminated by a -1 sentinel; _UNDEF == no operands.  *
 * ------------------------------------------------------------------ */
#define OPERAND(o)        OperandId_##o,
#define OPERANDS(n, list) list
#define Operand(ID, MTH, WT, ENC, DEC)
#define Operands(ID, OPS, REL, ENC, DEC) static const int opset_##ID[] = { OPS -1 };
#include "arch/lvx/generated/Operand.tuple"
#undef Operands
#undef Operand
#undef OPERANDS
#undef OPERAND

static const int opset__UNDEF[] = { -1 };

/* ------------------------------------------------------------------ *
 * 6. Opcode -> operand-set array.                                    *
 * ------------------------------------------------------------------ */
#define OPERANDS(set) opset_##set
static const int *const opcode_opset[Opcode__NUM] = {
#define Opcode(ID, SCH, DC, CW, OPS, INC, MN, SY, AA) [Opcode_##ID] = OPS,
#include "arch/lvx/generated/Opcode.tuple"
#undef Opcode
};
#undef OPERANDS

/* ------------------------------------------------------------------ *
 * 7. Runtime entry point.                                            *
 * ------------------------------------------------------------------ */
void
lvx_decode_operands(unsigned opcode, const uint32_t *words,
                    OperandDecoded *decoded)
{
    if (opcode >= (unsigned)Opcode__NUM)
        return;
    const int *set = opcode_opset[opcode];
    if (!set)
        return;
    for (int i = 0; set[i] >= 0; i++) {
        const OpDesc *d = &operand_desc[set[i]];
        uint64_t raw = d->field(words);
        switch (d->method) {
          case OPM_REG:
            decoded[i] = (raw < (unsigned)d->rccount) ? (uint64_t)d->rctab[raw] : 0;
            break;
          case OPM_IMM:
            decoded[i] = d->immdec(raw);
            break;
          default: /* OPM_MOD */
            decoded[i] = raw;
            break;
        }
    }
}
