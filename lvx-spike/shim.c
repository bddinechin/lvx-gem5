#include "shim.h"
#include <stdio.h>

void sh_idle(void *thiz, uint8_t v) {
    (void)thiz;
    printf("  [idle %llu]\n", (unsigned long long)v);
}

int256_t sh_operandRead(void *thiz, int index) {
    Insn *in = (Insn *)thiz;
    return in->value[index];
}

void sh_operandFromValue(void *thiz, int rank, int index, int bias, int256_t v) {
    (void)index; (void)bias;
    Insn *in = (Insn *)thiz;
    Cpu *c = in->cpu;
    int dst = in->regnum[rank - 1];      /* rank is 1-based operand number */
    c->wb[c->nwb].gpr = dst;
    c->wb[c->nwb].val = v;
    c->nwb++;
}

void insn_fetch(Insn *in) {
    for (int i = 0; i < 8; i++)
        if (in->kind[i] == OPK_REG)
            in->value[i] = int256_fromUInt64(in->cpu->gpr[in->regnum[i]]);
}

void bundle_commit(Cpu *c) {
    for (int i = 0; i < c->nwb; i++)
        c->gpr[c->wb[i].gpr] = c->wb[i].val.dwords[0];
    c->nwb = 0;
}
