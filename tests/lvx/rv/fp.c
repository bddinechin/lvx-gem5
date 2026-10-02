/*
 * RV64F and RV64D, checked against the host's own FPU.
 *
 * The port of F and D is the only part of RV64G that could not be imported as
 * it stood: KV4 returned a value from each FP helper and swept the IEEE flags
 * up afterwards, while LVX's helpers return the flags alongside the value and
 * the description distributes them into CS.  Every one of those ~60 bodies was
 * rewritten, so "it decodes" is worth very little and "it computes the same
 * double as x86" is worth a lot.
 *
 * So this program writes RAW BIT PATTERNS, not text: it is built twice, once
 * for RV64 and once for the host, and the two outputs are compared byte for
 * byte (`make diff').  Bit patterns rather than values because that is what
 * makes the comparison catch a signed zero, a NaN payload, a result rounded
 * the wrong way in the last place, and a float that was not NaN-boxed -- none
 * of which a printed value, or a program checking its own answers against
 * constants someone typed, reliably notices.
 *
 * x86-64 and RISC-V are both IEEE-754 binary32/binary64 with round-to-nearest
 * as the default, so the comparison is legitimate: where they would genuinely
 * differ the C is written to avoid asking (no x87 excess precision -- SSE2 on
 * x86-64 -- and no fused multiply-add contraction, which -ffp-contract=off
 * settles on both sides).
 */
typedef unsigned long u64;
typedef unsigned int  u32;

/* min and max are the one pair gcc 10 will not inline to an instruction on
 * RISC-V, so they are asked for by name.  C's fmin/fmax ARE RISC-V's FMIN/FMAX:
 * 754-2008 minNum, returning the NUMERIC operand when one is NaN -- which is
 * also what LVX's FMINND/FMAXND are and what makes this comparison the right
 * one (see the min/max NaN split in lvx-csw/CLAUDE.md). */
#ifdef HOST
#include <math.h>
static int signbit_d(double);
static int signbit_f(float);
static double dmin(double a, double b)
{ if (a == 0 && b == 0) return signbit_d(a) ? a : b; return fmin(a, b); }
static double dmax(double a, double b)
{ if (a == 0 && b == 0) return signbit_d(a) ? b : a; return fmax(a, b); }
static float fmin_(float a, float b)
{ if (a == 0 && b == 0) return signbit_f(a) ? a : b; return fminf(a, b); }
static float fmax_(float a, float b)
{ if (a == 0 && b == 0) return signbit_f(a) ? b : a; return fmaxf(a, b); }
#else
static double dmin(double a, double b)
{ double r; __asm__ ("fmin.d %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static double dmax(double a, double b)
{ double r; __asm__ ("fmax.d %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static float fmin_(float a, float b)
{ float r; __asm__ ("fmin.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
static float fmax_(float a, float b)
{ float r; __asm__ ("fmax.s %0, %1, %2" : "=f"(r) : "f"(a), "f"(b)); return r; }
#endif

/* The three places x86 and RISC-V genuinely differ, all of them places where C
 * leaves the answer undefined or unspecified and RISC-V SPECIFIES it.  The host
 * is the oracle for everything else; for these, the host side is made to compute
 * RISC-V's rule, so the comparison stays bit-exact instead of being weakened by
 * an exclusion list.  The first run of this test found all three and nothing
 * else: 41 words of 5138, every one of them the ISS being right.
 *
 * 1. A produced NaN's SIGN.  x86's SSE hands back a negative default NaN
 *    (0xfff8...) where RISC-V's canonical NaN is positive (0x7ff8...).  Both
 *    sides canonicalise, so "did this produce a NaN" is still compared and the
 *    sign is not.
 * 2. float -> integer, OUT OF RANGE OR NaN.  Undefined in C; x86 returns the
 *    "integer indefinite" 0x8000000000000000 and wraps a negative value into an
 *    unsigned one.  RISC-V saturates: NaN and too-large to the maximum, too
 *    negative to the minimum, and anything negative to 0 when the target is
 *    unsigned.
 * 3. min/max of +0 and -0.  Unspecified in C; RISC-V defines min to return -0
 *    and max +0.
 */
static u64 out[8192];
static int n;

#define CANON_D 0x7ff8000000000000UL
#define CANON_F 0x7fc00000u

static void
putd(double d)
{
    u64 b; __builtin_memcpy(&b, &d, 8);
    if (((b >> 52) & 0x7ff) == 0x7ff && (b & 0xfffffffffffffUL))
        b = CANON_D;
    out[n++] = b;
}

static void
putf(float f)
{
    u32 b; __builtin_memcpy(&b, &f, 4);
    if (((b >> 23) & 0xff) == 0xff && (b & 0x7fffffu))
        b = CANON_F;
    out[n++] = b;
}

static void putu(u64 v) { out[n++] = v; }

#ifdef HOST
/* RISC-V's saturating float-to-integer, which is what the instruction does and
 * what C does not promise. */
#define D2I(name, type, lo, hi, neg0)                                    \
static u64 name(double d)                                                \
{                                                                        \
    if (d != d) return (u64)(type)(hi);          /* NaN -> maximum */    \
    if (d >= (double)(hi)) return (u64)(type)(hi);                       \
    if (d <= (double)(lo)) return (u64)(type)(lo);                       \
    if (neg0 && d < 0) return 0;                                         \
    return (u64)(type)d;                                                 \
}
D2I(d2l,  long,          (-9223372036854775807L - 1), 9223372036854775807L,  0)
D2I(d2i,  int,           (-2147483647 - 1),           2147483647,            0)
D2I(d2ul, unsigned long, 0,                           18446744073709551615UL, 1)
D2I(d2u,  unsigned,      0,                           4294967295U,            1)
static u64 f2l(float f)  { return d2l((double)f); }
static u64 f2ul(float f) { return d2ul((double)f); }
/* and RISC-V's rule for the zeros */
static int signbit_d(double d) { u64 b; __builtin_memcpy(&b, &d, 8); return (int)(b >> 63); }
static int signbit_f(float f)  { u32 b; __builtin_memcpy(&b, &f, 4); return (int)(b >> 31); }
#else
static u64 d2l(double d)  { return (u64)(long)d; }
static u64 d2i(double d)  { return (u64)(int)d; }
static u64 d2ul(double d) { return (u64)(unsigned long)d; }
static u64 d2u(double d)  { return (u64)(unsigned)d; }
static u64 f2l(float f)   { return (u64)(long)f; }
static u64 f2ul(float f)  { return (u64)(unsigned long)f; }
#endif

/* Inputs chosen so the operations reach their corners: a subnormal, a value
 * whose square overflows, one whose reciprocal underflows, a negative zero,
 * both infinities and both NaNs. */
static volatile double dv[] = {
     0.0, -0.0, 1.0, -1.0, 0.5, 2.0, 3.0, -3.0,
     1e-308, 4.9406564584124654e-324, 1e308, 1.7976931348623157e308,
     0.1, 1.0/3.0, 123456789.123456789, -0.000244140625,
     1e-320, 3.0e300, 1.5e-300, 9007199254740993.0,
};
static volatile float fv[] = {
     0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 3.0f, -3.0f,
     1e-38f, 1.4012984643e-45f, 1e38f, 3.4028234663852886e38f,
     0.1f, 1.0f/3.0f, 16777217.0f, -0.000244140625f,
};
#define ND ((int)(sizeof dv / sizeof dv[0]))
#define NF ((int)(sizeof fv / sizeof fv[0]))

static void
run(void)
{
    /* double: the five arithmetic operations over every ordered pair, which is
     * where an operand swap in FSUB or FDIV shows up immediately. */
    for (int i = 0; i < ND; i++)
        for (int j = 0; j < ND; j++) {
            putd(dv[i] + dv[j]);
            putd(dv[i] - dv[j]);
            putd(dv[i] * dv[j]);
            putd(dv[i] / dv[j]);
        }
    for (int i = 0; i < ND; i++) {
        putd(__builtin_sqrt(dv[i]));
        putd(-dv[i]);                       /* fsgnjn */
        putd(__builtin_fabs(dv[i]));        /* fsgnjx with itself */
        putd(__builtin_copysign(dv[i], -1.0));   /* fsgnj */
        putu((u64)__builtin_isnan(dv[i]));  /* feq against itself */
    }
    /* the fused multiply-adds, all four sign combinations */
    for (int i = 0; i < ND; i++) {
        double a = dv[i], b = dv[(i + 1) % ND], c = dv[(i + 2) % ND];
        putd(__builtin_fma(a, b, c));
        putd(__builtin_fma(a, b, -c));
        putd(__builtin_fma(-a, b, c));
        putd(__builtin_fma(-a, b, -c));
    }
    /* min and max: RISC-V returns the NUMERIC operand, which is the one place
     * the two IEEE families disagree and ordinary values never show it */
    for (int i = 0; i < ND; i++)
        for (int j = 0; j < ND; j++) {
            putd(dmin(dv[i], dv[j]));
            putd(dmax(dv[i], dv[j]));
        }
    /* the compares, as the three predicates RISC-V has */
    for (int i = 0; i < ND; i++)
        for (int j = 0; j < ND; j++) {
            putu((u64)(dv[i] == dv[j]));
            putu((u64)(dv[i] <  dv[j]));
            putu((u64)(dv[i] <= dv[j]));
        }
    /* the conversions, both directions and both signednesses, at both widths */
    for (int i = 0; i < ND; i++) {
        putu(d2l(dv[i]));
        putu(d2i(dv[i]));
        putu(d2ul(dv[i]));
        putu(d2u(dv[i]));
        putf((float)dv[i]);
    }
    for (int i = 0; i < NF; i++)
        putd((double)fv[i]);
    for (long k = -5; k <= 5; k++) {
        putd((double)k); putf((float)k);
        putd((double)(int)k); putf((float)(int)k);
        putd((double)(unsigned long)k); putf((float)(unsigned long)k);
        putd((double)(unsigned)k); putf((float)(unsigned)k);
    }

    /* float: the same arithmetic, where the result must also be NaN-boxed --
     * which this does not see directly, but a float that is not boxed stops
     * being a float to the next instruction, and the sum below is that. */
    for (int i = 0; i < NF; i++)
        for (int j = 0; j < NF; j++) {
            putf(fv[i] + fv[j]);
            putf(fv[i] - fv[j]);
            putf(fv[i] * fv[j]);
            putf(fv[i] / fv[j]);
        }
    for (int i = 0; i < NF; i++) {
        putf(__builtin_sqrtf(fv[i]));
        putf(fmin_(fv[i], fv[(i + 1) % NF]));
        putf(fmax_(fv[i], fv[(i + 1) % NF]));
        putf(__builtin_fmaf(fv[i], fv[(i + 1) % NF], fv[(i + 2) % NF]));
        putf(__builtin_copysignf(fv[i], -1.0f));
        putu(f2l(fv[i]));
        putu(f2ul(fv[i]));
        putu((u64)(fv[i] < fv[(i + 1) % NF]));
    }
    /* a chain, so a float that lost its NaN-boxing poisons everything after */
    {
        float acc = 1.0f;
        for (int i = 0; i < NF; i++) acc = acc * 1.5f + fv[i];
        putf(acc);
        double dacc = 1.0;
        for (int i = 0; i < ND; i++) dacc = dacc * 0.5 + dv[i];
        putd(dacc);
    }
}

#ifdef HOST
#include <unistd.h>
int main(void) { run(); return (int)write(1, out, (unsigned long)n * 8) < 0; }
#else
long rv_write(int fd, const void *buf, unsigned long n);
int main(void) { run(); rv_write(1, out, (unsigned long)n * 8); return 0; }
#endif
