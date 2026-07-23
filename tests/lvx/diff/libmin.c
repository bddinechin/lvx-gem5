/* Minimal freestanding runtime for the differential corpus.
 *
 * There is no newlib in the -mbr toolchain yet, so these programs link against
 * nothing.  GCC still lowers struct copies and array/aggregate initialisers to
 * calls to memcpy/memset even under -ffreestanding, so provide just those (plus
 * the two siblings) to keep the corpus link-clean.  Nothing here is LVX-specific;
 * it exists only so a bug in the *compiler* is what a mismatch means, never a
 * missing symbol.  When newlib's mbr port lands, this file goes away. */

typedef unsigned long size_t;

void *memset(void *d, int c, size_t n) {
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}

void *memmove(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    if (a < b) { while (n--) *a++ = *b++; }
    else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *a = s1, *b = s2;
    while (n--) { if (*a != *b) return (int)*a - (int)*b; a++; b++; }
    return 0;
}
