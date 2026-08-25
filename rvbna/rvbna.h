#ifndef RVBNA_H
#define RVBNA_H

/*
 * Pure C implementation of the RISC-V Bulk Normalization Algorithm (RVBNA)
 * as specified in rvbna.adoc (the pseudo-code in lines 84-240).
 *
 * This is a direct, faithful translation of the pseudo-code.
 * It is parameterized via rvbna_config_t for different format combinations.
 */

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/* ── Exception flags (matching Berkeley SoftFloat-3 definitions) ─────── */
#define RVBNA_FLAG_INVALID   0x10
#define RVBNA_FLAG_OVERFLOW  0x04

/* ── Configuration ───────────────────────────────────────────────────── */

typedef struct {
    int e_l;   /* exponent width of left-hand-side operand  */
    int m_l;   /* mantissa width of LHS (significand = m_l + 1 bits) */
    int e_r;   /* exponent width of right-hand-side operand */
    int m_r;   /* mantissa width of RHS (significand = m_r + 1 bits) */
    int f;     /* exponent width of the result              */
    int q;     /* significand width of the result (mantissa = q-1) */
    int n;     /* number of products                        */
    int g;     /* number of guard bits   = ceil(log2(n))    */
    int o;     /* number of overflow bits = ceil(log2(n))   */
} rvbna_config_t;

/* ── Result ──────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t value;   /* IEEE-754 encoded result */
    uint8_t  flags;   /* exception flags         */
} rvbna_result_t;

/* ── Helpers ─────────────────────────────────────────────────────────── */

/** ceil(log2(n)) for small positive n */
static inline int rvbna_clog2(int n)
{
    int r = 0;
    int v = n - 1;
    while (v > 0) { v >>= 1; r++; }
    return r;
}

/** Leading-zero count on a uint64_t value.
 *  Returns 64 when val == 0. */
static inline int rvbna_clz64(uint64_t val)
{
    if (val == 0) return 64;
    int c = 0;
    if (!(val & 0xFFFFFFFF00000000ULL)) { c += 32; val <<= 32; }
    if (!(val & 0xFFFF000000000000ULL)) { c += 16; val <<= 16; }
    if (!(val & 0xFF00000000000000ULL)) { c +=  8; val <<=  8; }
    if (!(val & 0xF000000000000000ULL)) { c +=  4; val <<=  4; }
    if (!(val & 0xC000000000000000ULL)) { c +=  2; val <<=  2; }
    if (!(val & 0x8000000000000000ULL)) { c +=  1; }
    return c;
}

/* ── Pre-configured format helpers ───────────────────────────────────── */

/** Return a config for BF16 × BF16 → FP32 dot product with n products. */
static inline rvbna_config_t rvbna_config_bf16_fp32(int n)
{
    rvbna_config_t cfg;
    cfg.e_l = 8;  cfg.m_l = 7;   /* BF16: 8-bit exp, 7-bit mantissa */
    cfg.e_r = 8;  cfg.m_r = 7;
    cfg.f   = 8;  cfg.q   = 24;  /* FP32: 8-bit exp, 24-bit significand */
    cfg.n   = n;
    cfg.g   = rvbna_clog2(n);
    cfg.o   = rvbna_clog2(n);
    return cfg;
}

/* ── Core algorithm ──────────────────────────────────────────────────── */

/**
 * BulkNormalizedDotProduct — direct translation of the RVBNA pseudo-code
 * from rvbna.adoc lines 84-240.
 *
 * @param cfg   Algorithm configuration (format widths, n, g, o).
 * @param A     Array of n left-hand-side operands  (raw IEEE bit patterns).
 * @param B     Array of n right-hand-side operands (raw IEEE bit patterns).
 * @return      The IEEE-encoded result and exception flags.
 *
 * The operands are passed as uint16_t (suitable for BF16 or FP16).
 * The result is uint32_t (suitable for FP32).
 */
static inline rvbna_result_t rvbna_dot(const rvbna_config_t *cfg,
                                       const uint16_t *A,
                                       const uint16_t *B)
{
    const int n   = cfg->n;
    const int e_l = cfg->e_l;
    const int e_r = cfg->e_r;
    const int m_l = cfg->m_l;
    const int m_r = cfg->m_r;
    const int p_l = m_l + 1;
    const int p_r = m_r + 1;
    const int f   = cfg->f;
    const int q   = cfg->q;
    const int g   = cfg->g;
    const int o   = cfg->o;

    const uint32_t maskExpLHS  = (1u << e_l) - 1;
    const uint32_t maskExpRHS  = (1u << e_r) - 1;
    const uint32_t maskMantLHS = (1u << m_l) - 1;
    const uint32_t maskMantRHS = (1u << m_r) - 1;

    /* boundary for exponent overflow (output format)
     * this is also the output exponent for infinity and NaN */
    const int overflowExp = (1 << f) - 1;
    const int lhs_bias    = (1 << (e_l - 1)) - 1;
    const int rhs_bias    = (1 << (e_r - 1)) - 1;
    const int res_bias    = (1 << (f  - 1)) - 1;
    const int prodOpBias  = lhs_bias + rhs_bias;

    /* per-product arrays — fixed max size */
    #define RVBNA_MAX_N 64
    int      prodRefExps[RVBNA_MAX_N];
    int      prodSigns[RVBNA_MAX_N];
    uint64_t prodSigs[RVBNA_MAX_N];

    int maxExp = 0;

    /* predicate output special cases */
    bool nanResult      = false;   /* expected Not a Number (NaN) result */
    bool infiniteResult = false;   /* expected infinite result */
    bool invalidFlag    = false;   /* invalid operation flag */
    int  infiniteSign   = 0;       /* sign of infinite result */

    rvbna_result_t result;
    result.flags = 0;

    /* ── determining maximum reference exponent (pseudo-code lines 114-163) ── */
    for (int i = 0; i < n; i++) {
        /* extracting A[i] and B[i]'s encoded exponents */
        uint32_t A_i_exp  = ((uint32_t)A[i] >> m_l) & maskExpLHS;
        uint32_t B_i_exp  = ((uint32_t)B[i] >> m_r) & maskExpRHS;
        uint32_t A_i_mant = (uint32_t)A[i] & maskMantLHS;
        uint32_t B_i_mant = (uint32_t)B[i] & maskMantRHS;
        int A_i_sign      = ((uint32_t)A[i] >> (e_l + m_l)) & 1;
        int B_i_sign      = ((uint32_t)B[i] >> (e_r + m_r)) & 1;

        prodSigns[i] = A_i_sign ^ B_i_sign;

        bool A_i_isSub  = (A_i_exp == 0);
        bool B_i_isSub  = (B_i_exp == 0);
        bool A_i_isZero = A_i_isSub && (A_i_mant == 0);
        bool B_i_isZero = B_i_isSub && (B_i_mant == 0);
        bool prod_isZero = A_i_isZero || B_i_isZero;

        /* detecting corner cases */
        bool A_i_isInf  = (A_i_exp == maskExpLHS) && (A_i_mant == 0);
        bool B_i_isInf  = (B_i_exp == maskExpRHS) && (B_i_mant == 0);
        bool A_i_isNaN  = (A_i_exp == maskExpLHS) && (A_i_mant != 0);
        bool B_i_isNaN  = (B_i_exp == maskExpRHS) && (B_i_mant != 0);
        bool A_i_isSNaN = A_i_isNaN && ((A_i_mant & (1u << (m_l - 1))) == 0);
        bool B_i_isSNaN = B_i_isNaN && ((B_i_mant & (1u << (m_r - 1))) == 0);

        bool invalidProd     = (A_i_isInf && B_i_isZero) || (B_i_isInf && A_i_isZero);
        bool infiniteProdLHS = A_i_isInf && !B_i_isNaN && !B_i_isZero;
        bool infiniteProdRHS = B_i_isInf && !A_i_isNaN && !A_i_isZero;
        bool infiniteProd    = infiniteProdLHS || infiniteProdRHS;
        bool invalidSum      = infiniteResult && infiniteProd && (infiniteSign != prodSigns[i]);

        infiniteResult = infiniteResult || infiniteProd;
        invalidFlag    = invalidFlag || invalidProd || invalidSum || A_i_isSNaN || B_i_isSNaN;
        infiniteSign   = infiniteProd ? prodSigns[i] : infiniteSign;

        nanResult = nanResult || A_i_isNaN || B_i_isNaN || invalidProd || invalidSum;

        /* significand (with implicit bit) */
        uint64_t A_i_sig = ((uint64_t)(!A_i_isSub) << (p_l - 1)) | A_i_mant;
        uint64_t B_i_sig = ((uint64_t)(!B_i_isSub) << (p_r - 1)) | B_i_mant;

        prodSigs[i] = A_i_sig * B_i_sig;

        int A_i_ref_exp = A_i_isSub ? 1 : (int)A_i_exp;
        int B_i_ref_exp = B_i_isSub ? 1 : (int)B_i_exp;

        prodRefExps[i] = prod_isZero ? 0 : A_i_ref_exp + B_i_ref_exp;

        if (prodRefExps[i] > maxExp)
            maxExp = prodRefExps[i];
    }

    /* ── early exit for special cases (pseudo-code lines 167-175) ── */
    if (nanResult) {
        if (invalidFlag)
            result.flags |= RVBNA_FLAG_INVALID;
        /* canonical quiet NaN */
        result.value = ((uint32_t)overflowExp << (q - 1)) | (1u << (q - 2));
        return result;
    }
    if (infiniteResult) {
        result.value = ((uint32_t)infiniteSign << (q + f - 1))
                     | ((uint32_t)overflowExp << (q - 1));
        return result;
    }

    /* ── aligning products (pseudo-code lines 177-195) ── */
    uint64_t alignedProducts[RVBNA_MAX_N];
    for (int i = 0; i < n; i++) {
        int alignShift = maxExp - prodRefExps[i];

        /* aligning i-th product */
        int padRight = q + 1 + g - (p_l + p_r);
        assert(((unsigned)padRight + (p_l + p_r)) <= (8 * sizeof(alignedProducts[0])));
        alignedProducts[i] = (alignShift >= (q+1+g)) ? 0 : (prodSigs[i] << padRight) >> alignShift;

        /* evaluating values of discarded bits */
        uint64_t discardedMask;
        int dm_shift = q + 1 + g - alignShift;
        if (dm_shift >= (p_l + p_r)) {
            discardedMask = 0;
        } else if (dm_shift <= 0) {
            discardedMask = ((uint64_t)1 << (p_l + p_r)) - 1;
        } else {
            discardedMask =  (((uint64_t)1 << (p_l + p_r)) - 1) >> dm_shift;
        }
        uint64_t discardedBits = prodSigs[i] & discardedMask;
        bool jam = (alignShift >= (q + 1 + g))
                       ? (prodSigs[i] != 0)
                       : (discardedBits != 0);

        alignedProducts[i] |= (jam ? 1 : 0); /* rounding to odd aligned product */
    }

    /* ── accumulating products (pseudo-code lines 198-201) ── */
    int64_t accumulator = 0;
    for (int i = 0; i < n; i++) {
        if (prodSigns[i])
            accumulator -= (int64_t)alignedProducts[i];
        else
            accumulator += (int64_t)alignedProducts[i];
    }

    /* ── computing accumulator absolute value and normalizing (lines 204-239) ── */
    bool accSign = (accumulator < 0);
    uint64_t accAbs = accSign ? (uint64_t)(-(accumulator + 1)) + 1 : (uint64_t)accumulator;

    /* leading zero count assuming (g + q + 1 + o) width */
    int totalWidth = g + q + 1 + o;
    int lzc = rvbna_clz64(accAbs) - (64 - totalWidth);
    if (lzc < 0) lzc = 0;

    if (accAbs == 0) {
        /* a zero result is always +0 */
        result.value = 0;
        return result;
    }

    int resExp = (maxExp + o + 1 - lzc) - prodOpBias + res_bias;

    /* unrounded significand */
    uint64_t shifted = accAbs << lzc;
    uint64_t unroundedSig = shifted >> (g + o + 1);

    /* jam mask for the bits below the significand */
    uint64_t rawJamMask = ((uint64_t)1 << (g + o + 1)) - 1;
    int jamMaskShift = (lzc > (g + o + 1)) ? (g + o + 1) : (lzc);
    uint64_t jamMask = rawJamMask >> jamMaskShift;

    bool jamSig = ((accAbs & jamMask) != 0);
    uint64_t roundedSig = unroundedSig | (jamSig ? 1 : 0);

    if (resExp >= overflowExp) {
        /* overflow */
        result.flags |= RVBNA_FLAG_OVERFLOW;
        result.value = ((uint32_t)accSign << (q + f - 1))
                     | ((uint32_t)overflowExp << (q - 1));
        return result;
    }

    if (resExp >= 1) {
        /* normal output */
        uint32_t roundedMant = (uint32_t)roundedSig & ((1u << (q - 1)) - 1);
        result.value = ((uint32_t)accSign << (q + f - 1))
                     | ((uint32_t)resExp << (q - 1))
                     | roundedMant;
        return result;
    }

    if (resExp < -(q - 1)) {
        /* so tiny the entire significand is jammed into 1 bit */
        result.value = ((uint32_t)accSign << (q + f - 1))
                     | (accAbs != 0 ? 1 : 0);
        return result;
    }

    /* denormalization and final round-to-odd
     * (of bits discarded during denormalization)
     * — pseudo-code lines 231-238 */
    {
        assert(resExp <= 0);
        int denormShift = -resExp;  /* resExp <= 0, so denormShift in [0, q-1] */
        uint64_t denormalizedSig = (accAbs << lzc) >> (g + o + 1 + 1 + denormShift);
        // uint64_t discardedMask = ((uint64_t)((1u << (q - 1)) - 1)) >> denormShift;
        uint64_t discardedMask = ((uint64_t)((1u << (g + o + 1 + 1 + denormShift)) - 1));
        uint64_t discardedBits = (accAbs << lzc)& discardedMask;
        uint32_t forceLSB = (discardedBits != 0 ? 1 : 0);

        result.value = ((uint32_t)accSign << (q + f - 1)) | (uint32_t)denormalizedSig | forceLSB;
        return result;
    }

    #undef RVBNA_MAX_N
}

/* ── Convenience: BF16 × BF16 → FP32 dot product ────────────────────── */

/**
 * Compute the RVBNA dot product of two arrays of BF16 values,
 * producing a single FP32 result.
 *
 * @param a   Array of n BF16 values (raw uint16_t bit patterns).
 * @param b   Array of n BF16 values (raw uint16_t bit patterns).
 * @param n   Number of products (typically 2 or 4).
 * @return    FP32 result with exception flags.
 */
static inline rvbna_result_t rvbna_bf16_dot(const uint16_t *a,
                                            const uint16_t *b,
                                            int n)
{
    rvbna_config_t cfg = rvbna_config_bf16_fp32(n);
    return rvbna_dot(&cfg, a, b);
}

#endif /* RVBNA_H */
