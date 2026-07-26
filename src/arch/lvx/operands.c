/*
 * LVX ISA support for gem5 — operand decoder (Layer C, compiled as C).
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * The MDS Decode.c returns only an Opcode; the per-instruction operand fields
 * that the behavior bodies read as decoded[i] are extracted here. This mirrors
 * what Kalray's LAO instruction decoder does, reusing the MDS tuples directly:
 *
 *   Opcode.def     opcode -> operand-set id (its OPERANDS(...) field)
 *   Operand.def    Operands(set): ordered list of operand ids
 *                   Operand(op):   METHOD + a DECODE expr over WORDS[]
 *   RegClass.def   regclass -> ordered register list (index -> Register)
 *   Immediate.def  immediate type -> DECODE expr (sign-extend / <<s shift)
 *
 * For each operand we run its field DECODE over the instruction's syllables,
 * then apply the METHOD: register operands map the raw index through the
 * regclass register list to a Register enum value (so the bodies'
 * `decoded[i] - Register_lvx_R0` recovers the file index); immediates run
 * the Immediate DECODE (the bodies still re-apply SX, which is idempotent);
 * modifiers pass the raw field through.
 *
 * NOTE: the enum values here are biased +1 vs the MDS's own numbering (our
 * Register enum has a leading Register_ = 0), but the bias cancels in the
 * bodies' base subtraction, so it is internally consistent.
 */
#include <stdint.h>

#include "arch/lvx/behavior_rt.h"          /* OperandDecoded */
#include "arch/lvx/generated/lvx_enums.h"   /* Opcode, Register enums */

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
#include "arch/lvx/generated/RegClass.def"
#undef RegClass
#undef REGISTERS
#undef REGISTER

/* Per-class register-file base (the Register enum at file-relative index 0).
 *
 * The raw operand field encodes a register's position *within its file*, so the
 * correct decode is `filebase + raw` (the bodies then recover the file index as
 * `decoded[i] - filebase`).  The compact rc_<class>[] table only equals that
 * when the class lists the whole file in order (e.g. GPR singleReg, SFR
 * onlygetReg); it is WRONG for a view that omits the file base -- onlysetReg
 * drops the non-settable PC, so its raw=3 ($ra, SFR index 3) mis-indexes to CS.
 *
 * Only the SFR file needs this today (settable-vs-readable views differ); the
 * other files' operand classes are full-file-in-order, so they keep the compact
 * table via a -1 sentinel (no behaviour change).
 *
 * The register-file names are BARE (SFR, GPR, ...), not core-prefixed: RegClass.def
 * emits REGFILE(SFR) and Register_lvx_PC is the family-qualified name from lvx_enums.h
 * (same in both cores), so this file is identical for whichever core's generated/ dir
 * is compiled in -- that
 * is what lets one src/ tree build both gem5-lvx1 and gem5-lvx2.  The set below is
 * the union over both cores (they share it exactly, per the MDF merged RegClass
 * table); a core that adds a register file would fail to compile here until listed.
 * Register_lvx_PC is the family-qualified register name from lvx_enums.h. */
#define rfbase_SFR      Register_lvx_PC
#define rfbase_GPR      (-1)
#define rfbase_PGR      (-1)
#define rfbase_QGR      (-1)
#define rfbase_XCR      (-1)
#define rfbase_XBR      (-1)
#define rfbase_XVR      (-1)
#define rfbase_XMR      (-1)
#define rfbase_XTR      (-1)
#define rfbase_X2R      (-1)
#define rfbase_X4R      (-1)
#define rfbase_X8R      (-1)
#define rfbase_X16R     (-1)
#define rfbase_X32R     (-1)
#define rfbase_X64R     (-1)

#define REGFILE(f)           rfbase_##f
#define REGISTER(r)          /* nothing */
#define REGISTERS(count, rs) /* nothing */
#define RegClass(ID, RF, REGS, ENC, DEC, MRS)  enum { rcfilebase_##ID = (RF) };
#include "arch/lvx/generated/RegClass.def"
#undef RegClass
#undef REGISTERS
#undef REGISTER
#undef REGFILE

/* ------------------------------------------------------------------ *
 * 2. Immediate transforms: raw field -> sign-extended / shifted.     *
 * ------------------------------------------------------------------ */
#define Immediate(ID, MN, MX, EX, RL, EN, DE) \
    static uint64_t immdec_##ID(uint64_t VALUE) { DE; return VALUE; }
#include "arch/lvx/generated/Immediate.def"
#undef Immediate

/* ------------------------------------------------------------------ *
 * 3. Per-operand field extractors: WORDS[] -> raw field.             *
 * ------------------------------------------------------------------ */
#define Operand(ID, MTH, WT, ENC, DEC) \
    static uint64_t opfield_##ID(const uint32_t *WORDS) \
    { uint64_t VALUE = 0; (void)WORDS; DEC; return VALUE; }
#define Operands(ID, OPS, REL, ENC, DEC) /* handled below */
#include "arch/lvx/generated/Operand.def"
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
    int filebase;   /* register-file base for filebase+raw decode, or -1 */
} OpDesc;

typedef enum
{
#define Operand(ID, MTH, WT, ENC, DEC) OperandId_##ID,
#define Operands(ID, OPS, REL, ENC, DEC)
#include "arch/lvx/generated/Operand.def"
#undef Operand
#undef Operands
    OperandId__NUM
} OperandId;

/* METHOD(kind, sub) fills { method, rctab, rccount, immdec, filebase }. */
#define METHOD_RegClass(sub)  OPM_REG, rc_##sub, (int)(sizeof(rc_##sub) / sizeof(int)), 0, rcfilebase_##sub
#define METHOD_Immediate(sub) OPM_IMM, 0, 0, immdec_##sub, -1
#define METHOD_Modifier(sub)  OPM_MOD, 0, 0, 0, -1
#define METHOD(kind, sub)     METHOD_##kind(sub)

static const OpDesc operand_desc[OperandId__NUM] = {
#define Operand(ID, MTH, WT, ENC, DEC) [OperandId_##ID] = { opfield_##ID, MTH },
#define Operands(ID, OPS, REL, ENC, DEC)
#include "arch/lvx/generated/Operand.def"
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
#include "arch/lvx/generated/Operand.def"
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
#include "arch/lvx/generated/Opcode.def"
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
            /* raw is the register's position within its file; decode as
             * filebase + raw so view classes that omit the file base (e.g.
             * onlysetReg drops PC) still resolve correctly. Classes with no
             * base (-1) keep the compact table (full-file-in-order). */
            if (d->filebase >= 0)
                decoded[i] = (uint64_t)(d->filebase + (int)raw);
            else
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
