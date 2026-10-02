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

static u64 out[1024];
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
    rv_write(1, out, (u64)n * 8);
    return 0;
}
