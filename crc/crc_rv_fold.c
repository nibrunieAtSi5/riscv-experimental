#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <riscv_vector.h>

#ifndef LMUL
#define LMUL 8
#endif

#define _PASTE_3(a, b, c) a ## b ## c
#define PASTE_3(a, b, c) _PASTE_3(a, b, c)

#define _PASTE_5(a, b, c, d, e) a ## b ## c ## d ## e
#define PASTE_5(a, b, c, d, e) _PASTE_5(a, b, c, d, e)

// Types
#define VUINT64_T PASTE_3(vuint64m, LMUL, _t)
#define VUINT8_T PASTE_3(vuint8m, LMUL, _t)

// Intrinsics
#define VCLMUL_VV PASTE_3(__riscv_vclmul_vv_u64m, LMUL, )
#define VCLMUL_VX PASTE_3(__riscv_vclmul_vx_u64m, LMUL, )
#define VCLMULH_VV PASTE_3(__riscv_vclmulh_vv_u64m, LMUL, )
#define VCLMULH_VX PASTE_3(__riscv_vclmulh_vx_u64m, LMUL, )
#define VLE64_V PASTE_3(__riscv_vle64_v_u64m, LMUL, )
#define VLE8_V PASTE_3(__riscv_vle8_v_u8m, LMUL, )
#define VMV_V_X PASTE_3(__riscv_vmv_v_x_u64m, LMUL, )
#define VMV_X_S PASTE_3(__riscv_vmv_x_s_u64m, LMUL, _u64)
#define VREDXOR_VS PASTE_3(__riscv_vredxor_vs_u64m, LMUL, _u64m1)
#define VREINTERPRET_V_U8_U64 PASTE_5(__riscv_vreinterpret_v_u8m, LMUL, _u64m, LMUL, )
#define VSLIDEDOWN_VX PASTE_3(__riscv_vslidedown_vx_u64m, LMUL, )
#define VSLL_VX PASTE_3(__riscv_vsll_vx_u64m, LMUL, )
#define VSRL_VX PASTE_3(__riscv_vsrl_vx_u64m, LMUL, )
#define VXOR_VV PASTE_3(__riscv_vxor_vv_u64m, LMUL, )
#define VXOR_VV_TU PASTE_3(__riscv_vxor_vv_u64m, LMUL, _tu)
#define VGET_V_U64M1 PASTE_3(__riscv_vget_v_u64m, LMUL, _u64m1)

const uint64_t REV_CRC32_BE_INV_EXT = 0xb4e5b025f7011641ull;
const uint64_t rem_cst = 0x1db710641ull; 

const uint64_t r64Rev  = 0xb8bc676500000000ull; // for m=63
const uint64_t r128Rev = 0x9ba54c6f00000000ull; // for m=127
const uint64_t r192Rev = 0x65673b4600000000ull; // [reverse on 64-bit] for X^191
const uint64_t r256Rev = 0x1b5fd1d00000000ull; // [reverse on 64-bit] for X^255
const uint64_t r320Rev = 0x9570d49500000000ull; // [reverse on 64-bit] for X^319
const uint64_t r384Rev = 0x2a28386200000000ull; // [reverse on 64-bit] for X^383
const uint64_t r448Rev = 0x69ccfc0d00000000ull; // [reverse on 64-bit] for X^447
const uint64_t r512Rev = 0xcad38e8f00000000ull; // [reverse on 64-bit] for X^511
const uint64_t r576Rev = 0x653d982200000000ull; // [reverse on 64-bit] for X^575
const uint64_t r640Rev = 0x8e42b13e00000000ull; // [reverse on 64-bit] for X^639
const uint64_t r704Rev = 0x5a03a0cf00000000ull; // [reverse on 64-bit] for X^703
const uint64_t r768Rev = 0x101a233100000000ull; // [reverse on 64-bit] for X^767
const uint64_t r832Rev = 0x759fc69d00000000ull; // [reverse on 64-bit] for X^831
const uint64_t r896Rev = 0xc64ac0b800000000ull; // [reverse on 64-bit] for X^895
const uint64_t r960Rev = 0x19866e800000000ull; // [reverse on 64-bit] for X^959
const uint64_t r1024Rev = 0x7406fa9500000000ull; // [reverse on 64-bit] for X^1023
const uint64_t r2048Rev = 0x3f9f86300000000ull; // [reverse on 64-bit] for X^2047
const uint64_t allLastFoldingCsts[17] = {
    r1024Rev,
    r960Rev, r896Rev, r832Rev, r768Rev, r704Rev, r640Rev, r576Rev, r512Rev,
    r448Rev, r384Rev, r320Rev, r256Rev, r192Rev, r128Rev, r64Rev, 0
};
/** Carry-less multiply based implementation of a CRC32LE on a 64-bit input data (properly aligned).
 *  Assumption: only the first element of the data vector is to be considered.
 */
static inline VUINT64_T crc32_le_clmul64_v2(VUINT64_T data) {
    // REV_CRC32_BE_INV_EXT=0xb4e5b025f7011641
    VUINT64_T rev_q = VCLMUL_VX(data, REV_CRC32_BE_INV_EXT, 1);
    // bit_reverse(FULL_CRC32_POLY_BE << 31, 64)) = 0x1db710641
    VUINT64_T remainder = VCLMULH_VX(rev_q, rem_cst, 1);
    return remainder;
}

static inline vuint64m1_t crc32_le_clmul64_v2_m1(vuint64m1_t data) {
    // REV_CRC32_BE_INV_EXT=0xb4e5b025f7011641
    vuint64m1_t rev_q = __riscv_vclmul_vx_u64m1(data, REV_CRC32_BE_INV_EXT, 1);
    // bit_reverse(FULL_CRC32_POLY_BE << 31, 64)) = 0x1db710641
    vuint64m1_t remainder = __riscv_vclmulh_vx_u64m1(rev_q, rem_cst, 1);
    return remainder;
}


/** Folding based implementation of CRC32LE using vector carry-less multiplication.
 */
uint32_t rv_crc32_le_vector_clmul_fold(uint32_t crc, unsigned char const *p, size_t len) {
    // FIXME: currently only crc=0 value is supported (value is never injected)


    const size_t numBytesPerMainIteration = 16 * LMUL;
    const size_t numE64PerMainIteration = numBytesPerMainIteration / 8;

    const uint64_t *lastFoldingCsts = allLastFoldingCsts + 1 + (16 - numE64PerMainIteration);
    const uint64_t rmRev = allLastFoldingCsts[16 - 2 * LMUL];
#if LMUL != 8
    const uint64_t r2mRev = allLastFoldingCsts[16 - 4 * LMUL];
#else
    const uint64_t r2mRev = r2048Rev;
#endif


    // large accumulator
    VUINT64_T acc = VMV_V_X(0, numE64PerMainIteration);
    // 1-element accumulator (final stages)
    vuint64m1_t acc1 = __riscv_vmv_v_x_u64m1(0, 1);

    // Handling buffer alignment to ensure we can use 64-bit element vector loads (vle64),
    // without risking slowdown or trap on micro-architectures which do not support them (efficiently).
    if (len >= numBytesPerMainIteration && ((size_t) p & 7) != 0) {
        // printf("pointer alignment prolog p & 7 = %zu\n", (size_t) p & 7);
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

#if 1
    // double iterations:
    // the first one loads the data and xor them with the accumulator, and reduce both over the second iteration to the new accumulator
    // the second one loads the data and reduce them to the new accumulator index, before xor-ing into it.
    // the high part (*_rem_lo in little-endian) are XOR-ed together before being reduced to the accumulator position (those part are only 32-bit wide, left aligned)
    const uint8_t * p_limit_double_it = p + (num_main_iterations >> 1) * 2 * numBytesPerMainIteration; 
    for (; p < p_limit_double_it; p += 2 * numBytesPerMainIteration) {
          // since we have aligned p to a 8-byte boundary, we can safely load 64-bit elements
          // for the message (this should not be too slow / trap on uarchs which do not support mis-aligned
          // accesses natively). This saves somes vsetvli change.
          VUINT64_T data_hi = VLE64_V((const unsigned long int *) p, numE64PerMainIteration);
          data_hi = VXOR_VV(acc, data_hi, numE64PerMainIteration);
          VUINT64_T folded_hi_rem_hi = VCLMULH_VX(data_hi, r2mRev, numE64PerMainIteration);
          VUINT64_T folded_hi_rem_lo = VCLMUL_VX(data_hi, r2mRev, numE64PerMainIteration);

          VUINT64_T data_lo = VLE64_V((const unsigned long int *) (p + numBytesPerMainIteration), numE64PerMainIteration);
          VUINT64_T folded_lo_rem_hi = VCLMULH_VX(data_lo, rmRev, numE64PerMainIteration);
          VUINT64_T folded_lo_rem_lo = VCLMUL_VX(data_lo, rmRev, numE64PerMainIteration);

        VUINT64_T folded_rem_lo = VXOR_VV(folded_hi_rem_lo, folded_lo_rem_lo, numE64PerMainIteration);
        VUINT64_T folded_rem_hi = VXOR_VV(folded_hi_rem_hi, folded_lo_rem_hi, numE64PerMainIteration);

          // single folding of high part of folded rem
          VUINT64_T lo_rem = VCLMULH_VX(folded_rem_lo, r64Rev, numE64PerMainIteration);
          acc = VXOR_VV(lo_rem, folded_rem_hi, numE64PerMainIteration);
    }
#endif

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
    
    // New (full accumulatore size)-byte data and accumulator handling
    if (len >= numBytesPerMainIteration) {
        // printf("%zu-byte folding (len=%zu)\n", numBytesPerMainIteration, len);
          VUINT64_T data = VLE64_V((const unsigned long int *) p, numE64PerMainIteration);
          data = VXOR_VV(acc, data, numE64PerMainIteration);
          VUINT64_T vlastFoldingCsts = VLE64_V(lastFoldingCsts, numE64PerMainIteration);

          VUINT64_T folded_rem_hi = VCLMULH_VV(data, vlastFoldingCsts, numE64PerMainIteration);
          VUINT64_T folded_rem_lo = VCLMUL_VV(data, vlastFoldingCsts, numE64PerMainIteration);

          VUINT64_T lo_rem = VCLMULH_VX(folded_rem_lo, r64Rev, numE64PerMainIteration);
          acc = VXOR_VV_TU(data, lo_rem, folded_rem_hi, numE64PerMainIteration - 1);
          // reduction require a LMUL=1 result whatever the actual implementation LMUL
          acc1 = __riscv_vmv_v_x_u64m1(0, 1);
          acc1 = VREDXOR_VS(acc, acc1, numE64PerMainIteration);
          acc1 = crc32_le_clmul64_v2_m1(acc1);

          len -= numBytesPerMainIteration;
          p += numBytesPerMainIteration;
    } else {
#if LMUL == 1
        acc1 = acc;
#else
        acc1 = VGET_V_U64M1(acc, 0);
#endif
    }

    for (; len >= 8; len -=8, p += 8) {
        // printf("8-byte loop\n");
        vuint64m1_t data = __riscv_vle64_v_u64m1((const unsigned long int*) p, 1);
        acc1 = __riscv_vxor_vv_u64m1(acc1, data, 1);
        acc1 = crc32_le_clmul64_v2_m1(acc1);
    }
    // Because the pointer alignment is done initially (before the main loop),
    // we need to handle the non multiple of 8 size in the epilog.
    if (len != 0) {
        //printf("final unaligned len=%zu\n", len);
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
