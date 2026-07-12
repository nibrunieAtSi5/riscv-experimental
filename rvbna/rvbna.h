#ifndef RVBNA_H
#define RVBNA_H

/*
 * Pure C implementation of the RISC-V Bulk Normalization Algorithm (RVBNA)
 * as specified in rvbna.adoc.
 *
 * This implementation follows the same computational approach as the
 * reference C++ implementation in bulknormdot.h, translated to pure C99.
 * The exponent space is rebased into the output format (FP32) domain during
 * product exponent evaluation, which simplifies the final normalization.
 */

#include <stdint.h>
#include <stdbool.h>

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

/** Floor log2 of a non-zero uint64_t (position of highest set bit).
 *  Returns -1 when val == 0. */
static inline int rvbna_int_log2(uint64_t val)
{
    int res = 0;
    if (val == 0) return -1;
    while (val >>= 1)
        res++;
    return res;
}

/** Shift right with jamming: if any shifted-out bit was set, force LSB to 1. */
static inline uint64_t rvbna_shift_right_jam(uint64_t n, int amt)
{
    if (amt <= 0) return n;
    int width = 64;
    uint64_t shifted  = amt >= width ? 0 : n >> amt;
    uint64_t jam_mask = amt >= width ? (uint64_t)-1 : ((uint64_t)1 << amt) - 1;
    int jam = (n & jam_mask) != 0;
    return shifted | jam;
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
 * BulkNormalizedDotProduct — C implementation of the RVBNA algorithm.
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
    const int f   = cfg->f;
    const int q   = cfg->q;
    const int g   = cfg->g;

    /* output format constants */
    const int res_mant_bits = q - 1;                /* e.g. 23 for FP32 */
    const int overflowExp   = (1 << f) - 1;         /* e.g. 255 for FP32 */
    const int lhs_bias      = (1 << (e_l - 1)) - 1; /* e.g. 127 for BF16 */
    const int rhs_bias      = (1 << (e_r - 1)) - 1;
    const int res_bias      = (1 << (f  - 1)) - 1;  /* e.g. 127 for FP32 */
    const int bias_offset   = res_bias - (lhs_bias + rhs_bias);

    const uint32_t maskExpLHS  = (1u << e_l) - 1;
    const uint32_t maskExpRHS  = (1u << e_r) - 1;
    const uint32_t maskMantLHS = (1u << m_l) - 1;
    const uint32_t maskMantRHS = (1u << m_r) - 1;

    /* per-product arrays */
    #define RVBNA_MAX_N 64
    int      prodRefExps[RVBNA_MAX_N]; /* reference exponents, rebased to output format */
    int      prodSigns[RVBNA_MAX_N];
    uint64_t prodSigs[RVBNA_MAX_N];

    /* special-case predicates */
    bool anyNaN        = false;
    bool anyInvalidNaN = false;
    bool anySigNaN     = false;
    bool anyPosInf     = false;
    bool anyNegInf     = false;

    rvbna_result_t result;
    result.flags = 0;

    /* ────────── Phase 1: extract, detect specials, compute products ── */
    for (int i = 0; i < n; i++) {
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

        /* corner cases */
        bool A_i_isInf  = (A_i_exp == maskExpLHS) && (A_i_mant == 0);
        bool B_i_isInf  = (B_i_exp == maskExpRHS) && (B_i_mant == 0);
        bool A_i_isNaN  = (A_i_exp == maskExpLHS) && (A_i_mant != 0);
        bool B_i_isNaN  = (B_i_exp == maskExpRHS) && (B_i_mant != 0);
        bool A_i_isSNaN = A_i_isNaN && ((A_i_mant & (1u << (m_l - 1))) == 0);
        bool B_i_isSNaN = B_i_isNaN && ((B_i_mant & (1u << (m_r - 1))) == 0);

        bool either_inf  = A_i_isInf || B_i_isInf;
        bool either_nan  = A_i_isNaN || B_i_isNaN;
        bool either_zero = A_i_isZero || B_i_isZero;

        anyPosInf |= either_inf && !either_nan && !either_zero && (A_i_sign == B_i_sign);
        anyNegInf |= either_inf && !either_nan && !either_zero && (A_i_sign != B_i_sign);

        anyInvalidNaN |= (A_i_isInf && B_i_isZero) || (B_i_isInf && A_i_isZero);
        anyNaN |= anyInvalidNaN || A_i_isNaN || B_i_isNaN;
        anySigNaN |= A_i_isSNaN || B_i_isSNaN;

        /* significand (with implicit bit) */
        uint64_t A_i_sig = ((uint64_t)(!A_i_isSub) << m_l) | A_i_mant;
        uint64_t B_i_sig = ((uint64_t)(!B_i_isSub) << m_r) | B_i_mant;
        prodSigs[i] = A_i_sig * B_i_sig;

        /* Reference exponent, rebased into the output exponent domain.
         * For subnormals, the biased exponent is treated as 1 (emin_normal).
         * For zero products, set to a minimal value so they don't affect maxExp.
         *
         * expSubFixed = biased_exp + (biased_exp == 0 ? 1 : 0)
         *             = biased_exp == 0 ? 1 : biased_exp
         */
        int A_i_ref_exp = A_i_isSub ? 1 : (int)A_i_exp;
        int B_i_ref_exp = B_i_isSub ? 1 : (int)B_i_exp;

        if (A_i_isZero || B_i_isZero) {
            /* Minimal exponent for zero products: bias_offset ensures it's
             * at the bottom of the output exponent range */
            prodRefExps[i] = bias_offset;
        } else {
            prodRefExps[i] = A_i_ref_exp + B_i_ref_exp + bias_offset;
        }
    }

    /* ────────── Find maxExp ──────────────────────────────────────────── */
    int maxExp = prodRefExps[0];
    for (int i = 1; i < n; i++) {
        if (prodRefExps[i] > maxExp)
            maxExp = prodRefExps[i];
    }

    /* ────────── Special case: NaN / Infinity ─────────────────────────── */
    bool anyInf     = anyPosInf || anyNegInf;
    bool opSignInf  = anyPosInf && anyNegInf;
    bool nanOut     = anyNaN || opSignInf;

    if (nanOut) {
        if (anySigNaN || anyInvalidNaN || opSignInf)
            result.flags |= RVBNA_FLAG_INVALID;
        /* canonical quiet NaN */
        result.value = ((uint32_t)overflowExp << res_mant_bits)
                     | (1u << (res_mant_bits - 1));
        return result;
    }
    if (anyInf) {
        int infSign = anyNegInf ? 1 : 0;
        result.value = ((uint32_t)infSign << (f + res_mant_bits))
                     | ((uint32_t)overflowExp << res_mant_bits);
        return result;
    }

    /* ────────── Phase 2: align products via shift-right-jam, accumulate ── */
    int64_t acc = 0;
    bool acc_sign = false; /* assume accumulator is positive */

    for (int i = 0; i < n; i++) {
        uint64_t prod_sig = prodSigs[i];

        /* Pad the product significand so the fractional part width is
         * res_mant_bits + guardBits. The product significand has
         * (m_l + m_r) fractional bits, so we left-shift by the difference. */
        prod_sig <<= res_mant_bits - m_l - m_r + g;

        int shiftAmt = maxExp - prodRefExps[i];
        uint64_t shifted_sig = rvbna_shift_right_jam(prod_sig, shiftAmt);

        if (prodSigns[i] != (int)acc_sign)
            acc -= (int64_t)shifted_sig;
        else
            acc += (int64_t)shifted_sig;
    }

    /* ────────── Phase 3: normalize to output format ──────────────────── */
    bool sign = (acc < 0) != acc_sign;
    uint64_t mag = acc < 0 ? (uint64_t)(-(acc + 1)) + 1 : (uint64_t)acc;

    if (mag == 0) {
        /* exact zero → always +0 */
        result.value = 0;
        return result;
    }

    int norm_dist = rvbna_int_log2(mag);
    int exp = maxExp - res_mant_bits - g + norm_dist;

    /* For subnormal results, reduce the number of significand bits
     * to preserve to match the denormalized representation. */
    int sig_bits = (exp <= 0) ? res_mant_bits - (1 - exp) : res_mant_bits;
    if (sig_bits < 0) sig_bits = 0;
    uint32_t rounded_sig = (uint32_t)rvbna_shift_right_jam(mag << sig_bits, norm_dist);

    bool overflow     = (exp >= overflowExp && mag != 0);
    bool overflowflag = overflow && !anyInf && !nanOut;

    if (overflow) {
        if (overflowflag)
            result.flags |= RVBNA_FLAG_OVERFLOW;
        result.value = ((uint32_t)sign << (f + res_mant_bits))
                     | ((uint32_t)overflowExp << res_mant_bits);
        return result;
    }

    if (exp <= 0) {
        exp = 0;
        /* rounded_sig was already computed with reduced sig_bits → denormalized */
    }

    uint32_t mant_mask = (1u << res_mant_bits) - 1;
    result.value = (rounded_sig & mant_mask)
                 | ((uint32_t)exp << res_mant_bits)
                 | ((uint32_t)sign << (f + res_mant_bits));
    return result;

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
