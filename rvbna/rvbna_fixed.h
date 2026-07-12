#ifndef RVBNA_FIXED_H
#define RVBNA_FIXED_H

/*
 * Pure C implementation of the RISC-V Bulk Normalization Algorithm (RVBNA)
 * as specified in rvbna_fixed.adoc.
 *
 * This is a direct, literal translation of the pseudo-code.
 * Large shifts (which exceed the format size) are safely guarded to return 0.
 */

#include <stdint.h>
#include <stdbool.h>

#define RVBNA_FIXED_FLAG_INVALID   0x10
#define RVBNA_FIXED_FLAG_OVERFLOW  0x04

typedef struct {
    int e_l;
    int m_l;
    int e_r;
    int m_r;
    int f;
    int q;
    int n;
    int g;
    int o;
} rvbna_fixed_config_t;

typedef struct {
    uint32_t value;
    uint8_t  flags;
} rvbna_fixed_result_t;

static inline int rvbna_fixed_clog2(int n)
{
    int r = 0;
    int v = n - 1;
    while (v > 0) { v >>= 1; r++; }
    return r;
}

static inline int rvbna_fixed_clz64(uint64_t val)
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

static inline rvbna_fixed_config_t rvbna_fixed_config_bf16_fp32(int n)
{
    rvbna_fixed_config_t cfg;
    cfg.e_l = 8;  cfg.m_l = 7;
    cfg.e_r = 8;  cfg.m_r = 7;
    cfg.f   = 8;  cfg.q   = 24;
    cfg.n   = n;
    cfg.g   = rvbna_fixed_clog2(n);
    cfg.o   = rvbna_fixed_clog2(n);
    return cfg;
}

static inline rvbna_fixed_result_t rvbna_fixed_dot(const rvbna_fixed_config_t *cfg,
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

    int maxExp = 0;
    uint32_t maskExpLHS  = (1u << e_l) - 1;
    uint32_t maskExpRHS  = (1u << e_r) - 1;
    uint32_t maskMantLHS = (1u << m_l) - 1;
    uint32_t maskMantRHS = (1u << m_r) - 1;

    #define RVBNA_FIXED_MAX_N 64
    int      prodRefExps[RVBNA_FIXED_MAX_N] = {0};
    int      prodSigns[RVBNA_FIXED_MAX_N] = {0};
    uint64_t prodSigs[RVBNA_FIXED_MAX_N] = {0};

    int overflowExp = (1 << f) - 1;
    int lhs_bias    = (1 << (e_l - 1)) - 1;
    int rhs_bias    = (1 << (e_r - 1)) - 1;
    int res_bias    = (1 << (f  - 1)) - 1;
    int prodOpBias  = lhs_bias + rhs_bias;

    bool nanResult      = false;
    bool infiniteResult = false;
    bool invalidFlag    = false;
    int  infiniteSign   = 0;

    rvbna_fixed_result_t result;
    result.flags = 0;

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
        bool A_i_isZero = (A_i_isSub && A_i_mant == 0);
        bool B_i_isZero = (B_i_isSub && B_i_mant == 0);
        bool prod_isZero = A_i_isZero || B_i_isZero;

        bool A_i_isInf  = (A_i_exp == maskExpLHS) && (A_i_mant == 0);
        bool B_i_isInf  = (B_i_exp == maskExpRHS) && (B_i_mant == 0);
        bool A_i_isNaN  = (A_i_exp == maskExpLHS) && (A_i_mant != 0);
        bool B_i_isNaN  = (B_i_exp == maskExpRHS) && (B_i_mant != 0);
        bool A_i_isSNaN = A_i_isNaN && ((A_i_mant & (1u << (m_l - 1))) == 0);
        bool B_i_isSNaN = B_i_isNaN && ((B_i_mant & (1u << (m_r - 1))) == 0);

        bool invalidProd     = (A_i_isInf && B_i_isZero) || (B_i_isInf && A_i_isZero);
        bool infiniteProdLHS = (A_i_isInf && !B_i_isNaN && !B_i_isZero);
        bool infiniteProdRHS = (B_i_isInf && !A_i_isNaN && !A_i_isZero);
        bool infiniteProd    = infiniteProdLHS || infiniteProdRHS;
        bool invalidSum      = infiniteResult && infiniteProd && (infiniteSign != prodSigns[i]);

        infiniteResult = infiniteResult || infiniteProd;
        invalidFlag    = invalidFlag || invalidProd || invalidSum || A_i_isSNaN || B_i_isSNaN;
        infiniteSign   = infiniteProd ? prodSigns[i] : infiniteSign;

        nanResult = nanResult || A_i_isNaN || B_i_isNaN || invalidProd || invalidSum;

        uint64_t A_i_sig = ((uint64_t)(!A_i_isSub) << (p_l - 1)) | A_i_mant;
        uint64_t B_i_sig = ((uint64_t)(!B_i_isSub) << (p_r - 1)) | B_i_mant;

        prodSigs[i] = A_i_sig * B_i_sig;

        int A_i_ref_exp = (A_i_isSub ? 1 : (int)A_i_exp);
        int B_i_ref_exp = (B_i_isSub ? 1 : (int)B_i_exp);

        prodRefExps[i] = prod_isZero ? 0 : A_i_ref_exp + B_i_ref_exp;

        maxExp = (prodRefExps[i] > maxExp ? prodRefExps[i] : maxExp);
    }

    if (nanResult) {
        if (invalidFlag) {
            result.flags |= RVBNA_FIXED_FLAG_INVALID;
        }
        result.value = ((uint32_t)overflowExp << (q - 1)) | (1u << (q - 2));
        return result;
    } else if (infiniteResult) {
        result.value = ((uint32_t)infiniteSign << (q + f - 1)) | ((uint32_t)overflowExp << (q - 1));
        return result;
    }

    uint64_t alignedProducts[RVBNA_FIXED_MAX_N] = {0};
    for (int i = 0; i < n; i++) {
        int alignShift = maxExp - prodRefExps[i];

        int padRight = q + 1 + g - (p_l + p_r);
        alignedProducts[i] = (alignShift >= 64) ? 0 : ((prodSigs[i] << padRight) >> alignShift);

        uint64_t discardedMask;
        int shift_amt = q + 1 + g - alignShift;
        if (shift_amt < 0 || shift_amt >= 64) {
            discardedMask = 0;
        } else {
            discardedMask = (((uint64_t)1 << (p_l + p_r)) - 1) >> shift_amt;
        }
        uint64_t discardedBits = prodSigs[i] & discardedMask;
        bool jam = (alignShift >= (q + 1 + g) ? prodSigs[i] : discardedBits) != 0;

        alignedProducts[i] |= (jam ? 1 : 0);
    }

    int64_t accumulator = 0;
    for (int i = 0; i < n; i++) {
        accumulator += prodSigns[i] ? -(int64_t)alignedProducts[i] : (int64_t)alignedProducts[i];
    }

    bool accSign = accumulator < 0;
    uint64_t accAbs = accSign ? -accumulator : accumulator;
    int totalWidth = g + q + 1 + o;
    int lzc = rvbna_fixed_clz64(accAbs) - (64 - totalWidth);
    if (lzc < 0) lzc = 0;

    int resExp = (accumulator == 0) ? 0 : ((maxExp + o + 1 - lzc) - prodOpBias + res_bias);
    
    uint64_t shifted = accAbs << lzc;
    uint64_t unroundedSig = (g + o + 1 >= 64) ? 0 : (shifted >> (g + o + 1));
    
    uint64_t rawJamMask = ((uint64_t)1 << (g + o + 1)) - 1;
    int jamMaskShift = (lzc > (g + o + 1)) ? (g + o + 1) : lzc;
    uint64_t jamMask = (jamMaskShift >= 64) ? 0 : (rawJamMask >> jamMaskShift);

    bool jamSig = (accAbs & jamMask) != 0;
    uint64_t roundedSig = unroundedSig | (jamSig ? 1 : 0);

    if (accAbs == 0) {
        result.value = 0;
        return result;
    } else if (resExp >= overflowExp) {
        result.flags |= RVBNA_FIXED_FLAG_OVERFLOW;
        result.value = ((uint32_t)accSign << (q + f - 1)) | ((uint32_t)overflowExp << (q - 1));
        return result;
    } else if (resExp >= 1) {
        uint32_t roundedMant = roundedSig & (((uint32_t)1 << (q - 1)) - 1);
        result.value = ((uint32_t)accSign << (q + f - 1)) | ((uint32_t)resExp << (q - 1)) | roundedMant;
        return result;
    } else {
        if (resExp < -(q - 1)) {
            result.value = ((uint32_t)accSign << (q + f - 1)) | (accAbs != 0 ? 1 : 0);
            return result;
        } else {
            int denormShift = -resExp;
            int totalShift = g + o + 1 + 1 + denormShift;
            uint64_t denormalizedSig = (totalShift >= 64) ? 0 : ((accAbs << lzc) >> totalShift);
            uint64_t discardedMask = (totalShift >= 64) ? (uint64_t)-1 : (((uint64_t)1 << totalShift) - 1);
            uint64_t discardedBits = (accAbs << lzc) & discardedMask;
            uint32_t forceLSB = (discardedBits != 0 ? 1 : 0);
            result.value = ((uint32_t)accSign << (q + f - 1)) | (uint32_t)denormalizedSig | forceLSB;
            return result;
        }
    }
}

static inline rvbna_fixed_result_t rvbna_fixed_bf16_dot(const uint16_t *a,
                                                        const uint16_t *b,
                                                        int n)
{
    rvbna_fixed_config_t cfg = rvbna_fixed_config_bf16_fp32(n);
    return rvbna_fixed_dot(&cfg, a, b);
}

#endif
