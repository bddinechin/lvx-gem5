/*
 * The signed/unsigned boundary of RV64I's compares -- a regression test for a
 * bug the port introduced and `rv64i.c' only narrowed to a group of four.
 *
 * KV4's branch behaviours called typed helpers (comp64_lt, comp64_ge, ...).
 * Porting them to LVX's Behavior primitives, (LT a b) and (GE a b), dropped
 * the type: a Behavior register read is UNSIGNED at its container width, so
 * the comparison came out unsigned and `bgez' with -1 in the register branched
 * as if -1 >= 0.  The sign has to be stated on both operands -- (SX.64 ...) --
 * which is exactly why KV4's own SLT writes it and its branches did not have
 * to.  Only a negative operand shows it, so every non-negative test passes.
 *
 * Exit code is a bitmap of the four, so one run says which direction is wrong
 * rather than just that something is: expect 7.
 */
typedef unsigned long u64;
typedef long          i64;

int
main(void)
{
    volatile i64 a = -1, b = 1;
    int r = 0;
    if (a < b)           r |= 1;   /* slt/blt   signed: -1 < 1, set */
    if ((u64)a > (u64)b) r |= 2;   /* sltu/bgeu unsigned: huge > 1, set */
    if (a < 0)           r |= 4;   /* slti/bgez signed: -1 < 0, set */
    if ((u64)a < 2)      r |= 8;   /* sltiu     unsigned: huge < 2, CLEAR */
    return r;
}
