#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <riscv_vector.h>


/** Carry-less multiply based implementation of a CRC32LE on a 64-bit input data (properly aligned).
 *  Assumption: only the first element of the data vector is to be considered.
 */
static inline vuint64m1_t crc32_le_clmul64_v2(vuint64m1_t data) {
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

    // RmRev=0x9ba54c6f00000000 R64Rev=0xb8bc676500000000
    const uint64_t rmRev = 0x9ba54c6f00000000ull;
    const uint64_t r64Rev = 0xb8bc676500000000ull; 
    vuint64m1_t acc = __riscv_vmv_v_x_u64m1(0, 2);
    // Handling buffer alignment to ensure we can use 64-bit element vector loads (vle64),
    // without risking slowdown or trap on micro-architectures which do not support them (efficiently).
    if (len >= 16 && ((size_t) p & 7) != 0) {
          size_t p_align_len = (size_t) p & 7;
          vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, p_align_len);
          vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
          size_t align_shift = 8 * (8 - p_align_len);
          data = __riscv_vsll_vx_u64m1(data, align_shift, 1);
          // FIXME: this is brittle as we need elements > 0 to be zero in zero (undisturbed compared to init)
          acc = crc32_le_clmul64_v2(data);
          p += p_align_len;
          len -= p_align_len;
    }

    // pre-computing loop boundaries to allow single update (pointer) in loop body
    const size_t numBytesPerMainIteration = 16;
    int num_main_iterations = (len / numBytesPerMainIteration) - 1;
    num_main_iterations = num_main_iterations < 0 ? 0 : num_main_iterations;
    const uint8_t * p_limit = p + (num_main_iterations * numBytesPerMainIteration); 

    for (; p < p_limit; p += numBytesPerMainIteration) {
          // since we have aligned p to a 8-byte boundary, we can safely load 64-bit elements
          // for the message (this should not be too slow / trap on uarchs which do not support mis-aligned
          // accesses natively). This saves somes vsetvli change.
          vuint64m1_t data = __riscv_vle64_v_u64m1((const unsigned long int *) p, 2);
          data = __riscv_vxor_vv_u64m1(acc, data, 2);
          vuint64m1_t folded_rem_hi = __riscv_vclmulh_vx_u64m1(data, rmRev, 2);
          vuint64m1_t folded_rem_lo = __riscv_vclmul_vx_u64m1(data, rmRev, 2);

          vuint64m1_t lo_rem = __riscv_vclmulh_vx_u64m1(folded_rem_lo, r64Rev, 2);
          acc = __riscv_vxor_vv_u64m1(lo_rem, folded_rem_hi, 2);
    }
    len -= num_main_iterations * numBytesPerMainIteration;
    
    // New 16-byte data and accumulator handling
    if (len >= 16) {
          vuint64m1_t data = __riscv_vle64_v_u64m1((const unsigned long int *) p, 2);
          data = __riscv_vxor_vv_u64m1(acc, data, 2);
          vuint64m1_t acc_hi = data;
          vuint64m1_t acc_lo = __riscv_vslidedown_vx_u64m1(data, 1, 1);
          vuint64m1_t remainder = crc32_le_clmul64_v2(acc_hi);
          remainder = __riscv_vxor_vv_u64m1(remainder, acc_lo, 1);
          acc = crc32_le_clmul64_v2(remainder);
          len -= 16;
          p += 16;
    }

    for (; len >= 8; len -=8, p += 8) {
          vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, 8);
          vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
          data = __riscv_vxor_vv_u64m1(acc, data, 1);
          acc = crc32_le_clmul64_v2(data);
    }
    // Because the pointer alignment is done initially (before the main loop),
    // we need to handle the non multiple of 8 size in the epilog.
    if (len != 0) {
          vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, len);
          vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
          size_t align_shift = 8 * (8 - len);
          data = __riscv_vsll_vx_u64m1(data, align_shift, 1);
          vuint64m1_t crc_hi = __riscv_vsll_vx_u64m1(acc, align_shift, 1); 
          vuint64m1_t crc_lo = __riscv_vsrl_vx_u64m1(acc, 64 - align_shift, 1); 
          // FIXME: this is brittle as we need elements > 0 to be zero in zero (undisturbed compared to init)
          data = __riscv_vxor_vv_u64m1(data, crc_hi, 1); 
          acc = crc32_le_clmul64_v2(data);
          acc = __riscv_vxor_vv_u64m1(crc_lo, acc, 1); 
    }

    crc = __riscv_vmv_x_s_u64m1_u64(acc);
    return (uint32_t) crc;
}
