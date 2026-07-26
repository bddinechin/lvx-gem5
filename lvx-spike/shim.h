/* Runtime shim for the LVX gem5 spike.
 * This is the seam the generated Behavior.tuple code depends on. In the real
 * port these HELPER()s map to gem5 ExecContext (readIntRegOperand /
 * setIntRegOperand / readMem / writeMem); here they map to a toy CPU state so
 * the generated bodies can be compiled and run unmodified. */
#ifndef LVX_SPIKE_SHIM_H
#define LVX_SPIKE_SHIM_H
#include <stdint.h>
/* Single source of truth for Int256_ is the real gem5 arch shim; the spike used to
 * carry its own minimal, divergent copy. */
#include "../src/arch/lvx/int256.h"

typedef int64_t  ImmediateValue;
typedef uint64_t OperandDecoded;
typedef struct Processor_ *Processor;

/* The generated code calls HELPER(name)(...). */
#define HELPER(name) sh_##name

enum { OPK_NONE = 0, OPK_REG, OPK_IMM, OPK_MOD };

typedef struct Cpu {
    uint64_t gpr[64];
    struct { int gpr; Int256_ val; } wb[32]; /* bundle write buffer */
    int nwb;
} Cpu;

/* Per-instruction decode context; `this` points at one of these. */
typedef struct Insn {
    Cpu    *cpu;
    int     kind[8];
    int     regnum[8];   /* OPK_REG: GPR index */
    Int256_ value[8];    /* operand slot values (populated by insn_fetch) */
} Insn;

/* --- runtime API used by the generated code --- */
void    sh_idle(void *thiz, uint8_t v);   /* unboxed: idle's arg is narrowed to its demand */
Int256_ sh_operandRead(void *thiz, int index);
void    sh_operandFromValue(void *thiz, int rank, int index, int bias, Int256_ v);

/* --- bundle engine (Layer C in the plan) --- */
void insn_fetch(Insn *in);    /* read live regfile into value[] for reg operands */
void bundle_commit(Cpu *cpu); /* flush buffered writes to the regfile */

#endif
