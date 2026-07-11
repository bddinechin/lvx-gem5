/* Minimal Int256_ for the LVX gem5 spike.
 * Enough of the BSL Int256 API for the generated await/addw bodies.
 * (A real port would use Kalray's BSL/Int256.h, per the reuse decision.) */
#ifndef LVX_SPIKE_INT256_H
#define LVX_SPIKE_INT256_H
#include <stdint.h>

typedef struct { uint64_t w[4]; } Int256_;

#define Int256_zero ((Int256_){{0, 0, 0, 0}})

static inline Int256_ Int256_fromUInt64(uint64_t x) { return (Int256_){{x, 0, 0, 0}}; }
static inline Int256_ Int256_fromInt64(int64_t x) {
    uint64_t f = (x < 0) ? ~0ULL : 0ULL;
    return (Int256_){{(uint64_t)x, f, f, f}};
}

static inline int Int256_toBool(Int256_ v) {
    return (v.w[0] | v.w[1] | v.w[2] | v.w[3]) != 0;
}

/* Zero-extend: keep the low `width` bits, clear the rest. */
static inline Int256_ Int256_zx(Int256_ v, int width) {
    Int256_ r = v;
    for (int k = 0; k < 4; k++) {
        int lo = k * 64;
        if (lo >= width) r.w[k] = 0;
        else if (lo + 64 <= width) { /* keep whole word */ }
        else { int b = width - lo; r.w[k] &= (((uint64_t)1 << b) - 1); }
    }
    return r;
}

/* Sign-extend from bit (width-1). */
static inline Int256_ Int256_sx(Int256_ v, int width) {
    Int256_ r = Int256_zx(v, width);
    int s = width - 1, word = s / 64, bit = s % 64;
    if ((r.w[word] >> bit) & 1) {
        for (int k = 0; k < 4; k++) {
            int lo = k * 64;
            if (lo + 64 <= width) { /* fully below width: untouched */ }
            else if (lo >= width) r.w[k] = ~0ULL;
            else { int b = width - lo; r.w[k] |= ~(((uint64_t)1 << b) - 1); }
        }
    }
    return r;
}

static inline Int256_ Int256_add(Int256_ a, Int256_ b) {
    Int256_ r; unsigned __int128 carry = 0;
    for (int k = 0; k < 4; k++) {
        unsigned __int128 s = (unsigned __int128)a.w[k] + b.w[k] + carry;
        r.w[k] = (uint64_t)s; carry = s >> 64;
    }
    return r;
}

#endif
