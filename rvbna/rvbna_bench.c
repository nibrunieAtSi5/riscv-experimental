/*
 * RVBNA Benchmark
 *
 * Compares the pure-C implementation (rvbna.h) against the reference
 * C++ implementation (bulknormdot.h) for BF16×BF16 → FP32 dot products
 * with n=2.
 *
 * Test data:
 *   1. Directed test cases (user-extensible)
 *   2. Random test cases with skewed distributions
 *      (subnormal, normal, special values, near-boundary)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "rvbna.h"

/* ── Reference implementation (bulknormdot.h via C++ wrapper) ──────── */
extern void ref_bulk_norm_dot_bf16(const uint16_t *a_raw,
                                   const uint16_t *b_raw,
                                   int n,
                                   uint32_t *out_value,
                                   uint8_t  *out_flags);

/* ── BF16 encoding helpers ─────────────────────────────────────────── */

/* BF16 bit layout: [15] sign | [14:7] 8-bit exponent | [6:0] 7-bit mantissa */
#define BF16_EXP_BITS   8
#define BF16_MANT_BITS  7
#define BF16_EXP_MASK   0x7F80u
#define BF16_MANT_MASK  0x007Fu
#define BF16_SIGN_MASK  0x8000u

/* Well-known BF16 values */
#define BF16_POS_ZERO   0x0000u
#define BF16_NEG_ZERO   0x8000u
#define BF16_POS_INF    0x7F80u
#define BF16_NEG_INF    0xFF80u
#define BF16_QNAN       0x7FC0u  /* canonical quiet NaN */
#define BF16_SNAN       0x7F01u  /* a signaling NaN */
#define BF16_POS_ONE    0x3F80u  /* +1.0 */
#define BF16_NEG_ONE    0xBF80u  /* -1.0 */
#define BF16_POS_TWO    0x4000u  /* +2.0 */
#define BF16_NEG_TWO    0xC000u  /* -2.0 */
#define BF16_MAX_NORM   0x7F7Fu  /* largest normal: 3.38953e38 */
#define BF16_MIN_NORM   0x0080u  /* smallest positive normal: 2^-126 */
#define BF16_MIN_SUB    0x0001u  /* smallest positive subnormal */
#define BF16_MAX_SUB    0x007Fu  /* largest positive subnormal */
#define BF16_NEG_MIN_SUB 0x8001u /* smallest negative subnormal */

/* ── Directed test cases ───────────────────────────────────────────── */
/*
 * Each test case is { a[0], b[0], a[1], b[1] }.
 * Users can extend this array by adding entries.
 */
typedef struct {
    const char *name;
    uint16_t a[2];
    uint16_t b[2];
} directed_test_t;

static const directed_test_t directed_tests[] = {
    /* ── Zeros ──────────────────────────────────────────────────────── */
    { "zero * zero + zero * zero",
      { BF16_POS_ZERO, BF16_POS_ZERO }, { BF16_POS_ZERO, BF16_POS_ZERO } },
    { "+zero * +one + +zero * +one",
      { BF16_POS_ZERO, BF16_POS_ZERO }, { BF16_POS_ONE, BF16_POS_ONE } },
    { "-zero * +one + +zero * +one",
      { BF16_NEG_ZERO, BF16_POS_ZERO }, { BF16_POS_ONE, BF16_POS_ONE } },

    /* ── Normal × Normal ────────────────────────────────────────────── */
    { "+1 * +1 + +1 * +1",
      { BF16_POS_ONE, BF16_POS_ONE }, { BF16_POS_ONE, BF16_POS_ONE } },
    { "+1 * -1 + +1 * +1",
      { BF16_POS_ONE, BF16_POS_ONE }, { BF16_NEG_ONE, BF16_POS_ONE } },
    { "+1 * +1 + -1 * +1 (cancellation)",
      { BF16_POS_ONE, BF16_NEG_ONE }, { BF16_POS_ONE, BF16_POS_ONE } },
    { "+2 * +2 + +1 * +1",
      { BF16_POS_TWO, BF16_POS_ONE }, { BF16_POS_TWO, BF16_POS_ONE } },
    { "+1 * -1 + -1 * +1 (double cancel)",
      { BF16_POS_ONE, BF16_NEG_ONE }, { BF16_NEG_ONE, BF16_POS_ONE } },

    /* ── Subnormal inputs ───────────────────────────────────────────── */
    { "min_sub * min_sub + zero * zero",
      { BF16_MIN_SUB, BF16_POS_ZERO }, { BF16_MIN_SUB, BF16_POS_ZERO } },
    { "max_sub * max_sub + zero * zero",
      { BF16_MAX_SUB, BF16_POS_ZERO }, { BF16_MAX_SUB, BF16_POS_ZERO } },
    { "min_sub * +one + zero * zero",
      { BF16_MIN_SUB, BF16_POS_ZERO }, { BF16_POS_ONE, BF16_POS_ZERO } },
    { "max_sub * +one + zero * zero",
      { BF16_MAX_SUB, BF16_POS_ZERO }, { BF16_POS_ONE, BF16_POS_ZERO } },
    { "min_sub * min_sub + min_sub * min_sub",
      { BF16_MIN_SUB, BF16_MIN_SUB }, { BF16_MIN_SUB, BF16_MIN_SUB } },
    { "max_sub * max_sub + max_sub * max_sub",
      { BF16_MAX_SUB, BF16_MAX_SUB }, { BF16_MAX_SUB, BF16_MAX_SUB } },
    { "min_sub * max_norm + zero * zero",
      { BF16_MIN_SUB, BF16_POS_ZERO }, { BF16_MAX_NORM, BF16_POS_ZERO } },
    { "neg_min_sub * +one + min_sub * +one (subnormal cancel)",
      { BF16_NEG_MIN_SUB, BF16_MIN_SUB }, { BF16_POS_ONE, BF16_POS_ONE } },

    /* ── Overflow ───────────────────────────────────────────────────── */
    { "max_norm * max_norm + max_norm * max_norm",
      { BF16_MAX_NORM, BF16_MAX_NORM }, { BF16_MAX_NORM, BF16_MAX_NORM } },
    { "max_norm * +one + max_norm * +one",
      { BF16_MAX_NORM, BF16_MAX_NORM }, { BF16_POS_ONE, BF16_POS_ONE } },

    /* ── Infinity ───────────────────────────────────────────────────── */
    { "+inf * +one + +one * +one",
      { BF16_POS_INF, BF16_POS_ONE }, { BF16_POS_ONE, BF16_POS_ONE } },
    { "-inf * +one + +one * +one",
      { BF16_NEG_INF, BF16_POS_ONE }, { BF16_POS_ONE, BF16_POS_ONE } },
    { "+inf * +one + -inf * +one (inf - inf → NaN)",
      { BF16_POS_INF, BF16_NEG_INF }, { BF16_POS_ONE, BF16_POS_ONE } },
    { "+inf * zero → invalid",
      { BF16_POS_INF, BF16_POS_ONE }, { BF16_POS_ZERO, BF16_POS_ONE } },
    { "zero * +inf → invalid",
      { BF16_POS_ZERO, BF16_POS_ONE }, { BF16_POS_INF, BF16_POS_ONE } },

    /* ── NaN ────────────────────────────────────────────────────────── */
    { "qNaN * +one + +one * +one",
      { BF16_QNAN, BF16_POS_ONE }, { BF16_POS_ONE, BF16_POS_ONE } },
    { "+one * qNaN + +one * +one",
      { BF16_POS_ONE, BF16_POS_ONE }, { BF16_QNAN, BF16_POS_ONE } },
    { "sNaN * +one + +one * +one",
      { BF16_SNAN, BF16_POS_ONE }, { BF16_POS_ONE, BF16_POS_ONE } },
    { "+one * sNaN + +one * +one",
      { BF16_POS_ONE, BF16_POS_ONE }, { BF16_SNAN, BF16_POS_ONE } },
    { "qNaN * +inf + +one * +one",
      { BF16_QNAN, BF16_POS_ONE }, { BF16_POS_INF, BF16_POS_ONE } },

    /* ── Mixed normal/subnormal products ────────────────────────────── */
    { "+one * +one + min_sub * min_sub",
      { BF16_POS_ONE, BF16_MIN_SUB }, { BF16_POS_ONE, BF16_MIN_SUB } },
    { "min_norm * min_norm + zero * zero",
      { BF16_MIN_NORM, BF16_POS_ZERO }, { BF16_MIN_NORM, BF16_POS_ZERO } },
    { "min_norm * min_sub + zero * zero",
      { BF16_MIN_NORM, BF16_POS_ZERO }, { BF16_MIN_SUB, BF16_POS_ZERO } },

    /* ── Near-boundary products ──────────────────────────────────────── */
    { "max_norm * min_norm + zero * zero",
      { BF16_MAX_NORM, BF16_POS_ZERO }, { BF16_MIN_NORM, BF16_POS_ZERO } },
    { "max_norm * -max_norm + max_norm * max_norm (cancel at overflow)",
      { BF16_MAX_NORM, BF16_MAX_NORM }, { (uint16_t)(BF16_MAX_NORM | BF16_SIGN_MASK), BF16_MAX_NORM } },
};

static const int num_directed_tests =
    (int)(sizeof(directed_tests) / sizeof(directed_tests[0]));

/* ── Random number generator (xoshiro128**) ────────────────────────── */

static uint32_t rng_state[4];

static void rng_seed(uint64_t seed)
{
    /* SplitMix64 to initialize xoshiro state */
    for (int i = 0; i < 4; i++) {
        seed += 0x9E3779B97F4A7C15ULL;
        uint64_t z = seed;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        rng_state[i] = (uint32_t)(z >> 32);
    }
}

static inline uint32_t rotl32(uint32_t x, int k)
{
    return (x << k) | (x >> (32 - k));
}

static uint32_t rng_next(void)
{
    uint32_t result = rotl32(rng_state[1] * 5, 7) * 9;
    uint32_t t = rng_state[1] << 9;
    rng_state[2] ^= rng_state[0];
    rng_state[3] ^= rng_state[1];
    rng_state[1] ^= rng_state[2];
    rng_state[0] ^= rng_state[3];
    rng_state[2] ^= t;
    rng_state[3] = rotl32(rng_state[3], 11);
    return result;
}

/* ── Random BF16 generation with skewed distribution ─────────────── */

/*
 * Distribution categories (weights sum to 100):
 *   SUBNORMAL      - 25%  exponent = 0, mantissa random non-zero
 *   NORMAL         - 40%  exponent in [1, 254], mantissa random
 *   SPECIAL        - 10%  ±0, ±Inf, qNaN, sNaN
 *   NEAR_BOUNDARY  - 25%  near overflow/underflow boundaries
 */
typedef enum {
    CAT_SUBNORMAL = 0,
    CAT_NORMAL,
    CAT_SPECIAL,
    CAT_NEAR_BOUNDARY,
    CAT_COUNT
} bf16_category_t;

static const int category_weights[CAT_COUNT] = { 25, 40, 10, 25 };

static bf16_category_t pick_category(void)
{
    int r = (int)(rng_next() % 100);
    int cum = 0;
    for (int c = 0; c < CAT_COUNT; c++) {
        cum += category_weights[c];
        if (r < cum)
            return (bf16_category_t)c;
    }
    return CAT_NORMAL; /* fallback */
}

static uint16_t random_sign(void)
{
    return (rng_next() & 1) ? BF16_SIGN_MASK : 0;
}

static uint16_t gen_random_bf16(void)
{
    bf16_category_t cat = pick_category();
    uint16_t sign = random_sign();

    switch (cat) {
    case CAT_SUBNORMAL: {
        /* exponent = 0, mantissa = random non-zero 7-bit value */
        uint16_t mant = (uint16_t)((rng_next() % 127) + 1); /* [1, 127] */
        return sign | mant;
    }
    case CAT_NORMAL: {
        /* exponent in [1, 254], mantissa random 7-bit */
        uint16_t exp = (uint16_t)((rng_next() % 254) + 1); /* [1, 254] */
        uint16_t mant = (uint16_t)(rng_next() & BF16_MANT_MASK);
        return sign | (exp << BF16_MANT_BITS) | mant;
    }
    case CAT_SPECIAL: {
        /* pick from a table of special values (ignoring sign — we add it) */
        static const uint16_t specials[] = {
            0x0000u, /* +0 */
            0x7F80u, /* +Inf */
            0x7FC0u, /* qNaN */
            0x7F01u, /* sNaN */
        };
        int idx = (int)(rng_next() % (sizeof(specials) / sizeof(specials[0])));
        return sign | specials[idx];
    }
    case CAT_NEAR_BOUNDARY: {
        /* near overflow or underflow boundaries */
        static const uint16_t boundaries[] = {
            0x7F7Fu, /* max normal */
            0x7F00u, /* large normal (exp=254, mant=0) */
            0x0080u, /* min normal */
            0x0081u, /* min normal + 1 ulp */
            0x007Fu, /* max subnormal */
            0x0001u, /* min subnormal */
            0x0040u, /* subnormal with only bit 6 set */
            0x3F80u, /* +1.0 */
            0x4000u, /* +2.0 */
        };
        int idx = (int)(rng_next() % (sizeof(boundaries) / sizeof(boundaries[0])));
        return sign | boundaries[idx];
    }
    default:
        return 0;
    }
}

/* ── Flag name helpers ─────────────────────────────────────────────── */

static const char *flags_str(uint8_t flags, char *buf, int bufsize)
{
    buf[0] = '\0';
    if (flags & RVBNA_FLAG_INVALID)
        strncat(buf, "INVALID ", (size_t)(bufsize - 1 - (int)strlen(buf)));
    if (flags & RVBNA_FLAG_OVERFLOW)
        strncat(buf, "OVERFLOW ", (size_t)(bufsize - 1 - (int)strlen(buf)));
    if (flags == 0)
        strncat(buf, "(none)", (size_t)(bufsize - 1 - (int)strlen(buf)));
    return buf;
}

/* ── Float interpretation helper ───────────────────────────────────── */

static float uint32_to_float(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/* ── Main benchmark ────────────────────────────────────────────────── */

static int run_test(const char *name,
                    const uint16_t a[2], const uint16_t b[2],
                    int *pass_count, int *fail_count,
                    int max_failures_to_print, int *printed_failures)
{
    /* Run the pure-C implementation */
    rvbna_result_t c_result = rvbna_bf16_dot(a, b, 2);

    /* Run the reference C++ implementation */
    uint32_t ref_value;
    uint8_t  ref_flags;
    ref_bulk_norm_dot_bf16(a, b, 2, &ref_value, &ref_flags);

    bool value_match = (c_result.value == ref_value);
    bool flags_match = (c_result.flags == ref_flags);

    if (value_match && flags_match) {
        (*pass_count)++;
        return 1;
    }

    (*fail_count)++;
    if (*printed_failures < max_failures_to_print) {
        char c_flags_buf[64], ref_flags_buf[64];
        printf("  FAIL: %s\n", name);
        printf("    inputs: a={0x%04x, 0x%04x} b={0x%04x, 0x%04x}\n",
               a[0], a[1], b[0], b[1]);
        printf("    C impl:   value=0x%08x (%.8g)  flags=%s\n",
               c_result.value, uint32_to_float(c_result.value),
               flags_str(c_result.flags, c_flags_buf, sizeof(c_flags_buf)));
        printf("    ref impl: value=0x%08x (%.8g)  flags=%s\n",
               ref_value, uint32_to_float(ref_value),
               flags_str(ref_flags, ref_flags_buf, sizeof(ref_flags_buf)));
        if (!value_match)
            printf("    ** VALUE MISMATCH **\n");
        if (!flags_match)
            printf("    ** FLAGS MISMATCH **\n");
        printf("\n");
        (*printed_failures)++;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int num_random_tests = 10000;
    uint64_t seed = 42;
    int max_failures_to_print = 50;

    /* Parse optional arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--random") == 0 && i + 1 < argc)
            num_random_tests = atoi(argv[++i]);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed = (uint64_t)atoll(argv[++i]);
        else if (strcmp(argv[i], "--max-fail") == 0 && i + 1 < argc)
            max_failures_to_print = atoi(argv[++i]);
        else if (strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [--random N] [--seed S] [--max-fail M]\n", argv[0]);
            printf("  --random N    Number of random test cases (default: 10000)\n");
            printf("  --seed S      RNG seed (default: 42)\n");
            printf("  --max-fail M  Max failures to print (default: 50)\n");
            return 0;
        }
    }

    printf("=== RVBNA Benchmark: BF16×BF16 → FP32 (n=2) ===\n");
    printf("Configuration: %d directed + %d random tests (seed=%llu)\n\n",
           num_directed_tests, num_random_tests, (unsigned long long)seed);

    int pass_count = 0, fail_count = 0, printed_failures = 0;

    /* ── Directed tests ─────────────────────────────────────────────── */
    printf("--- Directed Tests (%d cases) ---\n", num_directed_tests);
    for (int i = 0; i < num_directed_tests; i++) {
        run_test(directed_tests[i].name,
                 directed_tests[i].a, directed_tests[i].b,
                 &pass_count, &fail_count,
                 max_failures_to_print, &printed_failures);
    }
    printf("  Directed: %d passed, %d failed\n\n",
           pass_count, fail_count);

    /* ── Random tests ───────────────────────────────────────────────── */
    int rand_pass = 0, rand_fail = 0;
    rng_seed(seed);

    printf("--- Random Tests (%d cases, seed=%llu) ---\n",
           num_random_tests, (unsigned long long)seed);
    printf("  Distribution: subnormal=%d%% normal=%d%% special=%d%% near_boundary=%d%%\n",
           category_weights[0], category_weights[1],
           category_weights[2], category_weights[3]);

    for (int t = 0; t < num_random_tests; t++) {
        uint16_t a[2], b[2];
        a[0] = gen_random_bf16();
        a[1] = gen_random_bf16();
        b[0] = gen_random_bf16();
        b[1] = gen_random_bf16();

        char name[128];
        snprintf(name, sizeof(name), "random[%d]", t);
        run_test(name, a, b,
                 &rand_pass, &rand_fail,
                 max_failures_to_print, &printed_failures);
    }
    printf("  Random: %d passed, %d failed\n\n", rand_pass, rand_fail);

    pass_count += rand_pass;
    fail_count += rand_fail;

    /* ── Summary ────────────────────────────────────────────────────── */
    printf("=== Summary ===\n");
    printf("  Total: %d tests, %d passed, %d failed\n",
           pass_count + fail_count, pass_count, fail_count);

    if (fail_count > 0) {
        if (printed_failures < fail_count)
            printf("  (%d additional failures not printed, use --max-fail to increase)\n",
                   fail_count - printed_failures);
        printf("  RESULT: FAIL\n");
        return 1;
    }

    printf("  RESULT: PASS\n");
    return 0;
}
