/*
 * RV64M and RV64A for the LVX ISS's RV64G personality.
 *
 * M: the multiply high forms, where a wrong signedness is invisible to a plain
 * `*', and the divides, whose two architectural special cases (division by
 * zero, and the most-negative value divided by -1) RISC-V *defines* rather
 * than traps -- so a description that forgets them is wrong in a way C's own
 * undefined behaviour will not show.  REM's sign is the other trap: it follows
 * the DIVIDEND (a truncated remainder), which is not what a floored modulo
 * gives, and the two agree for every non-negative operand.
 *
 * A: the read-modify-writes return the OLD value, and LR/SC's result is
 * inverted from the intuitive one -- 0 means the store happened.
 *
 * One check per group, each returning its own number; 0 means all passed.
 */
typedef unsigned long u64;
typedef long          i64;
typedef unsigned int  u32;
typedef int           i32;

long rv_write(int fd, const void *buf, unsigned long n);

static u64 cell;

int
main(void)
{
    /* 1: MUL and MULW -- MULW's result is sign-extended from bit 31. */
    {
        volatile i64 a = 0x100000001L, b = 3;
        if (a * b != 0x300000003L) return 1;
        volatile i32 w = 0x40000000, x = 4;
        if ((i64)(i32)(w * x) != 0) return 1;           /* mulw wraps to 0 */
        volatile i32 y = 0x30000000, z = 3;
        if ((i64)(i32)(y * z) != -0x70000000L) return 1; /* and sign-extends */
    }

    /* 2: the high multiplies -- signed, unsigned, and the mixed one. */
    {
        volatile i64 a = -1, b = -1;
        if ((i64)(((__int128)a * b) >> 64) != 0) return 2;        /* mulh */
        if ((u64)(((unsigned __int128)(u64)a * (u64)b) >> 64)
            != 0xfffffffffffffffeUL) return 2;                     /* mulhu */
        if ((i64)(((__int128)a * (unsigned __int128)(u64)b) >> 64)
            != -1) return 2;                                       /* mulhsu */
    }

    /* 3: signed divide and remainder, including a negative dividend. */
    {
        volatile i64 a = -7, b = 2;
        if (a / b != -3) return 3;                      /* truncated, not floored */
        if (a % b != -1) return 3;                      /* sign follows the dividend */
        volatile i64 c = 7, d = -2;
        if (c / d != -3) return 3;
        if (c % d != 1) return 3;
    }

    /* 4: the W divides -- 32-bit operands, result sign-extended. */
    {
        volatile i32 a = -7, b = 2;
        if ((i64)(a / b) != -3) return 4;               /* divw */
        if ((i64)(a % b) != -1) return 4;               /* remw */
        volatile u32 c = 0x80000000u, d = 2;
        if ((i64)(i32)(c / d) != 0x40000000L) return 4; /* divuw */
    }

    /* 5: unsigned divide and remainder. */
    {
        volatile u64 a = 0xffffffffffffffffUL, b = 3;
        if (a / b != 0x5555555555555555UL) return 5;
        if (a % b != 0) return 5;
    }

    /* 6: the atomic read-modify-writes, which return the OLD value. */
    {
        cell = 100;
        if (__atomic_fetch_add(&cell, 5, __ATOMIC_RELAXED) != 100) return 6;
        if (cell != 105) return 6;
        if (__atomic_fetch_or(&cell, 0x200, __ATOMIC_RELAXED) != 105) return 6;
        if (cell != 0x269) return 6;
        if (__atomic_fetch_and(&cell, 0xff, __ATOMIC_RELAXED) != 0x269) return 6;
        if (cell != 0x69) return 6;
        if (__atomic_exchange_n(&cell, 7, __ATOMIC_RELAXED) != 0x69) return 6;
        if (cell != 7) return 6;
    }

    /* 7: compare-and-swap, which the compiler builds out of LR/SC -- so this
     *    is where an inverted SC result or a reservation that never holds
     *    shows up, as a loop that does not terminate or one that never
     *    stores. */
    {
        cell = 42;
        u64 expect = 42;
        if (!__atomic_compare_exchange_n(&cell, &expect, 99, 0,
                                         __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return 7;
        if (cell != 99) return 7;
        expect = 42;                        /* now the wrong value */
        if (__atomic_compare_exchange_n(&cell, &expect, 7, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED))
            return 7;                       /* must fail */
        if (cell != 99 || expect != 99) return 7;
    }

    /* 8: the 32-bit atomics, whose result is sign-extended from bit 31. */
    {
        static u32 cell32;
        cell32 = 0x7fffffffu;
        if (__atomic_fetch_add(&cell32, 1, __ATOMIC_RELAXED) != 0x7fffffffu)
            return 8;
        if (cell32 != 0x80000000u) return 8;
    }

    rv_write(1, "rv64ma ok\n", 10);
    return 0;
}
