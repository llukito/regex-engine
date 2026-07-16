#ifndef REGEX_CHARSET_H
#define REGEX_CHARSET_H

/*
 * Branchless ASCII case folding and 256-bit character-class bitmaps.
 *
 * Case maps cover the full 0..255 byte range:
 *   - 0x00..0x7F: classic ASCII A-Z / a-z folding
 *   - 0x80..0xFF: identity (matches C-locale tolower/toupper for high bytes)
 *
 * Tables are expanded at compile time via macros — no runtime init loops.
 * Hot-path ops are table lookups and bit arithmetic only (no if/ternary
 * in the match path).
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "ast.h" /* CharRange — cold-path range↔bitmap conversion */

/* ---- compile-time byte classification tables -------------------------- */

/*
 * Use unsigned range checks so the true-branch never runs for high bytes
 * (avoids (c)+32 overflow warnings and truncated wrong table entries).
 * Result is cast to unsigned char for the array element type.
 */
#define BYTE_TOLOWER_OF(c) \
    ((unsigned char)( \
        (((unsigned)(c) - (unsigned)('A')) <= 25u) \
            ? ((unsigned)(c) + 32u) \
            : (unsigned)(c)))

#define BYTE_TOUPPER_OF(c) \
    ((unsigned char)( \
        (((unsigned)(c) - (unsigned)('a')) <= 25u) \
            ? ((unsigned)(c) - 32u) \
            : (unsigned)(c)))

#define CHARSET_R4(f, n)  f(n), f((n) + 1), f((n) + 2), f((n) + 3)
#define CHARSET_R16(f, n) \
    CHARSET_R4(f, n), CHARSET_R4(f, (n) + 4), \
    CHARSET_R4(f, (n) + 8), CHARSET_R4(f, (n) + 12)
#define CHARSET_R64(f, n) \
    CHARSET_R16(f, n), CHARSET_R16(f, (n) + 16), \
    CHARSET_R16(f, (n) + 32), CHARSET_R16(f, (n) + 48)
#define CHARSET_R256(f) \
    CHARSET_R64(f, 0), CHARSET_R64(f, 64), \
    CHARSET_R64(f, 128), CHARSET_R64(f, 192)

static const unsigned char BYTE_TOLOWER[256] = {
    CHARSET_R256(BYTE_TOLOWER_OF)
};

static const unsigned char BYTE_TOUPPER[256] = {
    CHARSET_R256(BYTE_TOUPPER_OF)
};

/* ---- branchless case fold (hot path: pure table index) --------------- */

static inline unsigned char byte_tolower(unsigned char c)
{
    return BYTE_TOLOWER[c];
}

static inline unsigned char byte_toupper(unsigned char c)
{
    return BYTE_TOUPPER[c];
}

/* ---- 256-bit character bitmap ----------------------------------------- */

typedef struct CharBitmap {
    uint64_t w[4]; /* bit i set ⇒ byte i is a member */
} CharBitmap;

static inline void char_bitmap_clear(CharBitmap *b)
{
    b->w[0] = b->w[1] = b->w[2] = b->w[3] = 0;
}

/* Set bit c. Branchless: shift + OR. */
static inline void char_bitmap_set(CharBitmap *b, unsigned char c)
{
    b->w[c >> 6] |= (uint64_t)1 << (c & 63);
}

/*
 * Test bit c. Returns 0 or 1. Branchless: shift + mask.
 * Hot path for class membership.
 */
static inline int char_bitmap_test(const CharBitmap *b, unsigned char c)
{
    return (int)((b->w[c >> 6] >> (c & 63)) & 1u);
}

/*
 * Invert all 256 bits when neg is 1; no-op when neg is 0.
 * neg must be 0 or 1. Branchless word XOR with 0 or ~0.
 */
static inline void char_bitmap_apply_neg(CharBitmap *b, unsigned neg)
{
    uint64_t m = 0u - (uint64_t)neg;
    b->w[0] ^= m;
    b->w[1] ^= m;
    b->w[2] ^= m;
    b->w[3] ^= m;
}

/* OR two bitmaps into dst (dst may alias a or b). */
static inline void char_bitmap_or(CharBitmap *dst,
                                  const CharBitmap *a, const CharBitmap *b)
{
    dst->w[0] = a->w[0] | b->w[0];
    dst->w[1] = a->w[1] | b->w[1];
    dst->w[2] = a->w[2] | b->w[2];
    dst->w[3] = a->w[3] | b->w[3];
}

/*
 * Add byte c and both of its case folds into the bitmap.
 * Always sets two bits (identical when fold is a no-op). Branchless.
 */
static inline void char_bitmap_set_icase(CharBitmap *b, unsigned char c)
{
    char_bitmap_set(b, byte_tolower(c));
    char_bitmap_set(b, byte_toupper(c));
}

/*
 * Expand every set bit in `src` with case folds into `dst`.
 * Order: read positive membership first, then fold — never fold after
 * negation. Branchless set ops; loop is cold (class construction).
 */
static inline void char_bitmap_expand_icase(CharBitmap *dst,
                                           const CharBitmap *src)
{
    char_bitmap_clear(dst);
    for (unsigned c = 0; c < 256; c++) {
        /* Always call set_icase; when bit is clear, use a zeroed scratch
         * approach: set folds only when member. Using multiply by bit
         * keeps the fold path data-driven:
         * if test is 0, we still must not set. Branchless select: */
        unsigned char uc = (unsigned char)c;
        int on = char_bitmap_test(src, uc);
        /* Branchless: set folds of c when on, else set nothing.
         * Implement by setting into a temp only when on via mask on OR. */
        CharBitmap one;
        char_bitmap_clear(&one);
        char_bitmap_set_icase(&one, uc);
        /* if on: dst |= one; else no-op.
         * mask = 0 or ~0 from on (0 or 1). */
        uint64_t m = 0u - (uint64_t)(unsigned)on;
        dst->w[0] |= one.w[0] & m;
        dst->w[1] |= one.w[1] & m;
        dst->w[2] |= one.w[2] & m;
        dst->w[3] |= one.w[3] & m;
    }
}

/* ---- cold-path conversions (loops OK; not match hot path) ------------- */

/* Fill bitmap from inclusive ranges (positive membership only). */
static inline void char_bitmap_from_ranges(CharBitmap *b,
                                          const CharRange *ranges,
                                          size_t nranges)
{
    char_bitmap_clear(b);
    for (size_t i = 0; i < nranges; i++) {
        unsigned lo = ranges[i].lo;
        unsigned hi = ranges[i].hi;
        for (unsigned c = lo;; c++) {
            char_bitmap_set(b, (unsigned char)c);
            if (c == hi)
                break;
        }
    }
}

/*
 * Positive ranges → case-insensitive positive bitmap.
 * Does NOT apply negation; caller must invert after if needed.
 */
static inline void char_bitmap_from_ranges_icase(CharBitmap *b,
                                                const CharRange *ranges,
                                                size_t nranges)
{
    CharBitmap pos;
    char_bitmap_from_ranges(&pos, ranges, nranges);
    char_bitmap_expand_icase(b, &pos);
}

/*
 * Convert a bitmap to a compact range list (malloc). Returns 0 on OOM.
 * *out_ranges / *out_nranges only written on success.
 */
static inline int char_bitmap_to_ranges(const CharBitmap *b,
                                        CharRange **out_ranges,
                                        size_t *out_nranges)
{
    CharRange *ranges = NULL;
    size_t n = 0;
    size_t cap = 0;
    int in_run = 0;
    unsigned char run_lo = 0;

    for (unsigned c = 0; c < 256; c++) {
        int on = char_bitmap_test(b, (unsigned char)c);
        if (on && !in_run) {
            in_run = 1;
            run_lo = (unsigned char)c;
        } else if (!on && in_run) {
            if (n >= cap) {
                size_t ncap = cap ? cap * 2 : 4;
                CharRange *nr = (CharRange *)realloc(ranges, ncap * sizeof(*nr));
                if (!nr) {
                    free(ranges);
                    return 0;
                }
                ranges = nr;
                cap = ncap;
            }
            ranges[n].lo = run_lo;
            ranges[n].hi = (unsigned char)(c - 1);
            n++;
            in_run = 0;
        }
    }
    if (in_run) {
        if (n >= cap) {
            size_t ncap = cap ? cap * 2 : 4;
            CharRange *nr = (CharRange *)realloc(ranges, ncap * sizeof(*nr));
            if (!nr) {
                free(ranges);
                return 0;
            }
            ranges = nr;
        }
        ranges[n].lo = run_lo;
        ranges[n].hi = 255;
        n++;
    }

    *out_ranges = ranges;
    *out_nranges = n;
    return 1;
}

#endif /* REGEX_CHARSET_H */
