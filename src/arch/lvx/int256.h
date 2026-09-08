/*
 * int256_t — LVX-owned 256-bit integer, implemented from scratch over __int128.
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2026 Liesme Tech.
 *
 * This replaces the Kalray BSL Int256_ that the BE/LAO differential test used to
 * fetch from kvx-lao (see DOC/GEM5-independence-plan.md, Phase 0). It is the
 * oracle the generated Behavior C runs against: a union of lane views plus the
 * int256_* operator library the generator emits calls to. Only the integer lanes
 * exist — the generated LVX bodies never touch float lanes (float ops go through
 * HELPERs) — plus the 16 single-bit views the lvx_v2 bit-slice code reads.
 *
 * The value semantics deliberately match what the LVX Behavior operators mean
 * (Behavior.md): where an operator has a non-obvious convention (sat/satu clamp,
 * swap group-reversal, cls count-leading-sign, rol/ror modulo width), the
 * behaviour is what the ISA description assumes, cross-checked by the differential
 * test against the previous (Kalray) oracle on every opcode.
 *
 * Plain C (compiled as C; the generated bodies use `val` as a parameter name).
 */
#ifndef LVX_INT256_H
#define LVX_INT256_H

#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

typedef unsigned __int128 uint128_t;
typedef signed   __int128 int128_t;

typedef union int256_t
{
    uint128_t qwords[2];
    uint64_t  dwords[4];
    uint32_t  words[8];
    uint16_t  hwords[16];
    uint8_t   bytes[32];
    struct {                       /* low-16 single-bit views (lvx_v2 bit slices) */
        unsigned bits_0:1,  bits_1:1,  bits_2:1,  bits_3:1,
                 bits_4:1,  bits_5:1,  bits_6:1,  bits_7:1,
                 bits_8:1,  bits_9:1,  bits_10:1, bits_11:1,
                 bits_12:1, bits_13:1, bits_14:1, bits_15:1;
    };
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
    int256_t r; r.qwords[0] = v; r.qwords[1] = 0; return r;
}

#define int256_zero int256_fromUInt64(0ULL)

/* --- extractors --- */
static inline bool     int256_toBool(int256_t a) { return (a.dwords[0] | a.dwords[1] | a.dwords[2] | a.dwords[3]) != 0; }
static inline uint32_t int256_toUInt32(int256_t a) { return (uint32_t)a.dwords[0]; }
static inline int32_t  int256_toInt32(int256_t a) { return (int32_t)a.dwords[0]; }
static inline uint64_t int256_toUInt64(int256_t a) { return a.dwords[0]; }
static inline int64_t  int256_toInt64(int256_t a) { return (int64_t)a.dwords[0]; }

/* --- bitwise --- */
static inline int256_t int256_and(int256_t a, int256_t b) { for (int i=0;i<4;i++) a.dwords[i] &= b.dwords[i]; return a; }
static inline int256_t int256_or (int256_t a, int256_t b) { for (int i=0;i<4;i++) a.dwords[i] |= b.dwords[i]; return a; }
static inline int256_t int256_xor(int256_t a, int256_t b) { for (int i=0;i<4;i++) a.dwords[i] ^= b.dwords[i]; return a; }
static inline int256_t int256_not(int256_t a)             { for (int i=0;i<4;i++) a.dwords[i] = ~a.dwords[i]; return a; }

/* --- add/sub/neg/abs (full 256-bit) --- */
static inline int256_t
int256_add(int256_t a, int256_t b)
{
    int256_t r;
    r.qwords[0] = a.qwords[0] + b.qwords[0];
    uint128_t carry = (r.qwords[0] < a.qwords[0]) ? 1 : 0;
    r.qwords[1] = a.qwords[1] + b.qwords[1] + carry;
    return r;
}
static inline int256_t int256_neg(int256_t a) { return int256_add(int256_not(a), int256_fromUInt64(1)); }
static inline int256_t int256_sub(int256_t a, int256_t b) { return int256_add(a, int256_neg(b)); }
static inline int256_t int256_abs(int256_t a) { return (a.dwords[3] >> 63) ? int256_neg(a) : a; }

/* --- multiply: low 128 of the product (operands pre-extended by sx/zx) --- */
static inline int256_t int256_mul(int256_t a, int256_t b) { return int256_fromUInt128(a.qwords[0] * b.qwords[0]); }

/* --- divide/modulo: low-128 signed (operands are sign/zero-extended to 256) --- */
static inline int256_t
int256_div(int256_t a, int256_t b)
{
    int256_t r = int256_zero;
    int128_t q = (int128_t)a.qwords[0] / (int128_t)b.qwords[0];
    r.qwords[0] = (uint128_t)q;
    if (q < 0) r.dwords[2] = r.dwords[3] = ~0ULL;
    return r;
}
static inline int256_t
int256_rem(int256_t a, int256_t b)
{
    int256_t r = int256_zero;
    int128_t m = (int128_t)a.qwords[0] % (int128_t)b.qwords[0];
    r.qwords[0] = (uint128_t)m;
    if (m < 0) r.dwords[2] = r.dwords[3] = ~0ULL;
    return r;
}
/* Floored modulo, NOT C's %: the result takes the sign of the DIVISOR, so
 * -1 MOD 5 == 4.  That is what Behavior's MOD means and what MDS/LIB/Width.pm
 * bounds it by (|a mod b| < |b|, and not bounded by |a|).  C's % is the
 * truncated remainder that pairs with C's / -- that is int256_rem above, and
 * REM is the operator the divmod instructions use. */
static inline int256_t
int256_mod(int256_t a, int256_t b)
{
    int256_t r = int256_zero;
    int128_t d = (int128_t)b.qwords[0];
    int128_t m = (int128_t)a.qwords[0] % d;
    if (m != 0 && ((m < 0) != (d < 0))) m += d;
    r.qwords[0] = (uint128_t)m;
    if (m < 0) r.dwords[2] = r.dwords[3] = ~0ULL;
    return r;
}
static inline int256_t int256_divu(int256_t a, int256_t b) { return int256_fromUInt128(a.qwords[0] / b.qwords[0]); }
static inline int256_t int256_modu(int256_t a, int256_t b) { return int256_fromUInt128(a.qwords[0] % b.qwords[0]); }

/* --- shifts: dword-limb, full 256-bit --- */
static inline int256_t
int256_shl_(int256_t val, unsigned shift)
{
    int256_t out;
    uint64_t previous = 0;
    int i = 0, j = 0;
    assert(shift < 256);
    while (shift >= 64) { out.dwords[i] = previous; shift -= 64; i++; }
    while (i < 4) {
        out.dwords[i] = val.dwords[j] << shift;
        if (shift) { out.dwords[i] |= previous >> (64 - shift); previous = val.dwords[j]; }
        i++, j++;
    }
    return out;
}
/* shift right, filling vacated high bits with `fill` (0 = logical, 1 = arithmetic) */
static inline int256_t
int256_shrx_(int256_t val, unsigned shift, unsigned fill)
{
    int256_t out;
    uint64_t previous = 0ULL - (fill != 0);
    int i = 3, j = 3;
    assert(shift < 256);
    while (shift >= 64) { out.dwords[i] = previous; shift -= 64; i--; }
    while (i >= 0) {
        out.dwords[i] = val.dwords[j] >> shift;
        if (shift) { out.dwords[i] |= previous << (64 - shift); previous = val.dwords[j]; }
        i--, j--;
    }
    return out;
}
static inline int256_t int256_shru_(int256_t a, unsigned s) { return int256_shrx_(a, s, 0); }
static inline int256_t int256_shr_ (int256_t a, unsigned s) { return int256_shrx_(a, s, (unsigned)(a.dwords[3] >> 63)); }
static inline int256_t int256_shl (int256_t a, int256_t s) { return int256_shl_ (a, (unsigned)int256_toUInt64(s)); }
static inline int256_t int256_shru(int256_t a, int256_t s) { return int256_shru_(a, (unsigned)int256_toUInt64(s)); }
static inline int256_t int256_shr (int256_t a, int256_t s) { return int256_shr_ (a, (unsigned)int256_toUInt64(s)); }

/* --- zero/sign extend to `width` low bits (full 256-bit) --- */
static inline int256_t
int256_zx(int256_t val, unsigned width)
{
    if (width >= 256) return val;
    int256_t out = int256_make(0,0,0,0);
    unsigned full = width / 64, rem = width % 64;
    for (unsigned i = 0; i < full && i < 4; i++) out.dwords[i] = val.dwords[i];
    if (rem && full < 4) out.dwords[full] = val.dwords[full] & ((1ULL << rem) - 1);
    return out;
}
static inline int256_t
int256_sx(int256_t val, unsigned width)
{
    if (width == 0) return int256_make(0,0,0,0);
    if (width >= 256) return val;
    int256_t out;
    unsigned k = (width - 1) / 64;        /* dword holding the sign bit */
    unsigned b = (width - 1) % 64;        /* sign-bit position within it */
    for (unsigned i = 0; i < k; i++) out.dwords[i] = val.dwords[i];
    unsigned s = 63 - b;
    int64_t ext = (int64_t)(val.dwords[k] << s) >> s;   /* sign-extend within dword k */
    out.dwords[k] = (uint64_t)ext;
    uint64_t fill = (ext < 0) ? ~0ULL : 0ULL;
    for (unsigned i = k + 1; i < 4; i++) out.dwords[i] = fill;
    return out;
}

/* --- saturating clamp to a signed / unsigned `width`-bit range --- */
static inline int256_t
int256_sat(int256_t val, unsigned width)
{
    int256_t out = val;
    if (width > 4*64) ;
    else if (width > 3*64) {
        int64_t smin = -(1LL << (width - 3*64 - 1));
        int64_t smax =  (1LL << (width - 3*64 - 1)) - 1;
        if ((int64_t)val.dwords[3] < smin) out = int256_make(0,0,0,(uint64_t)smin);
        else if ((int64_t)val.dwords[3] > smax) out = int256_make(~0ULL,~0ULL,~0ULL,(uint64_t)smax);
    } else if (width > 2*64) {
        int64_t smin = -(1LL << (width - 2*64 - 1));
        int64_t smax =  (1LL << (width - 2*64 - 1)) - 1;
        if ((int64_t)val.dwords[3] < 0) {
            if ((int64_t)val.dwords[3] != -1LL || (uint64_t)val.dwords[2] < (uint64_t)smin)
                out = int256_make(0,0,(uint64_t)smin,~0ULL);
        } else {
            if ((int64_t)val.dwords[3] != 0 || (uint64_t)val.dwords[2] > (uint64_t)smax)
                out = int256_make(~0ULL,~0ULL,(uint64_t)smax,0);
        }
    } else if (width > 1*64) {
        int64_t smin = -(1LL << (width - 64 - 1));
        int64_t smax =  (1LL << (width - 64 - 1)) - 1;
        if ((int64_t)val.dwords[3] < 0) {
            if ((int64_t)val.dwords[3] != -1LL || (int64_t)val.dwords[2] != -1LL || (uint64_t)val.dwords[1] < (uint64_t)smin)
                out = int256_make(0,(uint64_t)smin,~0ULL,~0ULL);
        } else {
            if ((int64_t)val.dwords[3] != 0 || (int64_t)val.dwords[2] != 0 || (uint64_t)val.dwords[1] > (uint64_t)smax)
                out = int256_make(~0ULL,(uint64_t)smax,0,0);
        }
    } else if (width > 0) {
        int64_t smin = -(1LL << (width - 1));
        int64_t smax =  (1LL << (width - 1)) - 1;
        if ((int64_t)val.dwords[3] < 0) {
            if ((int64_t)val.dwords[3] != -1LL || (int64_t)val.dwords[2] != -1LL || (int64_t)val.dwords[1] != -1LL || (uint64_t)val.dwords[0] < (uint64_t)smin)
                out = int256_make((uint64_t)smin,~0ULL,~0ULL,~0ULL);
        } else {
            if ((int64_t)val.dwords[3] != 0 || (int64_t)val.dwords[2] != 0 || (int64_t)val.dwords[1] != 0 || (uint64_t)val.dwords[0] > (uint64_t)smax)
                out = int256_make((uint64_t)smax,0,0,0);
        }
    } else __builtin_trap();
    return out;
}
static inline int256_t
int256_satu(int256_t val, unsigned width)
{
    int256_t out = val;
    if ((int64_t)val.dwords[3] < 0) return int256_make(0,0,0,0);
    if (width >= 4*64) ;
    else if (width >= 3*64) {
        uint64_t umax = (1ULL << (width - 3*64)) - 1;
        if ((uint64_t)val.dwords[3] > umax) out = int256_make(~0ULL,~0ULL,~0ULL,umax);
    } else if (width >= 2*64) {
        uint64_t umax = (1ULL << (width - 2*64)) - 1;
        if ((uint64_t)val.dwords[3] != 0 || (uint64_t)val.dwords[2] > umax) out = int256_make(~0ULL,~0ULL,umax,0);
    } else if (width >= 64) {
        uint64_t umax = (1ULL << (width - 64)) - 1;
        if ((uint64_t)val.dwords[3] != 0 || (uint64_t)val.dwords[2] != 0 || (uint64_t)val.dwords[1] > umax) out = int256_make(~0ULL,umax,0,0);
    } else if (width > 0) {
        uint64_t umax = (1ULL << width) - 1;
        if ((uint64_t)val.dwords[3] != 0 || (uint64_t)val.dwords[2] != 0 || (uint64_t)val.dwords[1] != 0 || (uint64_t)val.dwords[0] > umax) out = int256_make(umax,0,0,0);
    } else __builtin_trap();
    return out;
}

/* --- bit counts within `width` --- */
static inline unsigned
int256_clz(int256_t val, unsigned width)
{
    unsigned clz = width;
    assert(width <= 256);
    int256_t t = int256_shl_(val, 256 - width);
    for (int i = 3; i >= 0; --i)
        if (t.dwords[i]) { clz = 64*(3-i) + __builtin_clzll(t.dwords[i]); break; }
    return clz;
}
static inline unsigned
int256_ctz(int256_t val, unsigned width)
{
    unsigned ctz = width;
    assert(width <= 256);
    for (int i = 0; i <= 3; ++i)
        if (val.dwords[i]) { ctz = 64*i + __builtin_ctzll(val.dwords[i]); if (ctz > width) ctz = width; break; }
    return ctz;
}
static inline unsigned
int256_cls(int256_t val, unsigned width)   /* count leading sign bits, minus one */
{
    assert(width <= 256);
    int256_t t = int256_shl_(val, 256 - width);
    if (t.dwords[3] >> 63) t = int256_not(t);
    t = int256_shr_(t, 256 - width);
    return int256_clz(t, width) - 1;
}
static inline unsigned
int256_cbs(int256_t val, unsigned width)   /* population count over the low `width` bits */
{
    assert(width <= 256);
    unsigned n = 0;
    for (int k = 0; k < 4 && (unsigned)(64*k) < width; ++k) {
        unsigned lo = 64*k, bits = (width - lo >= 64) ? 64 : (width - lo);
        uint64_t mask = (bits >= 64) ? ~0ULL : ((1ULL << bits) - 1);
        n += __builtin_popcountll(val.dwords[k] & mask);
    }
    return n;
}

/* --- rotates, modulo `width` --- */
static inline int256_t
int256_rol(unsigned width, int256_t val, int256_t rotate)
{
    assert(width <= 256);
    unsigned shift = (unsigned)(int256_toUInt64(rotate) % width);
    return int256_or(int256_shru(int256_zx(val, width), int256_fromUInt32(width - shift)),
                     int256_zx(int256_shl(val, int256_fromUInt32(shift)), width));
}
static inline int256_t
int256_ror(unsigned width, int256_t val, int256_t rotate)
{
    assert(width <= 256);
    unsigned shift = (unsigned)(int256_toUInt64(rotate) % width);
    return int256_or(int256_shru(int256_zx(val, width), int256_fromUInt32(shift)),
                     int256_zx(int256_shl(val, int256_fromUInt32(width - shift)), width));
}

/* --- swap adjacent `slice`-bit groups within each 64-bit dword (slice 1..32);
 *     slice==64 swaps the two dwords within each 128-bit half --- */
static inline int256_t
int256_swap(int256_t val, unsigned slice)
{
    if (slice == 64) {
        uint64_t t;
        t = val.dwords[0]; val.dwords[0] = val.dwords[1]; val.dwords[1] = t;
        t = val.dwords[2]; val.dwords[2] = val.dwords[3]; val.dwords[3] = t;
        return val;
    }
    uint64_t mask;
    switch (slice) {
        case 1:  mask = 0x5555555555555555ULL; break;
        case 2:  mask = 0x3333333333333333ULL; break;
        case 4:  mask = 0x0f0f0f0f0f0f0f0fULL; break;
        case 8:  mask = 0x00ff00ff00ff00ffULL; break;
        case 16: mask = 0x0000ffff0000ffffULL; break;
        case 32: mask = 0x00000000ffffffffULL; break;
        default: assert(0 && "unsupported swap slice"); return val;
    }
    for (int i = 0; i < 4; ++i)
        val.dwords[i] = ((val.dwords[i] & mask) << slice) | ((val.dwords[i] & ~mask) >> slice);
    return val;
}

/* --- compares: signed / unsigned over full 256-bit -> {-1,0,1} --- */
static inline int
int256_cmp(int256_t a, int256_t b)
{
    if ((int64_t)a.dwords[3] != (int64_t)b.dwords[3]) return (int64_t)a.dwords[3] < (int64_t)b.dwords[3] ? -1 : 1;
    for (int i = 2; i >= 0; --i) if (a.dwords[i] != b.dwords[i]) return a.dwords[i] < b.dwords[i] ? -1 : 1;
    return 0;
}
static inline int
int256_cmpu(int256_t a, int256_t b)
{
    for (int i = 3; i >= 0; --i) if (a.dwords[i] != b.dwords[i]) return a.dwords[i] < b.dwords[i] ? -1 : 1;
    return 0;
}

#endif /* LVX_INT256_H */
