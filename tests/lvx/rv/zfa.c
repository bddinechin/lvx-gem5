/*
 * Zfa's FROUND and FROUNDNX in the RV64G mode, against Spike.
 *
 * These two are not a second implementation of anything: they are the NATIVE
 * LVX instructions FROUND* and FRINT* carrying a RISC-V encoding, the way
 * Zicsr's csrrw is the native CSRRW under RVZI_CSRR.  Which is exactly why
 * they are worth a test -- what is checked is that the second encoding reaches
 * the same behaviour, so the two personalities cannot drift apart.
 *
 * The pair differs in one flag and nothing else: froundnx raises inexact when
 * the rounding changed the value, fround does not.  So every measurement is a
 * result-and-fflags pair, as in fpflags.c, and the flag column is the only
 * place the two differ.
 *
 * The instructions are written with `.insn' rather than by name: the installed
 * binutils is 2.35.1, which predates Zfa and rejects `fround.d'.  The encoding
 * is still the encoding, and Spike knows what to do with the word.
 *
 * The rs2 field is a selector, not an operand, so it is spelled as a register
 * token (x4/x5): gas's `.insn r' wants a register there, and a bare integer
 * makes it try the seven-argument R4 form instead.
 *
 *   fround.d   rd, rs1, rm   funct7 = 0100001, rs2 = 00100
 *   froundnx.d rd, rs1, rm   funct7 = 0100001, rs2 = 00101
 *   fround.s   rd, rs1, rm   funct7 = 0100000, rs2 = 00100
 *   froundnx.s rd, rs1, rm   funct7 = 0100000, rs2 = 00101
 */
typedef unsigned long u64;
typedef unsigned int  u32;

long rv_write(int fd, const void *buf, u64 n);

static u64 out[4096];
static int n;

static void putd(double d) { u64 b; __builtin_memcpy(&b, &d, 8); out[n++] = b; }
static void putf(float f)  { u32 b; __builtin_memcpy(&b, &f, 4); out[n++] = b; }
static void putu(u64 v)    { out[n++] = v; }

static double mkd(u64 b) { double d; __builtin_memcpy(&d, &b, 8); return d; }

static inline void
flags_clear(void)
{
    u64 zero = 0;
    __asm__ volatile ("csrw fflags, %0" :: "r"(zero) : "memory");
}

static inline u64
flags_after_d(double keep)
{
    u64 f;
    __asm__ volatile ("csrr %0, fflags" : "=r"(f) : "f"(keep) : "memory");
    return f;
}

/* funct3 IS the rounding mode, so each mode needs its own .insn -- hence a
 * macro taking the mode as a token rather than a variable. */
#define ROUND_D(dst, src, rs2, rm) \
    __asm__ volatile (".insn r 0x53, " #rm ", 0x21, %0, %1, " #rs2 \
                      : "=f"(dst) : "f"(src))
#define ROUND_S(dst, src, rs2, rm) \
    __asm__ volatile (".insn r 0x53, " #rm ", 0x20, %0, %1, " #rs2 \
                      : "=f"(dst) : "f"(src))

#define MEASURE_RD(src, rs2, rm) do {               \
    double r_;                                      \
    flags_clear();                                  \
    ROUND_D(r_, (src), rs2, rm);                    \
    u64 f_ = flags_after_d(r_);                     \
    putd(r_); putu(f_);                             \
} while (0)

#define MEASURE_RS(src, rs2, rm) do {               \
    float r_;                                       \
    flags_clear();                                  \
    ROUND_S(r_, (src), rs2, rm);                    \
    u64 f_ = flags_after_d((double)r_);             \
    putf(r_); putu(f_);                             \
} while (0)

/* Values that round and values that do not, the halfway cases that separate
 * the five rounding modes, and the specials. */
static volatile double dv[] = {
    0.0, -0.0, 2.5, -2.5, 3.5, -3.5, 0.5, -0.5,
    2.0, -2.0, 1.0e300, 4.9406564584124654e-324,
    0.25, -0.25, 1e16, 9007199254740993.0,
};
#define ND ((int)(sizeof dv / sizeof dv[0]))
static const u64 dspecial[] = {
    0x7ff0000000000000UL, 0xfff0000000000000UL,     /* +inf, -inf */
    0x7ff8000000000000UL, 0x7ff4000000000000UL,     /* qNaN, sNaN */
};
#define NS ((int)(sizeof dspecial / sizeof dspecial[0]))

static volatile float fv[] = { 0.0f, 2.5f, -2.5f, 0.5f, 2.0f, 16777217.0f };
#define NF ((int)(sizeof fv / sizeof fv[0]))


/* ---- the rest of Zfa -------------------------------------------------------
 *
 *   fminm.d, fmaxm.d   funct7 0010101, funct3 010 and 011
 *   fltq.d, fleq.d     funct7 1010001, funct3 101 and 100
 *   fli.d              funct7 1111001, rs2 00001, the index in RS1
 *   fcvtmod.w.d        funct7 1100001, rs2 01000, funct3 001 (rtz fixed)
 *
 * fminm and fmaxm are the native LVX FMIND and FMAXD under a RISC-V encoding,
 * and fltq/fleq are the native quiet FCOMPD with its condition fixed; fli and
 * fcvtmod.w.d are RISC-V-only, having no native counterpart.
 *
 * fli's immediate lives in the RS1 FIELD, so it cannot be a compiler-allocated
 * operand -- each of the 32 constants needs its own .insn with the index
 * written as a register token.  Hence the list rather than a loop.
 */
#define MINM_D(dst, a, b) \
    __asm__ volatile (".insn r 0x53, 2, 0x15, %0, %1, %2" : "=f"(dst) : "f"(a), "f"(b))
#define MAXM_D(dst, a, b) \
    __asm__ volatile (".insn r 0x53, 3, 0x15, %0, %1, %2" : "=f"(dst) : "f"(a), "f"(b))
#define MINM_S(dst, a, b) \
    __asm__ volatile (".insn r 0x53, 2, 0x14, %0, %1, %2" : "=f"(dst) : "f"(a), "f"(b))
#define MAXM_S(dst, a, b) \
    __asm__ volatile (".insn r 0x53, 3, 0x14, %0, %1, %2" : "=f"(dst) : "f"(a), "f"(b))
#define LTQ_D(dst, a, b) \
    __asm__ volatile (".insn r 0x53, 5, 0x51, %0, %1, %2" : "=r"(dst) : "f"(a), "f"(b))
#define LEQ_D(dst, a, b) \
    __asm__ volatile (".insn r 0x53, 4, 0x51, %0, %1, %2" : "=r"(dst) : "f"(a), "f"(b))
#define LTQ_S(dst, a, b) \
    __asm__ volatile (".insn r 0x53, 5, 0x50, %0, %1, %2" : "=r"(dst) : "f"(a), "f"(b))
#define LEQ_S(dst, a, b) \
    __asm__ volatile (".insn r 0x53, 4, 0x50, %0, %1, %2" : "=r"(dst) : "f"(a), "f"(b))
#define CVTMOD(dst, a) \
    __asm__ volatile (".insn r 0x53, 1, 0x61, %0, %1, x8" : "=r"(dst) : "f"(a))
#define FLI_D(dst, idx) \
    __asm__ volatile (".insn r 0x53, 0, 0x79, %0, " #idx ", x1" : "=f"(dst))
#define FLI_S(dst, idx) \
    __asm__ volatile (".insn r 0x53, 0, 0x78, %0, " #idx ", x1" : "=f"(dst))

static inline u64
flags_after_i(u64 keep)
{
    u64 f;
    __asm__ volatile ("csrr %0, fflags" : "=r"(f) : "r"(keep) : "memory");
    return f;
}

#define MEASURE_2D(op, a, b) do {                   \
    double r_;                                      \
    flags_clear();                                  \
    op(r_, (a), (b));                               \
    u64 f_ = flags_after_d(r_);                     \
    putd(r_); putu(f_);                             \
} while (0)

#define MEASURE_2S(op, a, b) do {                   \
    float r_;                                       \
    flags_clear();                                  \
    op(r_, (a), (b));                               \
    u64 f_ = flags_after_d((double)r_);             \
    putf(r_); putu(f_);                             \
} while (0)

#define MEASURE_2I(op, a, b) do {                   \
    u64 r_;                                         \
    flags_clear();                                  \
    op(r_, (a), (b));                               \
    u64 f_ = flags_after_i(r_);                     \
    putu(r_); putu(f_);                             \
} while (0)

#define MEASURE_1I(op, a) do {                      \
    u64 r_;                                         \
    flags_clear();                                  \
    op(r_, (a));                                    \
    u64 f_ = flags_after_i(r_);                     \
    putu(r_); putu(f_);                             \
} while (0)

#define MEASURE_FLI_D(idx) do {                     \
    double r_;                                      \
    flags_clear();                                  \
    FLI_D(r_, idx);                                 \
    u64 f_ = flags_after_d(r_);                     \
    putd(r_); putu(f_);                             \
} while (0)

#define MEASURE_FLI_S(idx) do {                     \
    float r_;                                       \
    flags_clear();                                  \
    FLI_S(r_, idx);                                 \
    u64 f_ = flags_after_d((double)r_);             \
    putf(r_); putu(f_);                             \
} while (0)

static void
rest_of_zfa(void)
{
    /* fminm/fmaxm: the NaN-propagating pair, so the canonical NaN whenever
     * either operand is one -- which is what separates them from the base
     * fmin/fmax, and only a NaN operand shows it. */
    for (int i = 0; i < ND + NS; i++)
        for (int j = 0; j < ND + NS; j++) {
            double a = i < ND ? dv[i] : mkd(dspecial[i - ND]);
            double b = j < ND ? dv[j] : mkd(dspecial[j - ND]);
            MEASURE_2D(MINM_D, a, b);
            MEASURE_2D(MAXM_D, a, b);
        }
    for (int i = 0; i < NF; i++)
        for (int j = 0; j < NF; j++) {
            MEASURE_2S(MINM_S, fv[i], fv[j]);
            MEASURE_2S(MAXM_S, fv[i], fv[j]);
        }

    /* fltq/fleq: the QUIET compares, so a quiet NaN raises nothing where the
     * base flt/fle raise invalid.  The NaN rows are the whole point. */
    for (int i = 0; i < ND + NS; i++)
        for (int j = 0; j < ND + NS; j++) {
            double a = i < ND ? dv[i] : mkd(dspecial[i - ND]);
            double b = j < ND ? dv[j] : mkd(dspecial[j - ND]);
            MEASURE_2I(LTQ_D, a, b);
            MEASURE_2I(LEQ_D, a, b);
        }
    for (int i = 0; i < NF; i++)
        for (int j = 0; j < NF; j++) {
            MEASURE_2I(LTQ_S, fv[i], fv[j]);
            MEASURE_2I(LEQ_S, fv[i], fv[j]);
        }

    /* fli: all 32 constants at both widths, raising nothing. */
    MEASURE_FLI_D(x0);  MEASURE_FLI_D(x1);  MEASURE_FLI_D(x2);  MEASURE_FLI_D(x3);
    MEASURE_FLI_D(x4);  MEASURE_FLI_D(x5);  MEASURE_FLI_D(x6);  MEASURE_FLI_D(x7);
    MEASURE_FLI_D(x8);  MEASURE_FLI_D(x9);  MEASURE_FLI_D(x10); MEASURE_FLI_D(x11);
    MEASURE_FLI_D(x12); MEASURE_FLI_D(x13); MEASURE_FLI_D(x14); MEASURE_FLI_D(x15);
    MEASURE_FLI_D(x16); MEASURE_FLI_D(x17); MEASURE_FLI_D(x18); MEASURE_FLI_D(x19);
    MEASURE_FLI_D(x20); MEASURE_FLI_D(x21); MEASURE_FLI_D(x22); MEASURE_FLI_D(x23);
    MEASURE_FLI_D(x24); MEASURE_FLI_D(x25); MEASURE_FLI_D(x26); MEASURE_FLI_D(x27);
    MEASURE_FLI_D(x28); MEASURE_FLI_D(x29); MEASURE_FLI_D(x30); MEASURE_FLI_D(x31);
    MEASURE_FLI_S(x0);  MEASURE_FLI_S(x1);  MEASURE_FLI_S(x2);  MEASURE_FLI_S(x3);
    MEASURE_FLI_S(x4);  MEASURE_FLI_S(x5);  MEASURE_FLI_S(x6);  MEASURE_FLI_S(x7);
    MEASURE_FLI_S(x8);  MEASURE_FLI_S(x9);  MEASURE_FLI_S(x10); MEASURE_FLI_S(x11);
    MEASURE_FLI_S(x12); MEASURE_FLI_S(x13); MEASURE_FLI_S(x14); MEASURE_FLI_S(x15);
    MEASURE_FLI_S(x16); MEASURE_FLI_S(x17); MEASURE_FLI_S(x18); MEASURE_FLI_S(x19);
    MEASURE_FLI_S(x20); MEASURE_FLI_S(x21); MEASURE_FLI_S(x22); MEASURE_FLI_S(x23);
    MEASURE_FLI_S(x24); MEASURE_FLI_S(x25); MEASURE_FLI_S(x26); MEASURE_FLI_S(x27);
    MEASURE_FLI_S(x28); MEASURE_FLI_S(x29); MEASURE_FLI_S(x30); MEASURE_FLI_S(x31);

    /* fcvtmod.w.d: modular, not saturating -- so the out-of-range values are
     * where it differs from every other conversion, and NaN/infinity give 0
     * with invalid rather than a saturated extreme. */
    {
        static volatile double cv[] = {
            0.0, -0.0, 1.5, -1.5, 2147483647.0, 2147483648.0, -2147483648.0,
            -2147483649.0, 4294967296.0, 4294967297.5, 1e300, -1e300,
            1e9, -1e9, 0.25, -0.25, 9007199254740992.0, 1e-300,
        };
        for (int i = 0; i < (int)(sizeof cv / sizeof cv[0]); i++)
            MEASURE_1I(CVTMOD, cv[i]);
        MEASURE_1I(CVTMOD, mkd(0x7ff0000000000000UL));   /* +inf */
        MEASURE_1I(CVTMOD, mkd(0xfff0000000000000UL));   /* -inf */
        MEASURE_1I(CVTMOD, mkd(0x7ff8000000000000UL));   /* qNaN */
        MEASURE_1I(CVTMOD, mkd(0x7ff4000000000000UL));   /* sNaN */
    }
}

int
main(void)
{
    for (int i = 0; i < ND + NS; i++) {
        double a = i < ND ? dv[i] : mkd(dspecial[i - ND]);
        /* fround.d in every rounding mode: invalid is the only flag it may
         * raise, whatever the value does. */
        MEASURE_RD(a, x4, 0);            /* rne */
        MEASURE_RD(a, x4, 1);            /* rtz -- what trunc wants */
        MEASURE_RD(a, x4, 2);            /* rdn -- floor */
        MEASURE_RD(a, x4, 3);            /* rup -- ceil */
        MEASURE_RD(a, x4, 4);            /* rmm -- round, ties away from zero */
        /* froundnx.d: the same values, and inexact wherever it applies. */
        MEASURE_RD(a, x5, 0);
        MEASURE_RD(a, x5, 1);
        MEASURE_RD(a, x5, 2);
        MEASURE_RD(a, x5, 3);
        MEASURE_RD(a, x5, 4);
    }
    for (int i = 0; i < NF; i++) {
        MEASURE_RS(fv[i], x4, 0);
        MEASURE_RS(fv[i], x4, 1);
        MEASURE_RS(fv[i], x5, 0);
        MEASURE_RS(fv[i], x5, 1);
    }
    rest_of_zfa();

    rv_write(1, out, (u64)n * 8);
    return 0;
}
