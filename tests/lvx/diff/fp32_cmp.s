	## LVX f32 (binary32) functional test — arithmetic, min/max, compare,
	## conversions, classify. Mirrors fp_cmp.s for the 32-bit width. $r0 holds the
	## case number; exit code is 0 on success or the first failing case (1..21).
	## f32 results are 32-bit, compared with ccb.weq (low word); f16 results (case
	## 19) are masked to 16 bits; booleans/small ints with ccb.deq.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: faddw 2+3=5
	maked $r0 = 1
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	faddw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40a00000		# 5.0f
	;;
	ccb.weq $r3, $r4 ? c2
	;;
	goto fail
	;;
c2:
	## case 2: fsbfw ry-rz = 5-3 = 2
	maked $r0 = 2
	;;
	maked $r1 = 0x40400000		# 3.0f (rz)
	;;
	maked $r2 = 0x40a00000		# 5.0f (ry)
	;;
	fsbfw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40000000		# 2.0f
	;;
	ccb.weq $r3, $r4 ? c3
	;;
	goto fail
	;;
c3:
	## case 3: fmulw 2*3=6
	maked $r0 = 3
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	fmulw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40c00000		# 6.0f
	;;
	ccb.weq $r3, $r4 ? c4
	;;
	goto fail
	;;
c4:
	## case 4: ffmaw 2*3+1=7
	maked $r0 = 4
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	maked $r3 = 0x3f800000		# 1.0f (accumulator)
	;;
	ffmaw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40e00000		# 7.0f
	;;
	ccb.weq $r3, $r4 ? c5
	;;
	goto fail
	;;
c5:
	## case 5: ffmsw 1-2*3=-5
	maked $r0 = 5
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	maked $r3 = 0x3f800000		# 1.0f (accumulator)
	;;
	ffmsw $r3 = $r1, $r2
	;;
	maked $r4 = 0xc0a00000		# -5.0f
	;;
	ccb.weq $r3, $r4 ? c6
	;;
	goto fail
	;;
c6:
	## case 6: fdivw 6/3=2
	maked $r0 = 6
	;;
	maked $r1 = 0x40c00000		# 6.0f (rz)
	;;
	maked $r2 = 0x40400000		# 3.0f (ry)
	;;
	fdivw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40000000		# 2.0f
	;;
	ccb.weq $r3, $r4 ? c7
	;;
	goto fail
	;;
c7:
	## case 7: fsqrtw sqrt(4)=2
	maked $r0 = 7
	;;
	maked $r1 = 0x40800000		# 4.0f
	;;
	fsqrtw $r3 = $r1
	;;
	maked $r4 = 0x40000000		# 2.0f
	;;
	ccb.weq $r3, $r4 ? c8
	;;
	goto fail
	;;
c8:
	## case 8: frintw rint(2.75)=3
	maked $r0 = 8
	;;
	maked $r1 = 0x40300000		# 2.75f
	;;
	frintw $r3 = $r1
	;;
	maked $r4 = 0x40400000		# 3.0f
	;;
	ccb.weq $r3, $r4 ? c9
	;;
	goto fail
	;;
c9:
	## case 9: fminw min(2,3)=2
	maked $r0 = 9
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	fminw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40000000		# 2.0f
	;;
	ccb.weq $r3, $r4 ? c10
	;;
	goto fail
	;;
c10:
	## case 10: fmaxw max(2,3)=3
	maked $r0 = 10
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	fmaxw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40400000		# 3.0f
	;;
	ccb.weq $r3, $r4 ? c11
	;;
	goto fail
	;;
c11:
	## case 11: fminnw minNum(2,3)=2
	maked $r0 = 11
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	fminnw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40000000		# 2.0f
	;;
	ccb.weq $r3, $r4 ? c12
	;;
	goto fail
	;;
c12:
	## case 12: fmaxnw maxNum(2,3)=3
	maked $r0 = 12
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	fmaxnw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40400000		# 3.0f
	;;
	ccb.weq $r3, $r4 ? c13
	;;
	goto fail
	;;
c13:
	## case 13: fminw(NaN,3) = canonical NaN (propagate)
	maked $r0 = 13
	;;
	maked $r1 = 0x7fc00000		# qNaN f32
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	fminw $r3 = $r1, $r2
	;;
	maked $r4 = 0x7fc00000		# canonical NaN
	;;
	ccb.weq $r3, $r4 ? c14
	;;
	goto fail
	;;
c14:
	## case 14: fmaxnw(NaN,3) = 3 (return number)
	maked $r0 = 14
	;;
	maked $r1 = 0x7fc00000		# qNaN f32
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	fmaxnw $r3 = $r1, $r2
	;;
	maked $r4 = 0x40400000		# 3.0f
	;;
	ccb.weq $r3, $r4 ? c15
	;;
	goto fail
	;;
c15:
	## case 15: fcompw.oeq(2,2)=1
	maked $r0 = 15
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40000000		# 2.0f
	;;
	fcompw.oeq $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c16
	;;
	goto fail
	;;
c16:
	## case 16: fcompw.olt(2,3)=1
	maked $r0 = 16
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	maked $r2 = 0x40400000		# 3.0f
	;;
	fcompw.olt $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c17
	;;
	goto fail
	;;
c17:
	## case 17: fixedw f32->i32 4.0 -> 4
	maked $r0 = 17
	;;
	maked $r1 = 0x40800000		# 4.0f
	;;
	fixedw $r3 = $r1
	;;
	maked $r4 = 4
	;;
	ccb.weq $r3, $r4 ? c18
	;;
	goto fail
	;;
c18:
	## case 18: floatw i32->f32 5 -> 5.0f
	maked $r0 = 18
	;;
	maked $r1 = 5
	;;
	floatw $r3 = $r1
	;;
	maked $r4 = 0x40a00000		# 5.0f
	;;
	ccb.weq $r3, $r4 ? c19
	;;
	goto fail
	;;
c19:
	## case 19: fnarrowwh f32->f16 1.5f -> 0x3e00 (mask low 16)
	maked $r0 = 19
	;;
	maked $r1 = 0x3fc00000		# 1.5f
	;;
	fnarrowwh $r3 = $r1
	;;
	maked $r5 = 0xffff
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x3e00		# 1.5 in f16
	;;
	ccb.deq $r3, $r4 ? c20
	;;
	goto fail
	;;
c20:
	## case 20: fwidenhw f16->f32 2.0h(0x4000) -> 2.0f
	maked $r0 = 20
	;;
	maked $r1 = 0x4000		# 2.0 f16
	;;
	fwidenhw $r3 = $r1
	;;
	maked $r4 = 0x40000000		# 2.0f
	;;
	ccb.weq $r3, $r4 ? c21
	;;
	goto fail
	;;
c21:
	## case 21: fclassw classify(2.0f) = +normal = bit 6 = 0x40
	maked $r0 = 21
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	fclassw $r3 = $r1
	;;
	maked $r4 = 0x40			# +normal
	;;
	ccb.deq $r3, $r4 ? done
	;;
	goto fail
	;;
done:
	maked $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
