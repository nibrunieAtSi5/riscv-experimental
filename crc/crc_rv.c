#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <riscv_vector.h>


// Set of crc32 and crc32c routines using two custom RISC-V vector instructions to perform
// little-endian CRC
// vcrc32.vs       31..26=0x28 vm vs2 19..15=0x14 14..12=0x2 vd 6..0=0x77
// vcrc32c.vs      31..26=0x28 vm vs2 19..15=0x15 14..12=0x2 vd 6..0=0x77
// vcrc32.vs vd, vs2, vs1 (encoded as rs1=0x14 for vcrc32, rs1=0x15 for vcrc32c)
// opcode=0x77, func3=0x2, func7=(0x28<<1 | 1)=0x51 (vm=1 unmasked)
// Using inline assembly with .insn r

uint32_t rv_crc32_le(uint32_t crc, const uint8_t *buffer, size_t len) {

    size_t avl = len;
    uint32_t tmp_crc = crc;
    for (; avl > 0; ) {
        size_t vl = 0;

        // Example wrapper: you'll likely need to load/store vector registers.
        // Assuming v0 and v1 are used for vd and vs2 here.
        asm volatile (
            "vsetivli x0, 1, e32, m1, ta, ma \n\t"
            "vmv.v.x v2, %[crc] \n\t"
            "vsetvli %[vl], %[avl], e8, m1, ta, ma \n\t"
            "vle8.v v1, (%[buffer]) \n\t"
            // vcrc32.vs v2, v1
            ".insn r 0x77, 0x2, 0x51, x2, x20, x1 \n\t"
            "vsetivli x0, 1, e32, m1, ta, ma \n\t"
            "vmv.x.s %[crc], v2 \n\t"
            : [vl] "=r" (vl), [crc] "+r" (tmp_crc)
            : [avl] "r" (avl), [buffer] "r" (buffer)
            : "v0", "v1", "v2", "memory", "cc"
        );
        // printf("  vl=%d\n", vl);
        // printf("  crc=%x\n", tmp_crc);
        avl -= vl;
        buffer += vl;

    }

    return tmp_crc;
}

uint32_t rv_crc32c_le(uint32_t crc, const uint8_t *buffer, size_t len) {
    asm volatile (
        "vsetivli x0, 1, e32, m1, ta, ma \n\t"
        // initializaing accumulator
        "vmv.v.x v2, %[crc] \n\t"
    "1:\n\t"
        // t0 is used to store vl
        "vsetvli t0, %[avl], e8, m1, ta, ma \n\t"
        "vle8.v v1, (%[buffer]) \n\t"
        // vcrc32c.vs v2, v1
        ".insn r 0x77, 0x2, 0x51, x2, x21, x1 \n\t"
        "sub %[avl], %[avl], t0 \n\t"
        "add %[buffer], %[buffer], t0 \n\t"
        "bnez %[avl], 1b \n\t"
        "vsetivli x0, 1, e32, m1, ta, ma \n\t"
        "vmv.x.s %[crc], v2 \n\t"
        : [crc] "+r" (crc)
        : [avl] "r" (len), [buffer] "r" (buffer)
        : "v0", "v1", "v2", "memory", "cc", "t0"
    );
    return crc;
}

uint32_t rv_crc32_le_opt(uint32_t crc, const uint8_t *buffer, size_t len) {

    asm volatile (
        "vsetivli x0, 1, e32, m1, ta, ma \n\t"
        // initializaing accumulator
        "vmv.v.x v2, %[crc] \n\t"
    "1:\n\t"
        // t0 is used to store vl
        "vsetvli t0, %[avl], e8, m8, ta, ma \n\t"
        "vle8.v v8, (%[buffer]) \n\t"
        // vcrc32.vs v2, v1
        ".insn r 0x77, 0x2, 0x51, x2, x20, x8 \n\t"
        "sub %[avl], %[avl], t0 \n\t"
        "add %[buffer], %[buffer], t0 \n\t"
        "bnez %[avl], 1b \n\t"
        "vsetivli x0, 1, e32, m1, ta, ma \n\t"
        "vmv.x.s %[crc], v2 \n\t"
        : [crc] "+r" (crc)
        : [avl] "r" (len), [buffer] "r" (buffer)
        : "v0", "v1", "v2", "memory", "cc", "t0"
    );
    return crc;
}

uint32_t crc32_le_generic(uint32_t crc, unsigned char const *p, size_t len);


/** crc32Eth32_be_vector() - Calculate bitwise little-endian Ethernet AUTODIN II
 *			CRC32/CRC32C
 * @crc: seed value for computation.  ~0 for Ethernet, sometimes 0 for other
 *	 uses, or the previous crc32/crc32c value if computing incrementally.
 * @p: pointer to buffer over which CRC32/CRC32C is run
 * @len: length of buffer @p
 *
 */
uint32_t rv_crc32_le_vector_clmul(uint32_t crc, unsigned char const *p, size_t len)
{
  uint32_t polynomial = 0x04C11DB7;
	int i;
  size_t avl = len / 4; // 4-byte per element 

  // we take a pair of 32-bit elements (E, F) from the message and reduce
  // E.X^32 by multiplying E.R where R=X^32[CRCPoly]
  // F.X^64 by multiplying F.S where S=X^64[CRCPoly]

  // {(X^96 mod P), (X^64 mod P)} = {0xe8a45605, 0xf200aa66};
  // {(X^64 mod P), (X^32 mod P)}
  // const uint32_t redConstants[] = {0x177b1443, 0x3d6029b0};
  const uint32_t redConstants[] = {
    /* CRC32(X^64) */ 0xf200aa66,
    /* CRC32(X^32) */ 0x490d678d,
  };
  vuint32mf2_t redConstantVector = __riscv_vle32_v_u32mf2(redConstants, 2);
  vuint64m1_t extRedCstVector = __riscv_vzext_vf2_u64m1(redConstantVector, 2);

  vuint64m1_t crcAcc = __riscv_vmv_v_x_u64m1(0, 2);
  const vuint64m1_t zeroVecU64M1 = __riscv_vmv_v_x_u64m1(0, 2);

  // hoisting vl value setting outside of loop (require test to check
  // that avl is always larger than 2*vl: one time for loop body and one
  // time for the epilog which is not vectorized).
  // size_t vl = __riscv_vsetvl_e32mf2(-1);

  for (size_t vl = __riscv_vsetvl_e32mf2(-1); avl >= 2 * vl; avl -= vl, p += 4 * vl, len -= 4*vl) {
      // compute loop body vector length from application vector length avl
      // printf("vector loop body, vl=%lu, avl=%lu, len=%lu\n", vl, avl, len);
      vuint32mf2_t inputData = __riscv_vle32_v_u32mf2((uint32_t*) p, vl);
      vuint32m1_t inputDataM1 = __riscv_vlmul_ext_v_u32mf2_u32m1(inputData);
      // printf("pre brev inputData=%"PRIx64"\n", __riscv_vmv_x_s_u64m1_u64(__riscv_vreinterpret_v_u32m1_u64m1(inputDataM1)));
      // bit swapping each 32-bit element
#if HAS_ZVBB_SUPPORT
      crcAcc = __riscv_vbrev_v_u64m1(crcAcc, 1); 
#else // no HAS_ZVBB_SUPPORT
      crcAcc = __riscv_vbrev8_v_u64m1(crcAcc, 1); 
      crcAcc = __riscv_vrev8_v_u64m1(crcAcc, 1); 
#endif
      //printf("post brev crcAccU64=%"PRIx64"\n", __riscv_vmv_x_s_u64m1_u64(crcAcc));
      vuint32m1_t crcAccU32 = __riscv_vreinterpret_v_u64m1_u32m1 (crcAcc);
      //printf("post brev crcAccU32=%"PRIx32"\n", __riscv_vmv_x_s_u32m1_u32(crcAccU32));
      //printf("post brev crcAccU32=%"PRIx32"\n", __riscv_vmv_x_s_u32m1_u32(__riscv_vslidedown_vx_u32m1(crcAccU32, 1, 2)));
      vuint32mf2_t crcAccU32mf2 = __riscv_vlmul_trunc_v_u32m1_u32mf2(crcAccU32);
      inputData = __riscv_vxor_vv_u32mf2(inputData, crcAccU32mf2, vl);
#if HAS_ZVBB_SUPPORT
      inputData = __riscv_vbrev_v_u32mf2(inputData, vl);
#else // no HAS_ZVBB_SUPPORT
      inputData = __riscv_vbrev8_v_u32mf2(inputData, vl);
      inputData = __riscv_vrev8_v_u32mf2(inputData, vl);
#endif
      // inputDataM1 = __riscv_vlmul_ext_v_u32mf2_u32m1(inputData);
      // printf("post xor+brev inputData=%"PRIx64"\n", __riscv_vmv_x_s_u64m1_u64(__riscv_vreinterpret_v_u32m1_u64m1(inputDataM1)));

      // expanding data u32 -> u64
#ifdef HAS_ZVBB_SUPPORT
      vuint64m1_t extInputData = __riscv_vwsll_vx_u64m1(inputData, 0, vl);
#else // No HAS_ZVBB_SUPPORT
      vuint64m1_t extInputData = __riscv_vzext_vf2_u64m1(inputData, vl);
#endif // HAS_ZVBB_SUPPORT
      //
      vuint64m1_t multRes = __riscv_vclmul_vv_u64m1(extInputData, extRedCstVector, vl);
      crcAcc = __riscv_vredxor_vs_u64m1_u64m1(multRes, zeroVecU64M1, vl);
      // printf("crcAccU64=%"PRIx64"\n", __riscv_vmv_x_s_u64m1_u64(crcAcc));
  }
  uint64_t crcAccBuffer[1] = {0};
  uint32_t preCrc = __riscv_vmv_x_s_u64m1_u64(crcAcc);
  // bit order reversing to ensure MS-byte is stored first (lowest address)
#if HAS_ZVBB_SUPPORT
      crcAcc = __riscv_vbrev_v_u64m1(crcAcc, 1); 
#else // no HAS_ZVBB_SUPPORT
      crcAcc = __riscv_vbrev8_v_u64m1(crcAcc, 1); 
      crcAcc = __riscv_vrev8_v_u64m1(crcAcc, 1); 
#endif
  __riscv_vse64_v_u64m1(crcAccBuffer, crcAcc, 1);
  uint8_t* crcAccBufferU8 = (uint8_t*) crcAccBuffer;

  // const uint32_t ethCRC32Poly = 0x04C11DB7;
  // const uint32_t ethCRC32PolyInv = 0xedb88320;
  printf("len=%lu, crcAccBufferU8=%x, p=%x\n", len, crc32_le_generic(0, crcAccBufferU8, 8), crc32_le_generic(0, p, len));
  uint32_t pre_crc32 = crc32_le_generic(0, crcAccBufferU8, 8);
  uint8_t tail_buffer[8] = {0};
  if (len < 8) {
    return crc32_le_generic(0, p, len);
  } else {
    return  crc32_le_generic(pre_crc32, tail_buffer, len - 8) ^ crc32_le_generic(0, p, len);
  }
}

uint32_t rv_crc32_le_vector_clmul_opt(uint32_t crc, unsigned char const *p, size_t len)
{
  uint32_t polynomial = 0x04C11DB7;
	int i;
  size_t avl = len / 4; // 4-byte per element 

  // we take a pair of 32-bit elements (E, F) from the message and reduce
  // E.X^32 by multiplying E.R where R=X^32[CRCPoly]
  // F.X^64 by multiplying F.S where S=X^64[CRCPoly]

  // {(X^96 mod P), (X^64 mod P)} = {0xe8a45605, 0xf200aa66};
  // {(X^64 mod P), (X^32 mod P)}
  // const uint32_t redConstants[] = {0x177b1443, 0x3d6029b0};
  const uint32_t redConstants[] = {
    /* CRC32(X^64) */ 0xf200aa66,
    /* CRC32(X^32) */ 0x490d678d,
  };
  vuint32mf2_t redConstantVector = __riscv_vle32_v_u32mf2(redConstants, 2);
  vuint64m1_t extRedCstVector = __riscv_vzext_vf2_u64m1(redConstantVector, 2);

  vuint64m1_t crcAcc = __riscv_vmv_v_x_u64m1(0, 2);
  const vuint64m1_t zeroVecU64M1 = __riscv_vmv_v_x_u64m1(0, 2);

  // hoisting vl value setting outside of loop (require test to check
  // that avl is always larger than 2*vl: one time for loop body and one
  // time for the epilog which is not vectorized).
  // size_t vl = __riscv_vsetvl_e32mf2(-1);

  for (size_t vl = __riscv_vsetvl_e32mf2(-1); avl >= 2 * vl; avl -= vl, p += 4 * vl, len -= 4*vl) {
      // compute loop body vector length from application vector length avl
      // printf("vector loop body, vl=%lu, avl=%lu, len=%lu\n", vl, avl, len);
      vuint32mf2_t inputData = __riscv_vle32_v_u32mf2((uint32_t*) p, vl);
      vuint32m1_t inputDataM1 = __riscv_vlmul_ext_v_u32mf2_u32m1(inputData);
      // printf("pre brev inputData=%"PRIx64"\n", __riscv_vmv_x_s_u64m1_u64(__riscv_vreinterpret_v_u32m1_u64m1(inputDataM1)));
      // bit swapping each 32-bit element
#if HAS_ZVBB_SUPPORT
      crcAcc = __riscv_vbrev_v_u64m1(crcAcc, 1); 
#else // no HAS_ZVBB_SUPPORT
      crcAcc = __riscv_vbrev8_v_u64m1(crcAcc, 1); 
      crcAcc = __riscv_vrev8_v_u64m1(crcAcc, 1); 
#endif
      //printf("post brev crcAccU64=%"PRIx64"\n", __riscv_vmv_x_s_u64m1_u64(crcAcc));
      vuint32m1_t crcAccU32 = __riscv_vreinterpret_v_u64m1_u32m1 (crcAcc);
      //printf("post brev crcAccU32=%"PRIx32"\n", __riscv_vmv_x_s_u32m1_u32(crcAccU32));
      //printf("post brev crcAccU32=%"PRIx32"\n", __riscv_vmv_x_s_u32m1_u32(__riscv_vslidedown_vx_u32m1(crcAccU32, 1, 2)));
      vuint32mf2_t crcAccU32mf2 = __riscv_vlmul_trunc_v_u32m1_u32mf2(crcAccU32);
      inputData = __riscv_vxor_vv_u32mf2(inputData, crcAccU32mf2, vl);
#if HAS_ZVBB_SUPPORT
      inputData = __riscv_vbrev_v_u32mf2(inputData, vl);
#else // no HAS_ZVBB_SUPPORT
      inputData = __riscv_vbrev8_v_u32mf2(inputData, vl);
      inputData = __riscv_vrev8_v_u32mf2(inputData, vl);
#endif
      // inputDataM1 = __riscv_vlmul_ext_v_u32mf2_u32m1(inputData);
      // printf("post xor+brev inputData=%"PRIx64"\n", __riscv_vmv_x_s_u64m1_u64(__riscv_vreinterpret_v_u32m1_u64m1(inputDataM1)));

      // expanding data u32 -> u64
#ifdef HAS_ZVBB_SUPPORT
      vuint64m1_t extInputData = __riscv_vwsll_vx_u64m1(inputData, 0, vl);
#else // No HAS_ZVBB_SUPPORT
      vuint64m1_t extInputData = __riscv_vzext_vf2_u64m1(inputData, vl);
#endif // HAS_ZVBB_SUPPORT
      //
      vuint64m1_t multRes = __riscv_vclmul_vv_u64m1(extInputData, extRedCstVector, vl);
      crcAcc = __riscv_vredxor_vs_u64m1_u64m1(multRes, zeroVecU64M1, vl);
      // printf("crcAccU64=%"PRIx64"\n", __riscv_vmv_x_s_u64m1_u64(crcAcc));
  }
  uint64_t crcAccBuffer[1] = {0};
  uint32_t preCrc = __riscv_vmv_x_s_u64m1_u64(crcAcc);
  // bit order reversing to ensure MS-byte is stored first (lowest address)
#if HAS_ZVBB_SUPPORT
      crcAcc = __riscv_vbrev_v_u64m1(crcAcc, 1); 
#else // no HAS_ZVBB_SUPPORT
      crcAcc = __riscv_vbrev8_v_u64m1(crcAcc, 1); 
      crcAcc = __riscv_vrev8_v_u64m1(crcAcc, 1); 
#endif
  __riscv_vse64_v_u64m1(crcAccBuffer, crcAcc, 1);
  uint8_t* crcAccBufferU8 = (uint8_t*) crcAccBuffer;

  // const uint32_t ethCRC32Poly = 0x04C11DB7;
  const uint32_t ethCRC32PolyInv = 0xedb88320;
  return crc32_le_generic(0, crcAccBufferU8, 8) ^ crc32_le_generic(0, p, len);
}

static inline vuint64m1_t crc32_le_clmul64_v2(vuint64m1_t data) {
            // REV_CRC32_BE_INV_EXT=0xb4e5b025f7011641
            uint64_t REV_CRC32_BE_INV_EXT = 0xb4e5b025f7011641ull;
            vuint64m1_t rev_q = __riscv_vclmul_vx_u64m1(data, REV_CRC32_BE_INV_EXT, 1);
            // bit_reverse(FULL_CRC32_POLY_BE << 31, 64)) = 0x1db710641
            uint64_t rem_cst = 0x1db710641ull; 
            vuint64m1_t remainder = __riscv_vclmulh_vx_u64m1(rev_q, rem_cst, 1);
            return remainder;
}


uint32_t rv_crc32_le_vector_clmul_fold(uint32_t crc, unsigned char const *p, size_t len) {
      if (len <= 8) {
            vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, len);
            vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
            size_t align_shift = 8 * (8 - len);
            data = __riscv_vsll_vx_u64m1(data, align_shift, 1);
            vuint64m1_t remainder = crc32_le_clmul64_v2(data);
            uint64_t crc = __riscv_vmv_x_s_u64m1_u64(remainder);
            return (uint32_t) crc;
      } else if (len <= 32) {
            vuint64m1_t remainder = __riscv_vmv_v_x_u64m1(0, 1);
            if (len % 8 != 0) {
                  vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, len);
                  vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
                  size_t align_shift = 8 * (8 - len);
                  data = __riscv_vsll_vx_u64m1(data, align_shift, 1);
                  remainder = crc32_le_clmul64_v2(data);
                  p += (len % 8);
            }
            for (; len >= 8; len -=8, p += 8) {
                  vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, 8);
                  vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
                  data = __riscv_vxor_vv_u64m1(remainder, data, 1);
                  remainder = crc32_le_clmul64_v2(data);
            }
            uint64_t crc = __riscv_vmv_x_s_u64m1_u64(remainder);
            return (uint32_t) crc;
      } else {
            // TODO
            // RmRev=0x9ba54c6f00000000 R64Rev=0xb8bc676500000000
            const uint64_t rmRev = 0x9ba54c6f00000000ull;
            const uint64_t r64Rev = 0xb8bc676500000000ull; 
            vuint64m1_t acc = __riscv_vmv_v_x_u64m1(0, 2);
            if ((len % 8) != 0) {
                  vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, len);
                  vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
                  size_t align_shift = 8 * (8 - len);
                  data = __riscv_vsll_vx_u64m1(data, align_shift, 1);
                  // FIXME: this is brittle as we need elements > 0 to be zero in zero (undisturbed compared to init)
                  acc = crc32_le_clmul64_v2(data);
                  p += (len % 8);
            }

            for (; len >= 32; len -= 16, p += 16) {
                  vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, 16);
                  vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
                  data = __riscv_vxor_vv_u64m1(acc, data, 2);
                  vuint64m1_t folded_rem_hi = __riscv_vclmulh_vx_u64m1(data, rmRev, 2);
                  vuint64m1_t folded_rem_lo = __riscv_vclmul_vx_u64m1(data, rmRev, 2);

                  vuint64m1_t lo_rem = __riscv_vclmulh_vx_u64m1(folded_rem_lo, r64Rev, 2);
                  acc = __riscv_vxor_vv_u64m1(lo_rem, folded_rem_hi, 2);
            }
            {
                  vuint8m1_t byte_data = __riscv_vle8_v_u8m1(p, 16);
                  vuint64m1_t data = __riscv_vreinterpret_v_u8m1_u64m1(byte_data);
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

            uint64_t crc = __riscv_vmv_x_s_u64m1_u64(acc);
            return (uint32_t) crc;
            
      }
}

