#include "shim.h"
#include <stdio.h>
#include <assert.h>

/* Pull in the VERBATIM generated static execute_ functions. */
#include "generated.c"

#define ADDW execute_lvx_v1_ADDW_signextw_registerW_registerZ_registerY_simple
#define AWAIT execute_lvx_v1_AWAIT_simple

/* Fill an ADDW operand context: W=dest, Z/Y=sources, signextw modifier. */
static void set_addw(Insn *in, Cpu *cpu, int W, int Z, int Y, int signextw) {
    *in = (Insn){0};
    in->cpu = cpu;
    in->kind[0] = OPK_MOD; in->value[0] = int256_fromUInt64(signextw); /* %1 signextw */
    in->kind[1] = OPK_REG; in->regnum[1] = W;                          /* %2 registerW */
    in->kind[2] = OPK_REG; in->regnum[2] = Z;                          /* %3 registerZ */
    in->kind[3] = OPK_REG; in->regnum[3] = Y;                          /* %4 registerY */
}

int main(void) {
    Cpu cpu = {0};
    int fail = 0;

    /* Test 1: plain ADDW  r2 = r0 + r1 */
    cpu.gpr[0] = 10; cpu.gpr[1] = 20;
    Insn a; set_addw(&a, &cpu, /*W*/2, /*Z*/0, /*Y*/1, /*signed*/1);
    insn_fetch(&a);
    ADDW(&a, 0, 0);
    bundle_commit(&cpu);
    printf("Test1 ADDW: r2 = r0(10)+r1(20) => r2=%llu (want 30)\n",
           (unsigned long long)cpu.gpr[2]);
    fail |= (cpu.gpr[2] != 30);

    /* Test 2: ADDW with signextw=0 (zero-extend 32-bit result), negative-ish */
    cpu.gpr[3] = 0xFFFFFFF0ULL; cpu.gpr[4] = 0x20ULL; /* sum = 0x100000010 -> zx32 = 0x10 */
    Insn b; set_addw(&b, &cpu, /*W*/5, /*Z*/3, /*Y*/4, /*signed*/0);
    insn_fetch(&b);
    ADDW(&b, 0, 0);
    bundle_commit(&cpu);
    printf("Test2 ADDW: zx32(0xFFFFFFF0+0x20) => r5=0x%llx (want 0x10)\n",
           (unsigned long long)cpu.gpr[5]);
    fail |= (cpu.gpr[5] != 0x10ULL);

    /* Test 3: AWAIT (idle) just runs without touching regs */
    Insn w = {0}; w.cpu = &cpu;
    printf("Test3 AWAIT:\n");
    AWAIT(&w, 0, 0);

    /* Test 4: PARALLEL SEMANTICS via deferred write-back.
     * Bundle of two ADDWs where op2 reads a reg op1 writes:
     *   op1: r0 = r1 + r1   (-> 40)
     *   op2: r1 = r0 + r0   (must read OLD r0=10 -> 20, NOT new 40 -> 80)
     * Fetch BOTH (live read), execute BOTH (buffer writes), THEN commit. */
    cpu.gpr[0] = 10; cpu.gpr[1] = 20; cpu.nwb = 0;
    Insn i1, i2;
    set_addw(&i1, &cpu, /*W*/0, /*Z*/1, /*Y*/1, 0);   /* r0 = r1+r1 */
    set_addw(&i2, &cpu, /*W*/1, /*Z*/0, /*Y*/0, 0);   /* r1 = r0+r0 */
    insn_fetch(&i1);
    insn_fetch(&i2);
    ADDW(&i1, 0, 0);
    ADDW(&i2, 0, 0);
    bundle_commit(&cpu);
    printf("Test4 parallel bundle: r0=%llu (want 40), r1=%llu (want 20; sequential would give 80)\n",
           (unsigned long long)cpu.gpr[0], (unsigned long long)cpu.gpr[1]);
    fail |= (cpu.gpr[0] != 40 || cpu.gpr[1] != 20);

    printf("\n%s\n", fail ? "FAIL" : "ALL PASS");
    return fail;
}
