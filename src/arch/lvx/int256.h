/*
 * LVX ISA support for gem5 — int256_t value type.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Layout and function signatures mirror Kalray BSL Int256.h (so the
 * MDS-generated code compiles and links against it); implementations are our
 * own. int256_t is a union of lane views — the generated bodies read/write it as
 * .bytes/.hwords/.words/.dwords/.qwords. Only the integer lanes are provided
 * (the generated LVX bodies never touch the float lanes; float ops go through
 * HELPERs).
 *
 * DRAFT: arithmetic is correct for the low 64/128 bits (scalar LVX); full
 * 256-bit / SIMD-lane semantics are Phase 3 (marked TODO).
 *
 * Plain C (included by behavior.c, compiled as C).
 */
#ifndef __ARCH_LVX_INT256_H__
#define __ARCH_LVX_INT256_H__

#include <stdbool.h>
#include <stdint.h>

typedef unsigned __int128 uint128_t;
typedef signed   __int128 int128_t;   /* the unboxed Behavior C widens signed to int128_t */

typedef union int256_t
{
    uint128_t qwords[2];
    uint64_t  dwords[4];
    uint32_t  words[8];
    uint16_t  hwords[16];
    uint8_t   bytes[32];
} int256_t;

static inline int256_t
int256_make(uint64_t d0, uint64_t d1, uint64_t d2, uint64_t d3)
{
    int256_t r;
    r.dwords[0] = d0; r.dwords[1] = d1; r.dwords[2] = d2; r.dwords[3] = d3;
    return r;
}

/* --- constructors --- */
static inline int256_t int256_fromUInt64(uint64_t v) { return int256_make(v, 0, 0, 0); }
static inline int256_t int256_fromUInt32(unsigned v) { return int256_fromUInt64((uint32_t)v); }
static inline int256_t int256_fromUInt16(unsigned v) { return int256_fromUInt64((uint16_t)v); }
static inline int256_t int256_fromUInt8(unsigned v)  { return int256_fromUInt64((uint8_t)v); }
static inline int256_t int256_fromBool(unsigned v)   { return int256_fromUInt64(v ? 1 : 0); }
static inline int256_t
int256_fromInt64(int64_t v)
{
    uint64_t f = (v < 0) ? ~0ULL : 0ULL;
    return int256_make((uint64_t)v, f, f, f);
}
static inline int256_t int256_fromInt32(int v) { return int256_fromInt64((int32_t)v); }
static inline int256_t int256_fromInt16(int v) { return int256_fromInt64((int16_t)v); }
static inline int256_t int256_fromInt8(int v)  { return int256_fromInt64((int8_t)v); }
static inline int256_t
int256_fromUInt128(uint128_t v)
{
    return int256_make((uint64_t)v, (uint64_t)(v >> 64), 0, 0);
}

#define int256_zero int256_fromUInt64(0ULL)

/* --- extractors --- */
static inline bool     int256_toBool(int256_t a) { return (a.dwords[0] | a.dwords[1] | a.dwords[2] | a.dwords[3]) != 0; }
static inline uint32_t int256_toUInt32(int256_t a) { return (uint32_t)a.dwords[0]; }
static inline int32_t  int256_toInt32(int256_t a) { return (int32_t)a.dwords[0]; }
static inline uint64_t int256_toUInt64(int256_t a) { return a.dwords[0]; }
static inline int64_t  int256_toInt64(int256_t a) { return (int64_t)a.dwords[0]; }

/* --- bitwise --- */
static inline int256_t
int256_and(int256_t a, int256_t b)
{
    for (int i = 0; i < 4; i++) a.dwords[i] &= b.dwords[i];
    return a;
}
static inline int256_t
int256_or(int256_t a, int256_t b)
{
    for (int i = 0; i < 4; i++) a.dwords[i] |= b.dwords[i];
    return a;
}
static inline int256_t
int256_xor(int256_t a, int256_t b)
{
    for (int i = 0; i < 4; i++) a.dwords[i] ^= b.dwords[i];
    return a;
}
static inline int256_t
int256_not(int256_t a)
{
    for (int i = 0; i < 4; i++) a.dwords[i] = ~a.dwords[i];
    return a;
}

/* --- arithmetic (256-bit ripple for add/sub; low-128 for mul; low-64 div/mod) --- */
static inline int256_t
int256_add(int256_t a, int256_t b)
{
    int256_t r; unsigned __int128 c = 0;
    for (int i = 0; i < 4; i++) {
        unsigned __int128 s = (unsigned __int128)a.dwords[i] + b.dwords[i] + c;
        r.dwords[i] = (uint64_t)s; c = s >> 64;
    }
    return r;
}
static inline int256_t int256_neg(int256_t a) { return int256_add(int256_not(a), int256_fromUInt64(1)); }
static inline int256_t int256_sub(int256_t a, int256_t b) { return int256_add(a, int256_neg(b)); }
static inline int256_t int256_abs(int256_t a) { return ((int64_t)a.dwords[3] < 0) ? int256_neg(a) : a; } // TODO: width
static inline int256_t int256_mul(int256_t a, int256_t b) { return int256_fromUInt128(a.qwords[0] * b.qwords[0]); } // low 128 of product; operands pre-extended by sx/zx so signed/unsigned/mixed high halves are correct. TODO: full 256
static inline int256_t int256_div(int256_t a, int256_t b) { return int256_fromInt64(b.dwords[0] ? (int64_t)a.dwords[0] / (int64_t)b.dwords[0] : 0); }
static inline int256_t int256_mod(int256_t a, int256_t b) { return int256_fromInt64(b.dwords[0] ? (int64_t)a.dwords[0] % (int64_t)b.dwords[0] : 0); }
static inline int256_t int256_divu(int256_t a, int256_t b) { return int256_fromUInt64(b.dwords[0] ? a.dwords[0] / b.dwords[0] : 0); }
static inline int256_t int256_modu(int256_t a, int256_t b) { return int256_fromUInt64(b.dwords[0] ? a.dwords[0] % b.dwords[0] : 0); }

/* --- shifts (low-64 scalar; TODO full 256-bit) --- */
static inline int256_t int256_shl_(int256_t a, unsigned s) { return int256_fromUInt64(s < 64 ? a.dwords[0] << s : 0); }
static inline int256_t int256_shru_(int256_t a, unsigned s) { return int256_fromUInt64(s < 64 ? a.dwords[0] >> s : 0); }
static inline int256_t int256_shr_(int256_t a, unsigned s) { return int256_fromInt64(s < 64 ? (int64_t)a.dwords[0] >> s : ((int64_t)a.dwords[0] >> 63)); }
static inline int256_t int256_shl(int256_t a, int256_t s) { return int256_shl_(a, (unsigned)s.dwords[0]); }
static inline int256_t int256_shru(int256_t a, int256_t s) { return int256_shru_(a, (unsigned)s.dwords[0]); }
static inline int256_t int256_shr(int256_t a, int256_t s) { return int256_shr_(a, (unsigned)s.dwords[0]); }

/* --- width-parameterized: zero/sign extend to `width` low bits --- */
static inline int256_t
int256_zx(int256_t a, unsigned width)
{
    for (int k = 0; k < 4; k++) {
        unsigned lo = k * 64;
        if (lo >= width) a.dwords[k] = 0;
        else if (lo + 64 <= width) { }
        else a.dwords[k] &= (((uint64_t)1 << (width - lo)) - 1);
    }
    return a;
}
static inline int256_t
int256_sx(int256_t a, unsigned width)
{
    if (width == 0 || width >= 256) return a;
    int256_t r = int256_zx(a, width);
    unsigned s = width - 1;
    if ((r.dwords[s / 64] >> (s % 64)) & 1) {
        for (int k = 0; k < 4; k++) {
            unsigned lo = k * 64;
            if (lo + 64 <= width) { }
            else if (lo >= width) r.dwords[k] = ~0ULL;
            else r.dwords[k] |= ~(((uint64_t)1 << (width - lo)) - 1);
        }
    }
    return r;
}
static inline int256_t int256_sat(int256_t a, unsigned width) { return int256_sx(a, width); }  // TODO: saturate, not truncate
static inline int256_t int256_satu(int256_t a, unsigned width) { return int256_zx(a, width); } // TODO

/* --- bit counts within `width` (return unsigned) --- */
static inline unsigned
int256_clz(int256_t a, unsigned width)
{
    for (unsigned i = 0; i < width && i < 64; i++)
        if ((a.dwords[0] >> (width - 1 - i)) & 1) return i;
    return width; // TODO: >64-bit widths
}
static inline unsigned
int256_ctz(int256_t a, unsigned width)
{
    for (unsigned i = 0; i < width && i < 64; i++)
        if ((a.dwords[0] >> i) & 1) return i;
    return width;
}
static inline unsigned int256_cls(int256_t a, unsigned width) { return int256_clz(a, width); } // TODO: count leading sign
static inline unsigned
int256_cbs(int256_t a, unsigned width)
{
    unsigned n = 0;
    for (unsigned i = 0; i < width && i < 64; i++) n += (a.dwords[0] >> i) & 1;
    return n;
}

/* --- rotates within `width` (low-64; TODO full width) --- */
static inline int256_t
int256_rol(unsigned width, int256_t a, int256_t rot)
{
    unsigned r = width ? (unsigned)(rot.dwords[0] % width) : 0;
    uint64_t m = (width >= 64) ? ~0ULL : (((uint64_t)1 << width) - 1);
    uint64_t v = a.dwords[0] & m;
    uint64_t o = r ? ((v << r) | (v >> (width - r))) & m : v;
    return int256_fromUInt64(o);
}
static inline int256_t
int256_ror(unsigned width, int256_t a, int256_t rot)
{
    unsigned r = width ? (unsigned)(rot.dwords[0] % width) : 0;
    return int256_rol(width, a, int256_fromUInt64(r ? width - r : 0));
}

/* --- byte/element swap within `slice`-bit granularity (TODO full) --- */
static inline int256_t
int256_swap(int256_t a, unsigned slice)
{
    (void)slice;
    return int256_fromUInt64(__builtin_bswap64(a.dwords[0])); // TODO: honor slice / full width
}

/* --- compares: signed / unsigned -> {-1,0,1} on low 64 (TODO full 256) --- */
static inline int
int256_cmp(int256_t a, int256_t b)
{
    int64_t x = (int64_t)a.dwords[0], y = (int64_t)b.dwords[0];
    return (x > y) - (x < y);
}
static inline int
int256_cmpu(int256_t a, int256_t b)
{
    return (a.dwords[0] > b.dwords[0]) - (a.dwords[0] < b.dwords[0]);
}

#endif // __ARCH_LVX_INT256_H__
