/*
 * C++ wrapper exposing bulknormdot.h's bulk_norm_dot_bf16 as a C-callable
 * function for the benchmark.
 */

#include "bulknormdot.h"

extern "C" {

/**
 * C-callable wrapper around bulk_norm_dot_bf16.
 *
 * @param a_raw   Array of n raw BF16 bit patterns (uint16_t).
 * @param b_raw   Array of n raw BF16 bit patterns (uint16_t).
 * @param n       Number of products.
 * @param out_value  Output: the FP32 result as uint32_t.
 * @param out_flags  Output: exception flags.
 */
void ref_bulk_norm_dot_bf16(const uint16_t *a_raw,
                            const uint16_t *b_raw,
                            int n,
                            uint32_t *out_value,
                            uint8_t  *out_flags)
{
    /* Convert raw uint16_t to bf16_t objects */
    bf16_t a[64];
    bf16_t b[64];
    for (int i = 0; i < n; i++) {
        a[i] = bf16_t(a_raw[i]);
        b[i] = bf16_t(b_raw[i]);
    }

    /* The guard bits for bulknormdot.h are ceil(log2(n)) */
    int guardBits = 0;
    {
        int v = n - 1;
        while (v > 0) { v >>= 1; guardBits++; }
    }

    DotConfig cfg(n, guardBits);
    bulk_norm_out_t result = bulk_norm_dot_bf16(cfg, a, b);

    *out_value = result.out;
    *out_flags = result.flags;
}

} /* extern "C" */
