/*
 * LVX ISA support for gem5 — Layer B shim: IEEE-754 double-precision helpers.
 * SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2026 Liesme Tech.
 * Benoît Dupont de Dinechin (benoit.dinechin@gmail.com).
 *
 * Implements the f64 operator helpers the MDS-generated behavior bodies call
 * (Behavior_f64_{add,sub,mul,mulAdd,mulnAdd,div,sqrt,rint,min,max,minNum,
 * maxNum}), backed by Berkeley SoftFloat (ext/softfloat, RISC-V specialization
 * — default-NaN, tininess-after-rounding, round-to-odd, which LVX's floatmode
 * exposes).
 *
 * All operators match RISC-V FP arithmetic exactly (mnemonics aside) -- any
 * divergence would be an LVX ISA spec bug. RISC-V has two min/max families and
 * so does LVX, with the plain/Num names inverted between them:
 *   LVX f64_minNum/f64_maxNum == RISC-V FMIN.D/FMAX.D   (IEEE minimumNumber/
 *       maximumNumber: return the numeric operand when one is NaN, canonical NaN
 *       only if both are, -0 < +0, invalid only on signaling NaN) == SoftFloat
 *       f64_min/f64_max.
 *   LVX f64_min/f64_max       == RISC-V FMINM.D/FMAXM.D (Zfa; IEEE minimum/
 *       maximum: NaN-PROPAGATING -- either operand NaN yields canonical NaN,
 *       still raising invalid on a signaling NaN).
 * f64_rint is f64_roundToInt with exact=true (RISC-V FROUNDNX.D: raises inexact).
 *
 * These are PURE functions of (rounding mode, raw IEEE-754 bits). The generated
 * execute body owns all architectural FP state: it resolves the rounding mode
 * (static `floatmode`, or dynamic `$cs.RM` when floatmode==7), pre-applies the
 * `fnegate` sign flips to the operands, reads the current `$cs` sticky exception
 * flags, ORs in this op's freshly-raised flags, and writes `$cs` back. Each
 * helper only:
 *   1. sets SoftFloat's rounding mode from the LVX 3-bit code,
 *   2. clears the scratch exception flags,
 *   3. runs the operation on the raw bits it was handed,
 *   4. returns the result bits plus this op's raised flags, packed into the
 *      per-op Tuple in the exact field order that op's execute body binds.
 * SoftFloat's thread-globals are used strictly as per-call scratch — nothing
 * persists across helper calls; the sticky accumulation lives in `$cs`.
 *
 * Core-agnostic (shared by lvx_v1 and lvx_v2): f64 semantics are identical
 * across cores — a shared-instruction behavior difference would be an ISA
 * authoring error, not something to handle here — and the helper ABI
 * (signature, Tuple layout, flag order) is verified byte-identical between the
 * two cores' generated glue, so this one definition satisfies both. On lvx_v2
 * the packed/SIMD FP ops invoke these same scalar helpers per lane.
 */
#include <cstdint>

extern "C" {
#include "softfloat.h"
}

// Tuple_64_1_1_1 / Tuple_64_1_1_1_1 — the flag-returning tuple POD types the
// generated behavior expects. Included the same way behavior.c does; identical
// across cores (verified), so this stays core-agnostic.
#include "arch/lvx/generated/behavior_types.inc"

// SoftFloat internal fused multiply-add with an op selector (subProd negates the
// product). The public f64_mulAdd is just this with op==0; FFMSD needs
// -(a*b)+c, which is exactly op==softfloat_mulAdd_subProd. Declared here to
// avoid pulling all of SoftFloat's internals.h; the symbol is exported by
// libsoftfloat (s_mulAddF64.c).
extern "C" float64_t softfloat_mulAddF64(
    uint_fast64_t, uint_fast64_t, uint_fast64_t, uint_fast8_t);

namespace
{

// softfloat_mulAdd op selector for "subtract the product" (internals.h).
constexpr uint_fast8_t kMulAddSubProd = 2;

// LVX `floatmode` 3-bit code -> SoftFloat rounding mode.
//   0 RN nearest-even | 1 RZ toward-zero | 2 RD downward | 3 RU upward
//   4 RM nearest-max-magnitude | 5 reserved | 6 RO round-to-odd
// floatmode==7 ("use CS rounding") is resolved to a 0..6 code by the generated
// execute body, so the helper never sees it. The 6->odd(5) entry is why this is
// a table and not the identity.
inline uint_fast8_t
sfRoundingMode(uint8_t rm)
{
    static const uint8_t kMap[8] = {
        softfloat_round_near_even,    // 0 RN
        softfloat_round_minMag,       // 1 RZ
        softfloat_round_min,          // 2 RD
        softfloat_round_max,          // 3 RU
        softfloat_round_near_maxMag,  // 4 RM
        softfloat_round_near_even,    // 5 reserved (defensive)
        softfloat_round_odd,          // 6 RO
        softfloat_round_near_even,    // 7 dynamic (pre-resolved; unreachable)
    };
    return kMap[rm & 7];
}

// Set rounding mode + tininess and clear the scratch exception flags, once per
// helper call. Tininess-after-rounding is the RISC-V/LVX convention (and the
// specialization default); set explicitly so behaviour never depends on prior
// state.
inline void
sfBegin(uint8_t rm)
{
    softfloat_roundingMode = sfRoundingMode(rm);
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = 0;
}

inline uint8_t flagIO() { return (softfloat_exceptionFlags & softfloat_flag_invalid)   ? 1 : 0; }
inline uint8_t flagDZ() { return (softfloat_exceptionFlags & softfloat_flag_infinite)  ? 1 : 0; }
inline uint8_t flagOV() { return (softfloat_exceptionFlags & softfloat_flag_overflow)  ? 1 : 0; }
inline uint8_t flagUN() { return (softfloat_exceptionFlags & softfloat_flag_underflow) ? 1 : 0; }
inline uint8_t flagIN() { return (softfloat_exceptionFlags & softfloat_flag_inexact)   ? 1 : 0; }

inline float64_t f64(uint64_t bits) { return float64_t{bits}; }

// IEEE-754 binary64 canonical quiet NaN (RISC-V default NaN) and a raw-bits NaN
// test, for the min/max helpers whose NaN handling is done outside SoftFloat.
constexpr uint64_t kDefaultNaN64 = UINT64_C(0x7FF8000000000000);
inline bool
isNaN64(uint64_t b)
{
    return (b & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000)
        && (b & UINT64_C(0x000FFFFFFFFFFFFF)) != 0;
}

}  // namespace

extern "C" {

// FADDD: argument sum. Tuple = {value, io, ov, in} (LVX add/sub record neither
// underflow nor divide-by-zero).
Tuple_64_1_1_1
Behavior_f64_add(void * /*self*/, uint8_t rm, uint64_t a, uint64_t b)
{
    sfBegin(rm);
    float64_t r = f64_add(f64(a), f64(b));
    return Tuple_64_1_1_1{ r.v, flagIO(), flagOV(), flagIN() };
}

// FSBFD: a - b (the generated code passes the minuend as `a`).
Tuple_64_1_1_1
Behavior_f64_sub(void * /*self*/, uint8_t rm, uint64_t a, uint64_t b)
{
    sfBegin(rm);
    float64_t r = f64_sub(f64(a), f64(b));
    return Tuple_64_1_1_1{ r.v, flagIO(), flagOV(), flagIN() };
}

// FMULD: a * b. Tuple = {value, io, ov, un, in}.
Tuple_64_1_1_1_1
Behavior_f64_mul(void * /*self*/, uint8_t rm, uint64_t a, uint64_t b)
{
    sfBegin(rm);
    float64_t r = f64_mul(f64(a), f64(b));
    return Tuple_64_1_1_1_1{ r.v, flagIO(), flagOV(), flagUN(), flagIN() };
}

// FFMAD: fused a*b + c (single rounding).
Tuple_64_1_1_1_1
Behavior_f64_mulAdd(void * /*self*/, uint8_t rm, uint64_t a, uint64_t b, uint64_t c)
{
    sfBegin(rm);
    float64_t r = f64_mulAdd(f64(a), f64(b), f64(c));
    return Tuple_64_1_1_1_1{ r.v, flagIO(), flagOV(), flagUN(), flagIN() };
}

// FFMSD: fused -(a*b) + c (product subtracted from the accumulator), single
// rounding. subProd negates the product's sign, matching "subtracted from %1".
Tuple_64_1_1_1_1
Behavior_f64_mulnAdd(void * /*self*/, uint8_t rm, uint64_t a, uint64_t b, uint64_t c)
{
    sfBegin(rm);
    float64_t r = softfloat_mulAddF64(a, b, c, kMulAddSubProd);
    return Tuple_64_1_1_1_1{ r.v, flagIO(), flagOV(), flagUN(), flagIN() };
}

// FDIVD: a / b. Tuple = {value, io, dz, ov, un, in} (only op that can raise the
// divide-by-zero flag -- SoftFloat reports it as the "infinite" flag).
Tuple_64_1_1_1_1_1
Behavior_f64_div(void * /*self*/, uint8_t rm, uint64_t a, uint64_t b)
{
    sfBegin(rm);
    float64_t r = f64_div(f64(a), f64(b));
    return Tuple_64_1_1_1_1_1{ r.v, flagIO(), flagDZ(), flagOV(), flagUN(), flagIN() };
}

// FSQRTD: sqrt(a). Tuple = {value, io, in}.
Tuple_64_1_1
Behavior_f64_sqrt(void * /*self*/, uint8_t rm, uint64_t a)
{
    sfBegin(rm);
    float64_t r = f64_sqrt(f64(a));
    return Tuple_64_1_1{ r.v, flagIO(), flagIN() };
}

// FRINTD: round to integral value in f64, RISC-V FROUNDNX.D -- exact=true, so
// the inexact flag IS raised when the rounded result differs from the input.
// Tuple = {value, io, in}.
Tuple_64_1_1
Behavior_f64_rint(void * /*self*/, uint8_t rm, uint64_t a)
{
    sfBegin(rm);
    float64_t r = f64_roundToInt(f64(a), softfloat_roundingMode, true);
    return Tuple_64_1_1{ r.v, flagIO(), flagIN() };
}

// FMINND/FMAXND: IEEE-754 minimumNumber/maximumNumber == RISC-V FMIN.D/FMAX.D.
// SoftFloat's f64_min/f64_max implement exactly this: return the numeric operand
// when one is NaN, canonical NaN only if both are, treat -0.0 < +0.0, and raise
// invalid only on a signaling NaN (they select with f64_lt_quiet + an f64_eq
// signed-zero tiebreak). Tuple = {value, io}. No rounding.
Tuple_64_1
Behavior_f64_minNum(void * /*self*/, uint64_t a, uint64_t b)
{
    softfloat_exceptionFlags = 0;
    float64_t r = f64_min(f64(a), f64(b));
    return Tuple_64_1{ r.v, flagIO() };
}

Tuple_64_1
Behavior_f64_maxNum(void * /*self*/, uint64_t a, uint64_t b)
{
    softfloat_exceptionFlags = 0;
    float64_t r = f64_max(f64(a), f64(b));
    return Tuple_64_1{ r.v, flagIO() };
}

// FMIND/FMAXD: IEEE-754 minimum/maximum == RISC-V FMINM.D/FMAXM.D (Zfa). Same as
// the Number variants but NaN-PROPAGATING: if either operand is NaN the result
// is the canonical NaN (the signaling-NaN invalid flag is still raised). Built
// on SoftFloat's f64_min/f64_max, overriding only the value when an input is
// NaN, so the flag and signed-zero behaviour stay identical to RISC-V.
Tuple_64_1
Behavior_f64_min(void * /*self*/, uint64_t a, uint64_t b)
{
    softfloat_exceptionFlags = 0;
    float64_t r = f64_min(f64(a), f64(b));
    uint64_t v = (isNaN64(a) || isNaN64(b)) ? kDefaultNaN64 : r.v;
    return Tuple_64_1{ v, flagIO() };
}

Tuple_64_1
Behavior_f64_max(void * /*self*/, uint64_t a, uint64_t b)
{
    softfloat_exceptionFlags = 0;
    float64_t r = f64_max(f64(a), f64(b));
    uint64_t v = (isNaN64(a) || isNaN64(b)) ? kDefaultNaN64 : r.v;
    return Tuple_64_1{ v, flagIO() };
}

}  // extern "C"
