#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <riscv_vector.h>

#ifndef LMUL
#define LMUL m1
#endif

#define _PASTE_3(a, b, c) a ## b ## c
#define PASTE_3(a, b, c) _PASTE_3(a, b, c)

#define _PASTE_5(a, b, c, d, e) a ## b ## c ## d ## e
#define PASTE_5(a, b, c, d, e) _PASTE_5(a, b, c, d, e)

// Types
#define VUINT64_T PASTE_3(vuint64, LMUL, _t)
#define VUINT8_T PASTE_3(vuint8, LMUL, _t)

// Intrinsics
#define VCLMUL_VV PASTE_3(__riscv_vclmul_vv_u64, LMUL, )
#define VCLMUL_VX PASTE_3(__riscv_vclmul_vx_u64, LMUL, )
#define VCLMULH_VV PASTE_3(__riscv_vclmulh_vv_u64, LMUL, )
#define VCLMULH_VX PASTE_3(__riscv_vclmulh_vx_u64, LMUL, )
#define VLE64_V PASTE_3(__riscv_vle64_v_u64, LMUL, )
#define VLE8_V PASTE_3(__riscv_vle8_v_u8, LMUL, )
#define VMV_V_X PASTE_3(__riscv_vmv_v_x_u64, LMUL, )
#define VMV_X_S PASTE_3(__riscv_vmv_x_s_u64, LMUL, _u64)
#define VREDXOR_VS PASTE_3(__riscv_vredxor_vs_u64, LMUL, _u64m1)
#define VREINTERPRET_V_U8_U64 PASTE_5(__riscv_vreinterpret_v_u8, LMUL, _u64, LMUL, )
#define VSLIDEDOWN_VX PASTE_3(__riscv_vslidedown_vx_u64, LMUL, )
#define VSLL_VX PASTE_3(__riscv_vsll_vx_u64, LMUL, )
#define VSRL_VX PASTE_3(__riscv_vsrl_vx_u64, LMUL, )
#define VXOR_VV PASTE_3(__riscv_vxor_vv_u64, LMUL, )
#define VXOR_VV_TU PASTE_3(__riscv_vxor_vv_u64, LMUL, _tu)
#define VGET_V_U64M1 PASTE_3(__riscv_vget_v_u64, LMUL, _u64m1)

/** Carry-less multiply based implementation of a CRC32LE on a 64-bit input data (properly aligned).
 *  Assumption: only the first element of the data vector is to be considered.
 */
static inline VUINT64_T crc32_le_clmul64_v2(VUINT64_T data) {
    // REV_CRC32_BE_INV_EXT=0xb4e5b025f7011641
    uint64_t REV_CRC32_BE_INV_EXT = 0xb4e5b025f7011641ull;
    VUINT64_T rev_q = VCLMUL_VX(data, REV_CRC32_BE_INV_EXT, 1);
    // bit_reverse(FULL_CRC32_POLY_BE << 31, 64)) = 0x1db710641
    uint64_t rem_cst = 0x1db710641ull; 
    VUINT64_T remainder = VCLMULH_VX(rev_q, rem_cst, 1);
    return remainder;
}

static inline vuint64m1_t crc32_le_clmul64_v2_m1(vuint64m1_t data) {
    // REV_CRC32_BE_INV_EXT=0xb4e5b025f7011641
    uint64_t REV_CRC32_BE_INV_EXT = 0xb4e5b025f7011641ull;
    vuint64m1_t rev_q = __riscv_vclmul_vx_u64m1(data, REV_CRC32_BE_INV_EXT, 1);
    // bit_reverse(FULL_CRC32_POLY_BE << 31, 64)) = 0x1db710641
    uint64_t rem_cst = 0x1db710641ull; 
    vuint64m1_t remainder = __riscv_vclmulh_vx_u64m1(rev_q, rem_cst, 1);
    return remainder;
}


/** Folding based implementation of CRC32LE using vector carry-less multiplication.
 */
uint32_t rv_crc32_le_vector_clmul_fold(uint32_t crc, unsigned char const *p, size_t len) {
    // FIXME: currently only crc=0 value is supported (value is never injected)

    const size_t numBytesPerMainIteration = 16;
    const size_t numE64PerMainIteration = numBytesPerMainIteration / 8;

    // RmRev=0x9ba54c6f00000000 R64Rev=0xb8bc676500000000
    const uint64_t rmRev = 0x9ba54c6f00000000ull; // for m=127
    const uint64_t r255Rev = 0x1b5fd1d00000000ull;
    const uint64_t r64Rev = 0xb8bc676500000000ull; 
    VUINT64_T acc = VMV_V_X(0, numE64PerMainIteration);
    vuint64m1_t acc1 = __riscv_vmv_v_x_u64m1(0, 1);
    // Handling buffer alignment to ensure we can use 64-bit element vector loads (vle64),
    // without risking slowdown or trap on micro-architectures which do not support them (efficiently).
    if (len >= numBytesPerMainIteration && ((size_t) p & 7) != 0) {
          size_t p_align_len = (size_t) p & 7;
          VUINT8_T byte_data = VLE8_V(p, p_align_len);
          VUINT64_T data = VREINTERPRET_V_U8_U64(byte_data);
          size_t align_shift = 8 * (8 - p_align_len);
          data = VSLL_VX(data, align_shift, 1);
          // FIXME: this is brittle as we need elements > 0 to be zero in zero (undisturbed compared to init)
          acc = crc32_le_clmul64_v2(data);
          p += p_align_len;
          len -= p_align_len;
    }

    // pre-computing loop boundaries to allow single update (pointer) in loop body
    int num_main_iterations = (len / numBytesPerMainIteration) - 1;
    num_main_iterations = num_main_iterations < 0 ? 0 : num_main_iterations;
    const uint8_t * p_limit = p + (num_main_iterations * numBytesPerMainIteration); 

    for (; p < p_limit; p += numBytesPerMainIteration) {
          // since we have aligned p to a 8-byte boundary, we can safely load 64-bit elements
          // for the message (this should not be too slow / trap on uarchs which do not support mis-aligned
          // accesses natively). This saves somes vsetvli change.
          VUINT64_T data = VLE64_V((const unsigned long int *) p, numE64PerMainIteration);
          data = VXOR_VV(acc, data, numE64PerMainIteration);
          VUINT64_T folded_rem_hi = VCLMULH_VX(data, rmRev, numE64PerMainIteration);
          VUINT64_T folded_rem_lo = VCLMUL_VX(data, rmRev, numE64PerMainIteration);

          VUINT64_T lo_rem = VCLMULH_VX(folded_rem_lo, r64Rev, numE64PerMainIteration);
          acc = VXOR_VV(lo_rem, folded_rem_hi, numE64PerMainIteration);
    }
    len -= num_main_iterations * numBytesPerMainIteration;
    
    // New 16-byte data and accumulator handling
    if (len >= numBytesPerMainIteration) {
          VUINT64_T data = VLE64_V((const unsigned long int *) p, numE64PerMainIteration);
          data = VXOR_VV(acc, data, numE64PerMainIteration);
#if 0
          VUINT64_T acc_hi = data;
          VUINT64_T acc_lo = VSLIDEDOWN_VX(data, 1, 1);
          VUINT64_T remainder = crc32_le_clmul64_v2(acc_hi);
          remainder = VXOR_VV(remainder, acc_lo, 1);
          acc = crc32_le_clmul64_v2(remainder);
#else
          const uint64_t lastFoldingCsts[2] = {r64Rev};
          VUINT64_T vlastFoldingCsts = VLE64_V(lastFoldingCsts, numE64PerMainIteration);

          VUINT64_T folded_rem_hi = VCLMULH_VV(data, vlastFoldingCsts, numE64PerMainIteration);
          VUINT64_T folded_rem_lo = VCLMUL_VV(data, vlastFoldingCsts, numE64PerMainIteration);

          VUINT64_T lo_rem = VCLMULH_VV(folded_rem_lo, vlastFoldingCsts, numE64PerMainIteration);
          acc = VXOR_VV_TU(data, lo_rem, folded_rem_hi, numE64PerMainIteration - 1);
          acc1 = __riscv_vmv_v_x_u64m1(0, 1);
          acc1 = VREDXOR_VS(acc, acc1, numE64PerMainIteration);
          acc1 = crc32_le_clmul64_v2_m1(acc1);
#endif
          len -= numBytesPerMainIteration;
          p += numBytesPerMainIteration;
    } else {
#if LMUL == m1
        acc1 = acc;
#else
        acc1 = VGET_V_U64M1(acc, 0);
#endif
    }

    for (; len >= 8; len -=8, p += 8) {
          vuint64m1_t data = __riscv_vle64_v_u64m1((const unsigned long int*) p, 1);
          acc1 = __riscv_vxor_vv_u64m1(acc1, data, 1);
          acc1 = crc32_le_clmul64_v2_m1(acc1);
    }
    // Because the pointer alignment is done initially (before the main loop),
    // we need to handle the non multiple of 8 size in the epilog.
    if (len != 0) {
          vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, len);
          vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
          size_t align_shift = 8 * (8 - len);
          data = __riscv_vsll_vx_u64m1(data, align_shift, 1); 
          vuint64m1_t crc_hi = __riscv_vsll_vx_u64m1(acc1, align_shift, 1); 
          vuint64m1_t crc_lo = __riscv_vsrl_vx_u64m1(acc1, 64 - align_shift, 1); 
          // FIXME: this is brittle as we need elements > 0 to be zero in zero (undisturbed compared to init)
          data = __riscv_vxor_vv_u64m1(data, crc_hi, 1); 
          acc1 = crc32_le_clmul64_v2_m1(data);
          acc1 = __riscv_vxor_vv_u64m1(crc_lo, acc1, 1); 
    }

    crc = __riscv_vmv_x_s_u64m1_u64(acc1);
    return (uint32_t) crc;
}
