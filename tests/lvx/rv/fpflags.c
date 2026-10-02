/*
 * The IEEE exception flags of RV64F and RV64D, against Spike.
 *
 * `fp.c' compares results against the HOST FPU, which has no fflags to
 * compare -- so it proves the values and says nothing about the flags.  That
 * was the weakest point of the F/D port: every body was rewritten from KV4's
 * "return a value, commit the flags separately" shape into LVX's flag-tuple
 * one, and the flag NAMES and their ORDER were re-derived per helper.  A
 * tuple element in the wrong position is invisible to any test that only
 * looks at results.
 *
 * Spike has the flags, so this one is ISS-against-Spike and emits a PAIR per
 * operation: the raw result bits and the fflags the operation raised.  Raw,
 * not canonicalised -- both sides implement RISC-V, so NaN payloads must agree
 * too, which is a stronger check than fp.c can make against x86.
 *
 * The inputs are chosen for the flags rather than for the values, and the two
 * that matter most are the ones fp.c did not have at all: a QUIET NaN and a
 * SIGNALLING NaN.  They are what separates the quiet comparison (FEQ, which
 * raises nothing on a quiet NaN) from the signalling ones (FLT, FLE, which
 * raise NV on any NaN) -- the distinction this port had to add a bit to
 * floatcomp's predicate code for, and which nothing else here would exercise.
 */
typedef unsigned long u64;
typedef unsigned int  u32;

long rv_write(int fd, const void *buf, u64 n);

static u64 out[16384];
static int n;

static void putd(double d) { u64 b; __builtin_memcpy(&b, &d, 8); out[n++] = b; }
static void putf(float f)  { u32 b; __builtin_memcpy(&b, &f, 4); out[n++] = b; }
static void putu(u64 v)    { out[n++] = v; }

static double
mkd(u64 bits)
{
    double d; __builtin_memcpy(&d, &bits, 8); return d;
}

static float
mkf(u32 bits)
{
    float f; __builtin_memcpy(&f, &bits, 4); return f;
}

/* Clearing and reading fflags.  The operation must land BETWEEN the two, and
 * the compiler has no model of the FP status register, so the ordering is
 * forced rather than hoped for: the clear carries a memory clobber (and every
 * operand below is volatile, so no load crosses it), and the read takes the
 * operation's own result as an INPUT, so the operation cannot sink past it. */
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

static inline u64
flags_after_i(u64 keep)
{
    u64 f;
    __asm__ volatile ("csrr %0, fflags" : "=r"(f) : "r"(keep) : "memory");
    return f;
}

#define MEASURE_D(expr) do {                            \
    double r_;                                          \
    flags_clear();                                      \
    r_ = (expr);                                        \
    u64 f_ = flags_after_d(r_);                          \
    putd(r_); putu(f_);                                 \
} while (0)

#define MEASURE_F(expr) do {                            \
    float r_;                                           \
    flags_clear();                                      \
    r_ = (expr);                                        \
    u64 f_ = flags_after_d((double)r_);                  \
    putf(r_); putu(f_);                                 \
} while (0)

#define MEASURE_I(expr) do {                            \
    u64 r_;                                             \
    flags_clear();                                      \
    r_ = (u64)(expr);                                   \
    u64 f_ = flags_after_i(r_);                          \
    putu(r_); putu(f_);                                 \
} while (0)

/* Doubles picked for the flag sets, not the values: both NaNs, both
 * infinities, both zeros, a subnormal, the largest finite (so a product
 * overflows), the smallest normal (so a quotient underflows), and a value
 * whose conversion to an integer is out of range. */
static volatile double dv[] = {
    0.0, -0.0, 1.0, -1.0, 3.0,
    1e308, 1.7976931348623157e308,       /* squaring these overflows */
    2.2250738585072014e-308,             /* smallest normal: /2 is subnormal */
    4.9406564584124654e-324,             /* subnormal */
    1e300,
};
#define ND ((int)(sizeof dv / sizeof dv[0]))

/* The four specials, as bit patterns, so the signalling NaN really is one and
 * is not quieted by the compiler on its way into the array. */
static const u64 dspecial[] = {
    0x7ff0000000000000UL,                /* +inf */
    0xfff0000000000000UL,                /* -inf */
    0x7ff8000000000000UL,                /* quiet NaN (canonical) */
    0x7ff4000000000000UL,                /* SIGNALLING NaN */
};
#define NS ((int)(sizeof dspecial / sizeof dspecial[0]))

static volatile float fv[] = {
    0.0f, -1.0f, 3.0f, 1e38f, 3.4028234663852886e38f, 1.1754943508e-38f,
};
#define NF ((int)(sizeof fv / sizeof fv[0]))
static const u32 fspecial[] = {
    0x7f800000u, 0xff800000u, 0x7fc00000u, 0x7fa00000u, /* +inf -inf qNaN sNaN */
};
#define NFS ((int)(sizeof fspecial / sizeof fspecial[0]))

int
main(void)
{
    /* --- double arithmetic over every ordered pair, finite and special --- */
    for (int i = 0; i < ND + NS; i++)
        for (int j = 0; j < ND + NS; j++) {
            double a = i < ND ? dv[i] : mkd(dspecial[i - ND]);
            double b = j < ND ? dv[j] : mkd(dspecial[j - ND]);
            MEASURE_D(a + b);
            MEASURE_D(a - b);
            MEASURE_D(a * b);
            MEASURE_D(a / b);
            MEASURE_D(__builtin_fma(a, b, a));
        }

    /* --- the unary double operations --- */
    for (int i = 0; i < ND + NS; i++) {
        double a = i < ND ? dv[i] : mkd(dspecial[i - ND]);
        MEASURE_D(__builtin_sqrt(a));           /* NV for a negative operand */
        MEASURE_D(-a);                          /* fsgnjn: raises nothing */
        MEASURE_D(__builtin_fabs(a));           /* fsgnjx: raises nothing */
        MEASURE_I((long)a);                     /* NV out of range, NX inexact */
        MEASURE_I((int)a);
        MEASURE_I((unsigned long)a);
        MEASURE_I((unsigned)a);
        MEASURE_F((float)a);                    /* fcvt.s.d: OF/UF/NX */
    }

    /* --- min and max: NV only for a signalling NaN --- */
    for (int i = 0; i < ND + NS; i++)
        for (int j = 0; j < ND + NS; j++) {
            double a = i < ND ? dv[i] : mkd(dspecial[i - ND]);
            double b = j < ND ? dv[j] : mkd(dspecial[j - ND]);
            double r1, r2;
            flags_clear();
            __asm__ volatile ("fmin.d %0, %1, %2" : "=f"(r1) : "f"(a), "f"(b));
            u64 f1 = flags_after_d(r1);
            putd(r1); putu(f1);
            flags_clear();
            __asm__ volatile ("fmax.d %0, %1, %2" : "=f"(r2) : "f"(a), "f"(b));
            u64 f2 = flags_after_d(r2);
            putd(r2); putu(f2);
        }

    /* --- the compares.  THE point of this test: feq is quiet and raises
     *     nothing on a quiet NaN, while flt and fle are signalling and raise
     *     NV on any NaN at all.  Only a NaN operand tells them apart, and
     *     only a quiet one tells the two families apart. --- */
    for (int i = 0; i < ND + NS; i++)
        for (int j = 0; j < ND + NS; j++) {
            double a = i < ND ? dv[i] : mkd(dspecial[i - ND]);
            double b = j < ND ? dv[j] : mkd(dspecial[j - ND]);
            MEASURE_I(a == b);                  /* feq.d  -- quiet */
            MEASURE_I(a <  b);                  /* flt.d  -- signalling */
            MEASURE_I(a <= b);                  /* fle.d  -- signalling */
            MEASURE_I(__builtin_isnan(a) ? 1 : 0);
        }

    /* --- fclass raises nothing, whatever it is handed --- */
    for (int i = 0; i < ND + NS; i++) {
        double a = i < ND ? dv[i] : mkd(dspecial[i - ND]);
        u64 c;
        flags_clear();
        __asm__ volatile ("fclass.d %0, %1" : "=r"(c) : "f"(a));
        putu(c); putu(flags_after_i(c));
    }

    /* --- the same for single precision --- */
    for (int i = 0; i < NF + NFS; i++)
        for (int j = 0; j < NF + NFS; j++) {
            float a = i < NF ? fv[i] : mkf(fspecial[i - NF]);
            float b = j < NF ? fv[j] : mkf(fspecial[j - NF]);
            MEASURE_F(a + b);
            MEASURE_F(a - b);
            MEASURE_F(a * b);
            MEASURE_F(a / b);
            MEASURE_I(a == b);
            MEASURE_I(a < b);
            MEASURE_I(a <= b);
        }
    for (int i = 0; i < NF + NFS; i++) {
        float a = i < NF ? fv[i] : mkf(fspecial[i - NF]);
        MEASURE_F(__builtin_sqrtf(a));
        MEASURE_D((double)a);                   /* fcvt.d.s: NV only for sNaN */
        MEASURE_I((long)a);
        MEASURE_I((unsigned long)a);
    }

    /* --- the rounding modes, since RM reaches the helper as an argument and a
     *     wrong one is invisible at round-to-nearest --- */
    {
        volatile double x = 2.5, y = -2.5, z = 1.0, w = 3.0;
        for (int rm = 0; rm < 5; rm++) {
            u64 r;
            flags_clear();
            switch (rm) {
              case 0: __asm__ volatile ("fcvt.l.d %0, %1, rne" : "=r"(r) : "f"(x)); break;
              case 1: __asm__ volatile ("fcvt.l.d %0, %1, rtz" : "=r"(r) : "f"(x)); break;
              case 2: __asm__ volatile ("fcvt.l.d %0, %1, rdn" : "=r"(r) : "f"(x)); break;
              case 3: __asm__ volatile ("fcvt.l.d %0, %1, rup" : "=r"(r) : "f"(x)); break;
              default: __asm__ volatile ("fcvt.l.d %0, %1, rmm" : "=r"(r) : "f"(x)); break;
            }
            putu(r); putu(flags_after_i(r));
            flags_clear();
            switch (rm) {
              case 0: __asm__ volatile ("fcvt.l.d %0, %1, rne" : "=r"(r) : "f"(y)); break;
              case 1: __asm__ volatile ("fcvt.l.d %0, %1, rtz" : "=r"(r) : "f"(y)); break;
              case 2: __asm__ volatile ("fcvt.l.d %0, %1, rdn" : "=r"(r) : "f"(y)); break;
              case 3: __asm__ volatile ("fcvt.l.d %0, %1, rup" : "=r"(r) : "f"(y)); break;
              default: __asm__ volatile ("fcvt.l.d %0, %1, rmm" : "=r"(r) : "f"(y)); break;
            }
            putu(r); putu(flags_after_i(r));
            double q;
            flags_clear();
            switch (rm) {
              case 0: __asm__ volatile ("fdiv.d %0, %1, %2, rne" : "=f"(q) : "f"(z), "f"(w)); break;
              case 1: __asm__ volatile ("fdiv.d %0, %1, %2, rtz" : "=f"(q) : "f"(z), "f"(w)); break;
              case 2: __asm__ volatile ("fdiv.d %0, %1, %2, rdn" : "=f"(q) : "f"(z), "f"(w)); break;
              case 3: __asm__ volatile ("fdiv.d %0, %1, %2, rup" : "=f"(q) : "f"(z), "f"(w)); break;
              default: __asm__ volatile ("fdiv.d %0, %1, %2, rmm" : "=f"(q) : "f"(z), "f"(w)); break;
            }
            putd(q); putu(flags_after_d(q));
        }
    }

    rv_write(1, out, (u64)n * 8);
    return 0;
}
