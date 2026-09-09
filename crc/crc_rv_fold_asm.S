	.file	"crc_rv_fold.S"
	.option nopic
	.attribute arch, "rv64i2p1_m2p0_a2p1_f2p2_d2p2_c2p0_v1p0_zicsr2p0_zifencei2p0_zmmul1p0_zaamo1p0_zalrsc1p0_zca1p0_zcd1p0_zvbb1p0_zvbc1p0_zve32f1p0_zve32x1p0_zve64d1p0_zve64f1p0_zve64x1p0_zvkb1p0_zvl128b1p0_zvl32b1p0_zvl64b1p0"
	.attribute unaligned_access, 0
	.attribute stack_align, 16
	.text
	.align	1
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
	vsetivli	zero,4,e64,m2,ta,ma
	vmv.v.i	v2,0
.Lloop_bounds:
	srli	a4,a2,5
	addiw	a4,a4,-1
	sext.w	a5,a4
	bge	a4,zero,.Lbound_calc
	li	a5,0
.Lbound_calc:
	sraiw	t4,a5,1
	slli	t4,t4,6
	slli	t3,a5,5
	add	t1,a1,t4
	add	a0,a1,t3
	bgeu	a1,t1,.Lcheck_single
	li	a3,-892104704
	li	a4,28704768
	li	a7,-1195614208
	addi	a3,a3,-369
	addi	a4,a4,-739
	addi	a7,a7,1893
	slli	a3,a3,32
	slli	a4,a4,32
	slli	a7,a7,32
	mv	a5,a1
.Lloop_double:
	vle64.v	v4,0(a5)
	addi	a6,a5,32
	vle64.v	v6,0(a6)
	addi	a5,a5,64
	vxor.vv	v2,v2,v4
	vclmul.vx	v8,v6,a4
	vclmul.vx	v4,v2,a3
	vclmulh.vx	v6,v6,a4
	vclmulh.vx	v2,v2,a3
	vxor.vv	v4,v4,v8
	vxor.vv	v2,v2,v6
	vclmulh.vx	v4,v4,a7
	vxor.vv	v2,v4,v2
	bgtu	t1,a5,.Lloop_double
	add	a1,a1,t4
.Lcheck_single:
	bleu	a0,a1,.Lreduce_acc
	li	a4,28704768
	li	a3,-1195614208
	addi	a4,a4,-739
	addi	a3,a3,1893
	slli	a4,a4,32
	slli	a3,a3,32
	mv	a5,a1
.Lloop_single:
	vle64.v	v4,0(a5)
	addi	a5,a5,32
	vxor.vv	v2,v2,v4
	vclmul.vx	v4,v2,a4
	vclmulh.vx	v2,v2,a4
	vclmulh.vx	v4,v4,a3
	vxor.vv	v2,v4,v2
	bgtu	a0,a5,.Lloop_single
	addi	a0,a0,-1
	sub	a0,a0,a1
	andi	a0,a0,-32
	addi	a1,a1,32
	add	a1,a0,a1
.Lreduce_acc:
	vsetivli	zero,1,e64,m1,ta,ma
	sub	a2,a2,t3
	li	a5,31
	vmv1r.v	v1,v2
	bleu	a2,a5,.Lsetup_8_byte
	vsetivli	zero,4,e64,m2,ta,ma
	vle64.v	v6,0(a1)
	lui	a5,%hi(.LANCHOR0+104)
	addi	a5,a5,%lo(.LANCHOR0+104)
	vle64.v	v4,0(a5)
	li	a4,-1195614208
	addi	a4,a4,1893
	vxor.vv	v2,v2,v6
	vsetivli	zero,1,e64,m1,ta,ma
	vmv.v.i	v1,0
	vsetivli	zero,4,e64,m2,ta,ma
	vclmul.vv	v6,v2,v4
	slli	a4,a4,32
	vclmulh.vv	v4,v2,v4
	li	a5,-39374848
	vclmulh.vx	v6,v6,a4
	vsetivli	zero,3,e64,m2,tu,ma
	addi	a5,a5,-639
	slli	a5,a5,13
	vxor.vv	v2,v6,v4
	vsetivli	zero,4,e64,m2,ta,ma
	addi	a5,a5,1527
	vredxor.vs	v2,v2,v1
	slli	a5,a5,12
	addi	a5,a5,17
	slli	a5,a5,12
	vsetivli	zero,1,e64,m1,ta,ma
	addi	a5,a5,1601
	li	a4,498536448
	slli	a4,a4,4
	vclmul.vx	v2,v2,a5
	addi	a4,a4,1601
	addi	a2,a2,-32
	addi	a1,a1,32
	vclmulh.vx	v1,v2,a4
.Lsetup_8_byte:
	li	a5,7
	bleu	a2,a5,.Lepilog
	li	a4,-39374848
	addi	a4,a4,-639
	slli	a4,a4,13
	addi	a4,a4,1527
	slli	a4,a4,12
	srli	a5,a2,3
	addi	a4,a4,17
	li	a3,498536448
	slli	a0,a5,3
	slli	a4,a4,12
	slli	a3,a3,4
	addi	a6,a5,-1
	addi	a4,a4,1601
	add	a0,a1,a0
	addi	a3,a3,1601
	mv	a5,a1
.Lloop_8_byte:
	vle64.v	v2,0(a5)
	addi	a5,a5,8
	vxor.vv	v1,v1,v2
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
	vle8.v	v2,0(a1)
	li	a4,8
	sub	a4,a4,a2
	li	a5,-39374848
	vsetivli	zero,1,e64,m1,ta,ma
	slli	a4,a4,3
	addi	a5,a5,-639
	vsll.vx	v3,v1,a4
	vsll.vx	v2,v2,a4
	slli	a5,a5,13
	addi	a5,a5,1527
	slli	a5,a5,12
	vxor.vv	v2,v2,v3
	addi	a5,a5,17
	slli	a5,a5,12
	addi	a5,a5,1601
	vclmul.vx	v2,v2,a5
	li	a5,498536448
	slli	a5,a5,4
	addi	a5,a5,1601
	li	a3,64
	vclmulh.vx	v2,v2,a5
	sub	a5,a3,a4
	vsrl.vx	v1,v1,a5
	vxor.vv	v1,v1,v2
	vmv.x.s	a0,v1
	sext.w	a0,a0
	ret
.Lreturn:
	vsetvli	zero,a2,e64,m8,ta,ma
	vmv.x.s	a0,v1
	sext.w	a0,a0
	ret
.Lprolog_align:
	vsetvli	zero,a4,e8,m2,ta,ma
	vle8.v	v2,0(a1)
	li	a5,-39374848
	addi	a5,a5,-639
	li	a3,8
	slli	a5,a5,13
	sub	a3,a3,a4
	addi	a5,a5,1527
	vsetivli	zero,1,e64,m2,ta,ma
	slli	a3,a3,3
	slli	a5,a5,12
	vsll.vx	v2,v2,a3
	addi	a5,a5,17
	slli	a5,a5,12
	addi	a5,a5,1601
	vclmul.vx	v2,v2,a5
	li	a5,498536448
	slli	a5,a5,4
	addi	a5,a5,1601
	vclmulh.vx	v2,v2,a5
	sub	a2,a2,a4
	add	a1,a1,a4
	vsetivli	zero,4,e64,m2,ta,ma
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
	.dword	8360645276116975616
	.dword	114954937116393472
	.dword	-4158299409416716288
	.dword	8475711401346727936
	.dword	1160278547361693696
	.dword	6486204699262320640
	.dword	-8195793492014792704
	.dword	7295154243206381568
	.dword	-3831562113130692608
	.dword	7623745401996181504
	.dword	3037739942219350016
	.dword	-7678403628251480064
	.dword	123282865818435584
	.dword	7306874092265668608
	.dword	-7231289587048382464
	.dword	-5135115791619850240
	.dword	0
	.section	.srodata,"a"
	.align	3
	.type	rva_r2048Rev, @object
	.size	rva_r2048Rev, 8
rva_r2048Rev:
	.dword	286533155400187904
	.type	rva_r1024Rev, @object
	.size	rva_r1024Rev, 8
rva_r1024Rev:
	.dword	8360645276116975616
	.type	rva_r960Rev, @object
	.size	rva_r960Rev, 8
rva_r960Rev:
	.dword	114954937116393472
	.type	rva_r896Rev, @object
	.size	rva_r896Rev, 8
rva_r896Rev:
	.dword	-4158299409416716288
	.type	rva_r832Rev, @object
	.size	rva_r832Rev, 8
rva_r832Rev:
	.dword	8475711401346727936
	.type	rva_r768Rev, @object
	.size	rva_r768Rev, 8
rva_r768Rev:
	.dword	1160278547361693696
	.type	rva_r704Rev, @object
	.size	rva_r704Rev, 8
rva_r704Rev:
	.dword	6486204699262320640
	.type	rva_r640Rev, @object
	.size	rva_r640Rev, 8
rva_r640Rev:
	.dword	-8195793492014792704
	.type	rva_r576Rev, @object
	.size	rva_r576Rev, 8
rva_r576Rev:
	.dword	7295154243206381568
	.type	rva_r512Rev, @object
	.size	rva_r512Rev, 8
rva_r512Rev:
	.dword	-3831562113130692608
	.type	rva_r448Rev, @object
	.size	rva_r448Rev, 8
rva_r448Rev:
	.dword	7623745401996181504
	.type	rva_r384Rev, @object
	.size	rva_r384Rev, 8
rva_r384Rev:
	.dword	3037739942219350016
	.type	rva_r320Rev, @object
	.size	rva_r320Rev, 8
rva_r320Rev:
	.dword	-7678403628251480064
	.type	rva_r256Rev, @object
	.size	rva_r256Rev, 8
rva_r256Rev:
	.dword	123282865818435584
	.type	rva_r192Rev, @object
	.size	rva_r192Rev, 8
rva_r192Rev:
	.dword	7306874092265668608
	.type	rva_r128Rev, @object
	.size	rva_r128Rev, 8
rva_r128Rev:
	.dword	-7231289587048382464
	.type	rva_r64Rev, @object
	.size	rva_r64Rev, 8
rva_r64Rev:
	.dword	-5135115791619850240
	.type	rva_rem_cst, @object
	.size	rva_rem_cst, 8
rva_rem_cst:
	.dword	7976584769
	.type	rva_REV_CRC32_BE_INV_EXT, @object
	.size	rva_REV_CRC32_BE_INV_EXT, 8
rva_REV_CRC32_BE_INV_EXT:
	.dword	-5411725700111460799
	.ident	"GCC: () 16.1.0"
	.section	.note.GNU-stack,"",@progbits
