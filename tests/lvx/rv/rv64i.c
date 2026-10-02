/*
 * RV64I coverage for the LVX ISS's RV64G personality.
 *
 * One check per group of instructions, each adding nothing to the exit code
 * when it passes and returning its own number when it fails -- so the exit
 * code names the first thing that broke, and 0 means all of them passed.
 * `volatile' on the operands keeps the compiler from folding a check into its
 * answer, which would test nothing.
 *
 * The point is the instructions, not the C: the W forms, the signed/unsigned
 * compares, the shift widths and the sub-word loads are where a ported
 * description gets a sign extension or a shift mask wrong, and each of those
 * is wrong in a way that ordinary arithmetic does not notice.
 */
typedef unsigned long u64;
typedef long          i64;
typedef unsigned int  u32;
typedef int           i32;

long rv_write(int fd, const void *buf, unsigned long n);

static char buf[64];

int
main(void)
{
    /* 1: ADD/SUB/ADDI and the 32-bit W forms, which sign-extend from bit 31. */
    {
        volatile i64 a = 0x7fffffff, b = 1;
        if (a + b != 0x80000000L) return 1;
        volatile i32 w = 0x7fffffff, x = 1;
        if ((i64)(i32)(w + x) != -0x80000000L) return 1;   /* addw wraps */
    }

    /* 2: the logical ops and their immediate forms. */
    {
        volatile u64 a = 0xf0f0f0f0f0f0f0f0UL, b = 0x00ff00ff00ff00ffUL;
        if ((a & b) != 0x00f000f000f000f0UL) return 2;
        if ((a | b) != 0xf0fff0fff0fff0ffUL) return 2;
        if ((a ^ b) != 0xf00ff00ff00ff00fUL) return 2;
        if ((a & 0xff) != 0xf0) return 2;
    }

    /* 3: shifts at 64 bits -- the count is taken mod 64. */
    {
        volatile u64 a = 1;
        volatile int n = 63;
        if ((a << n) != 0x8000000000000000UL) return 3;
        volatile i64 s = -256;
        if ((s >> 4) != -16) return 3;                  /* sra, not srl */
        volatile u64 u = 0xff00000000000000UL;
        if ((u >> 56) != 0xff) return 3;
    }

    /* 4: the W shifts -- count mod 32, result sign-extended from bit 31. */
    {
        volatile i32 a = -256;
        volatile int n = 4;
        if ((i64)(a >> n) != -16) return 4;             /* sraw */
        volatile u32 u = 0x80000000u;
        if ((u64)(u >> 31) != 1) return 4;              /* srlw */
        volatile i32 b = 1;
        if ((i64)(i32)(b << 31) != -0x80000000L) return 4;  /* sllw */
    }

    /* 5: SLT/SLTU -- the same bits compare differently signed and unsigned. */
    {
        volatile i64 a = -1, b = 1;
        if (!(a < b)) return 5;
        if (!((u64)a > (u64)b)) return 5;
        if ((i64)(a < 0) != 1) return 5;                /* slti */
    }

    /* 6: the branches, including the unsigned pair. */
    {
        volatile i64 a = -1, b = 1;
        int taken = 0;
        if (a == a) taken |= 1;
        if (a != b) taken |= 2;
        if (a <  b) taken |= 4;
        if (b >= a) taken |= 8;
        if ((u64)b < (u64)a) taken |= 16;
        if ((u64)a >= (u64)b) taken |= 32;
        if (taken != 63) return 6;
    }

    /* 7: the loads and stores at every width, signed and unsigned. */
    {
        volatile unsigned char *b8  = (unsigned char *)buf;
        volatile unsigned short *b16 = (unsigned short *)(buf + 8);
        volatile u32 *b32 = (u32 *)(buf + 16);
        volatile u64 *b64 = (u64 *)(buf + 24);
        *b8 = 0x80; *b16 = 0x8000; *b32 = 0x80000000u; *b64 = 0x8000000000000000UL;
        if (*b8 != 0x80 || (signed char)*b8 != -128) return 7;        /* lbu/lb */
        if (*b16 != 0x8000 || (short)*b16 != -32768) return 7;        /* lhu/lh */
        if (*b32 != 0x80000000u) return 7;                            /* lwu */
        if ((i64)(i32)*b32 != -0x80000000L) return 7;                 /* lw */
        if (*b64 != 0x8000000000000000UL) return 7;                   /* ld */
    }

    /* 8: a loop -- branches, an induction variable and an indexed store. */
    {
        u64 sum = 0;
        for (int i = 0; i < 16; i++) {
            buf[i + 32] = (char)(i * 3);
            sum += (unsigned char)buf[i + 32];
        }
        if (sum != 360) return 8;                       /* 3*(0+..+15) */
    }

    /* 9: LUI/AUIPC reach a static address, and a call returns. */
    {
        if (rv_write(1, "rv64i ok\n", 9) != 9) return 9;
    }

    return 0;
}
