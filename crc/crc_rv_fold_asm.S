	.file	"crc_rv_fold.S"
	.option nopic
	.attribute arch, "rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_v1p0_zicsr2p0_zifencei2p0_zmmul1p0_zaamo1p0_zalrsc1p0_zca1p0_zcd1p0_zvbb1p0_zvbc1p0_zve32f1p0_zve32x1p0_zve64d1p0_zve64f1p0_zve64x1p0_zvkb1p0_zvl128b1p0_zvl32b1p0_zvl64b1p0"
	.attribute unaligned_access, 0
	.attribute stack_align, 16
	.text
	.align	1

#ifndef LMUL
#define LMUL 2
#endif

#if LMUL == 1
#define V_LMUL m1
#define NUM_E64 2
#define NUM_E64_M_1 1
#define NUM_BYTES 16
#define LOG2_NUM_BYTES 4
#define DOUBLE_BYTES 32
#define LOG2_DOUBLE_BYTES 5
#define R2M_REV r256Rev
#define RM_REV r128Rev
#elif LMUL == 2
#define V_LMUL m2
#define NUM_E64 4
#define NUM_E64_M_1 3
#define NUM_BYTES 32
#define LOG2_NUM_BYTES 5
#define DOUBLE_BYTES 64
#define LOG2_DOUBLE_BYTES 6
#define R2M_REV r512Rev
#define RM_REV r256Rev
#elif LMUL == 4
#define V_LMUL m4
#define NUM_E64 8
#define NUM_E64_M_1 7
#define NUM_BYTES 64
#define LOG2_NUM_BYTES 6
#define DOUBLE_BYTES 128
#define LOG2_DOUBLE_BYTES 7
#define R2M_REV r1024Rev
#define RM_REV r512Rev
#elif LMUL == 8
#define V_LMUL m8
#define NUM_E64 16
#define NUM_E64_M_1 15
#define NUM_BYTES 128
#define LOG2_NUM_BYTES 7
#define DOUBLE_BYTES 256
#define LOG2_DOUBLE_BYTES 8
#define R2M_REV r2048Rev
#define RM_REV r1024Rev
#else
#error "Unsupported LMUL"
#endif

#define _PASTE(a, b) a ## b
#define PASTE(a, b) _PASTE(a, b)
#define LDRVA(reg, sym) ld reg, PASTE(rva_, sym)


	.globl	rv_crc32_le_vector_clmul_fold_asm
	.type	rv_crc32_le_vector_clmul_fold_asm, @function
rv_crc32_le_vector_clmul_fold_asm:
	.size	rv_crc32_le_vector_clmul_fold_asm, .-rv_crc32_le_vector_clmul_fold_asm
	// a0: initial crc value
	// a1: input buffer address
	// a2: input buffer size (in bytes)
	//
	// Invariants:
	// TBD
	li	a5,31
	bleu	a2,a5,.Linit_acc
	andi	a4,a1,7
	bne	a4,zero,.Lprolog_align
.Linit_acc:
	vsetivli	zero,NUM_E64,e64,V_LMUL,ta,ma
	vmv.v.i	v8,0
.Lloop_bounds:
	srli	a4,a2,LOG2_NUM_BYTES
	addiw	a4,a4,-1
	sext.w	a5,a4
	bge	a4,zero,.Lbound_calc
	li	a5,0
.Lbound_calc:
	sraiw	t4,a5,1
	slli	t4,t4,LOG2_DOUBLE_BYTES
	slli	t3,a5,LOG2_NUM_BYTES
	add	t1,a1,t4
	add	a0,a1,t3
	bgeu	a1,t1,.Lcheck_single
	// r512Rev = 0xcaaf6e8f00000000
	LDRVA(a3, R2M_REV)
	// r256Rev = 0x01b5fd1d00000000
	LDRVA(a4, RM_REV)
	// r64Rev = 0xb8bc676500000000
	LDRVA(a7, r64Rev)
	mv	a5,a1
.Lloop_double:
	vle64.v	v16,0(a5)
	addi	a6,a5,NUM_BYTES
	vle64.v	v24,0(a6)
	addi	a5,a5,DOUBLE_BYTES
	vxor.vv	v8,v8,v16
	vclmul.vx	v0,v24,a4
	vclmul.vx	v16,v8,a3
	vclmulh.vx	v24,v24,a4
	vclmulh.vx	v8,v8,a3
	vxor.vv	v16,v16,v0
	vxor.vv	v8,v8,v24
	vclmulh.vx	v16,v16,a7
	vxor.vv	v8,v16,v8
	bgtu	t1,a5,.Lloop_double
	add	a1,a1,t4
.Lcheck_single:
	bleu	a0,a1,.Lreduce_acc
	// r256Rev = 0x01b5fd1d00000000
	LDRVA(a4, RM_REV)
	// r64Rev = 0xb8bc676500000000
	LDRVA(a3, r64Rev)
	mv	a5,a1
.Lloop_single:
	vle64.v	v16,0(a5)
	addi	a5,a5,NUM_BYTES
	vxor.vv	v8,v8,v16
	vclmul.vx	v16,v8,a4
	vclmulh.vx	v8,v8,a4
	vclmulh.vx	v16,v16,a3
	vxor.vv	v8,v16,v8
	bgtu	a0,a5,.Lloop_single
	addi	a0,a0,-1
	sub	a0,a0,a1
	andi	a0,a0,-NUM_BYTES
	addi	a1,a1,NUM_BYTES
	add	a1,a0,a1
.Lreduce_acc:
	vsetivli	zero,1,e64,m1,ta,ma
	sub	a2,a2,t3
	li	a5,31
	vmv1r.v	v1,v8
	bleu	a2,a5,.Lsetup_8_byte
	vsetivli	zero,NUM_E64,e64,V_LMUL,ta,ma
	vle64.v	v24,0(a1)
	la	a5, rva_allLastFoldingCsts + ((17 - NUM_E64) * 8)
	vle64.v	v16,0(a5)
	// r64Rev = 0xb8bc676500000000
	LDRVA(a4, r64Rev)
	vxor.vv	v8,v8,v24
	vsetivli	zero,1,e64,m1,ta,ma
	vmv.v.i	v1,0
	vsetivli	zero,NUM_E64,e64,V_LMUL,ta,ma
	vclmul.vv	v24,v8,v16
	vclmulh.vv	v16,v8,v16
	// REV_CRC32_BE_INV_EXT = 0xb4e5b025f7011641
	LDRVA(a5, REV_CRC32_BE_INV_EXT)
	vclmulh.vx	v24,v24,a4
	vsetivli	zero,NUM_E64_M_1,e64,V_LMUL,tu,ma
	vxor.vv	v8,v24,v16
	vsetivli	zero,NUM_E64,e64,V_LMUL,ta,ma
	vredxor.vs	v4,v8,v1
	vsetivli	zero,1,e64,m1,ta,ma
	// rem_cst = 0x1db710641
	LDRVA(a4, rem_cst)
	vclmul.vx	v8,v4,a5
	addi	a2,a2,-NUM_BYTES
	addi	a1,a1,NUM_BYTES
	vclmulh.vx	v1,v8,a4
.Lsetup_8_byte:
	li	a5,7
	bleu	a2,a5,.Lepilog
	// REV_CRC32_BE_INV_EXT = 0xb4e5b025f7011641
	LDRVA(a4, REV_CRC32_BE_INV_EXT)
	srli	a5,a2,3
	// rem_cst = 0x1db710641
	LDRVA(a3, rem_cst)
	slli	a0,a5,3
	addi	a6,a5,-1
	add	a0,a1,a0
	mv	a5,a1
.Lloop_8_byte:
	vle64.v	v8,0(a5)
	addi	a5,a5,8
	vxor.vv	v1,v1,v8
	vclmul.vx	v1,v1,a4
	vclmulh.vx	v1,v1,a3
	bne	a5,a0,.Lloop_8_byte
	addi	a1,a1,8
	slli	a5,a6,3
	add	a1,a1,a5
	andi	a2,a2,7
.Lepilog:
	beq	a2,zero,.Lreturn
	vsetvli	zero,a2,e64,m8,ta,ma
	vle8.v	v8,0(a1)
	li	a4,8
	sub	a4,a4,a2
	// REV_CRC32_BE_INV_EXT = 0xb4e5b025f7011641
	LDRVA(a5, REV_CRC32_BE_INV_EXT)
	vsetivli	zero,1,e64,m1,ta,ma
	slli	a4,a4,3
	vsll.vx	v3,v1,a4
	vsll.vx	v8,v8,a4
	vxor.vv	v8,v8,v3
	vclmul.vx	v8,v8,a5
	// rem_cst = 0x1db710641
	LDRVA(a5, rem_cst)
	li	a3,64
	vclmulh.vx	v8,v8,a5
	sub	a5,a3,a4
	vsrl.vx	v1,v1,a5
	vxor.vv	v1,v1,v8
	vmv.x.s	a0,v1
	sext.w	a0,a0
	ret
.Lreturn:
	vsetvli	zero,a2,e64,m8,ta,ma
	vmv.x.s	a0,v1
	sext.w	a0,a0
	ret
.Lprolog_align:
	vsetvli	zero,a4,e8,V_LMUL,ta,ma
	vle8.v	v8,0(a1)
	// REV_CRC32_BE_INV_EXT = 0xb4e5b025f7011641
	LDRVA(a5, REV_CRC32_BE_INV_EXT)
	li	a3,8
	sub	a3,a3,a4
	vsetivli	zero,1,e64,V_LMUL,ta,ma
	slli	a3,a3,3
	vsll.vx	v8,v8,a3
	vclmul.vx	v8,v8,a5
	// rem_cst = 0x1db710641
	LDRVA(a5, rem_cst)
	vclmulh.vx	v8,v8,a5
	sub	a2,a2,a4
	add	a1,a1,a4
	vsetivli	zero,NUM_E64,e64,V_LMUL,ta,ma
	j	.Lloop_bounds
	.size	rv_crc32_le_vector_clmul_fold_asm, .-rv_crc32_le_vector_clmul_fold_asm
	.globl	rva_allLastFoldingCsts
	.globl	rva_r2048Rev
	.globl	rva_r1024Rev
	.globl	rva_r960Rev
	.globl	rva_r896Rev
	.globl	rva_r832Rev
	.globl	rva_r768Rev
	.globl	rva_r704Rev
	.globl	rva_r640Rev
	.globl	rva_r576Rev
	.globl	rva_r512Rev
	.globl	rva_r448Rev
	.globl	rva_r384Rev
	.globl	rva_r320Rev
	.globl	rva_r256Rev
	.globl	rva_r192Rev
	.globl	rva_r128Rev
	.globl	rva_r64Rev
	.globl	rva_rem_cst
	.globl	rva_REV_CRC32_BE_INV_EXT
	.section	.rodata
	.align	3
	.set	.LANCHOR0,. + 0
	.type	rva_allLastFoldingCsts, @object
	.size	rva_allLastFoldingCsts, 136
rva_allLastFoldingCsts:
	.dword	0x7406fa9500000000
	.dword	0x19866e800000000
	.dword	0xc64ac0b800000000
	.dword	0x759fc69d00000000
	.dword	0x101a233100000000
	.dword	0x5a03a0cf00000000
	.dword	0x8e42b13e00000000
	.dword	0x653d982200000000
	.dword	0xcad38e8f00000000
	.dword	0x69ccfc0d00000000
	.dword	0x2a28386200000000
	.dword	0x9570d49500000000
	.dword	0x1b5fd1d00000000
	.dword	0x65673b4600000000
	.dword	0x9ba54c6f00000000
	.dword	0xb8bc676500000000
	.dword	0x0
	.section	.srodata,"a"
	.align	3
	.type	rva_r2048Rev, @object
	.size	rva_r2048Rev, 8
rva_r2048Rev:
	.dword	0x3f9f86300000000
	.type	rva_r1024Rev, @object
	.size	rva_r1024Rev, 8
rva_r1024Rev:
	.dword	0x7406fa9500000000
	.type	rva_r960Rev, @object
	.size	rva_r960Rev, 8
rva_r960Rev:
	.dword	0x19866e800000000
	.type	rva_r896Rev, @object
	.size	rva_r896Rev, 8
rva_r896Rev:
	.dword	0xc64ac0b800000000
	.type	rva_r832Rev, @object
	.size	rva_r832Rev, 8
rva_r832Rev:
	.dword	0x759fc69d00000000
	.type	rva_r768Rev, @object
	.size	rva_r768Rev, 8
rva_r768Rev:
	.dword	0x101a233100000000
	.type	rva_r704Rev, @object
	.size	rva_r704Rev, 8
rva_r704Rev:
	.dword	0x5a03a0cf00000000
	.type	rva_r640Rev, @object
	.size	rva_r640Rev, 8
rva_r640Rev:
	.dword	0x8e42b13e00000000
	.type	rva_r576Rev, @object
	.size	rva_r576Rev, 8
rva_r576Rev:
	.dword	0x653d982200000000
	.type	rva_r512Rev, @object
	.size	rva_r512Rev, 8
rva_r512Rev:
	.dword	0xcad38e8f00000000
	.type	rva_r448Rev, @object
	.size	rva_r448Rev, 8
rva_r448Rev:
	.dword	0x69ccfc0d00000000
	.type	rva_r384Rev, @object
	.size	rva_r384Rev, 8
rva_r384Rev:
	.dword	0x2a28386200000000
	.type	rva_r320Rev, @object
	.size	rva_r320Rev, 8
rva_r320Rev:
	.dword	0x9570d49500000000
	.type	rva_r256Rev, @object
	.size	rva_r256Rev, 8
rva_r256Rev:
	.dword	0x1b5fd1d00000000
	.type	rva_r192Rev, @object
	.size	rva_r192Rev, 8
rva_r192Rev:
	.dword	0x65673b4600000000
	.type	rva_r128Rev, @object
	.size	rva_r128Rev, 8
rva_r128Rev:
	.dword	0x9ba54c6f00000000
	.type	rva_r64Rev, @object
	.size	rva_r64Rev, 8
rva_r64Rev:
	.dword	0xb8bc676500000000
	.type	rva_rem_cst, @object
	.size	rva_rem_cst, 8
rva_rem_cst:
	.dword	0x1db710641
	.type	rva_REV_CRC32_BE_INV_EXT, @object
	.size	rva_REV_CRC32_BE_INV_EXT, 8
rva_REV_CRC32_BE_INV_EXT:
	.dword	0xb4e5b025f7011641
	.ident	"GCC: () 16.1.0"
	.section	.note.GNU-stack,"",@progbits
