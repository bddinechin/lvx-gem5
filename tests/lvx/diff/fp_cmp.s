	## LVX f64 IEEE-754 functional test (SoftFloat-backed ISS helpers).
	##
	## Exercises the f64 operator helpers now implemented in the gem5 shim
	## (Behavior_f64_{add,sub,mul,mulAdd,mulnAdd,div,sqrt,rint,min,max,minNum,
	## maxNum}) with exact operands, so the expected result is a known bit pattern:
	##   faddd  2.0 + 3.0        = 5.0   0x4014000000000000
	##   fsbfd  5.0 - 3.0        = 2.0   0x4000000000000000  (%2 subtracted from %3 => ry - rz)
	##   fmuld  2.0 * 3.0        = 6.0   0x4018000000000000
	##   ffmad  2.0*3.0 + 1.0    = 7.0   0x401c000000000000  (fused, accumulator = dest)
	##   ffmsd  1.0 - 2.0*3.0    = -5.0  0xc014000000000000  (product subtracted from acc)
	##   fdivd  6.0 / 3.0        = 2.0   0x4000000000000000
	##   fsqrtd sqrt(4.0)        = 2.0   0x4000000000000000
	##   frintd rint(2.75)       = 3.0   0x4008000000000000  (round to nearest even)
	##   fmind  min(2.0,3.0)     = 2.0   |  fmaxd  max(2.0,3.0) = 3.0
	##   fminnd minNum(2.0,3.0)  = 2.0   |  fmaxnd maxNum(2.0,3.0) = 3.0
	##   fmind(NaN,3.0)  = canonical NaN 0x7ff8000000000000  (min PROPAGATES NaN)
	##   fmaxnd(NaN,3.0) = 3.0                                (maxNum RETURNS the number)
	## Same self-checking shape as ccb_cmp.s: $r0 holds the current case number, so
	## the process exit code is 0 on full success or the index (1..17) of the first
	## op whose result bits did not match. ccb.deq compares the raw 64-bit patterns.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: faddd  2.0 + 3.0 = 5.0
	make $r0 = 1
	;;
	make $r1 = 0x4000000000000000ULL		# 2.0
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0
	;;
	faddd $r3 = $r1, $r2
	;;
	make $r4 = 0x4014000000000000ULL		# 5.0
	;;
	ccb.deq $r3, $r4 ? c2
	;;
	goto fail
	;;
c2:
	## case 2: fsbfd  ry - rz = 5.0 - 3.0 = 2.0
	make $r0 = 2
	;;
	make $r1 = 0x4008000000000000ULL		# 3.0 (rz)
	;;
	make $r2 = 0x4014000000000000ULL		# 5.0 (ry)
	;;
	fsbfd $r3 = $r1, $r2
	;;
	make $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c3
	;;
	goto fail
	;;
c3:
	## case 3: fmuld  2.0 * 3.0 = 6.0
	make $r0 = 3
	;;
	make $r1 = 0x4000000000000000ULL		# 2.0
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fmuld $r3 = $r1, $r2
	;;
	make $r4 = 0x4018000000000000ULL		# 6.0
	;;
	ccb.deq $r3, $r4 ? c4
	;;
	goto fail
	;;
c4:
	## case 4: ffmad  2.0*3.0 + 1.0 = 7.0  (accumulator is the destination $r3)
	make $r0 = 4
	;;
	make $r1 = 0x4000000000000000ULL		# 2.0
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0
	;;
	make $r3 = 0x3ff0000000000000ULL		# 1.0 (accumulator)
	;;
	ffmad $r3 = $r1, $r2
	;;
	make $r4 = 0x401c000000000000ULL		# 7.0
	;;
	ccb.deq $r3, $r4 ? c5
	;;
	goto fail
	;;
c5:
	## case 5: ffmsd  1.0 - 2.0*3.0 = -5.0  (product subtracted from accumulator)
	make $r0 = 5
	;;
	make $r1 = 0x4000000000000000ULL		# 2.0
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0
	;;
	make $r3 = 0x3ff0000000000000ULL		# 1.0 (accumulator)
	;;
	ffmsd $r3 = $r1, $r2
	;;
	make $r4 = 0xc014000000000000ULL		# -5.0
	;;
	ccb.deq $r3, $r4 ? c6
	;;
	goto fail
	;;
c6:
	## case 6: fdivd  rz / ry = 6.0 / 3.0 = 2.0
	make $r0 = 6
	;;
	make $r1 = 0x4018000000000000ULL		# 6.0 (rz, numerator)
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0 (ry, denominator)
	;;
	fdivd $r3 = $r1, $r2
	;;
	make $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c7
	;;
	goto fail
	;;
c7:
	## case 7: fsqrtd  sqrt(4.0) = 2.0
	make $r0 = 7
	;;
	make $r1 = 0x4010000000000000ULL		# 4.0
	;;
	fsqrtd $r3 = $r1
	;;
	make $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c8
	;;
	goto fail
	;;
c8:
	## case 8: frintd  rint(2.75) = 3.0 (round to nearest even, reset $cs.RM = RN)
	make $r0 = 8
	;;
	make $r1 = 0x4006000000000000ULL		# 2.75
	;;
	frintd $r3 = $r1
	;;
	make $r4 = 0x4008000000000000ULL		# 3.0
	;;
	ccb.deq $r3, $r4 ? c9
	;;
	goto fail
	;;
c9:
	## case 9: fmind  min(2.0, 3.0) = 2.0
	make $r0 = 9
	;;
	make $r1 = 0x4000000000000000ULL		# 2.0
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fmind $r3 = $r1, $r2
	;;
	make $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c10
	;;
	goto fail
	;;
c10:
	## case 10: fmaxd  max(2.0, 3.0) = 3.0
	make $r0 = 10
	;;
	make $r1 = 0x4000000000000000ULL		# 2.0
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fmaxd $r3 = $r1, $r2
	;;
	make $r4 = 0x4008000000000000ULL		# 3.0
	;;
	ccb.deq $r3, $r4 ? c11
	;;
	goto fail
	;;
c11:
	## case 11: fminnd  minNum(2.0, 3.0) = 2.0
	make $r0 = 11
	;;
	make $r1 = 0x4000000000000000ULL		# 2.0
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fminnd $r3 = $r1, $r2
	;;
	make $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c12
	;;
	goto fail
	;;
c12:
	## case 12: fmaxnd  maxNum(2.0, 3.0) = 3.0
	make $r0 = 12
	;;
	make $r1 = 0x4000000000000000ULL		# 2.0
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fmaxnd $r3 = $r1, $r2
	;;
	make $r4 = 0x4008000000000000ULL		# 3.0
	;;
	ccb.deq $r3, $r4 ? c13
	;;
	goto fail
	;;
c13:
	## case 13: fmind(NaN, 3.0) = canonical NaN  (min PROPAGATES NaN)
	make $r0 = 13
	;;
	make $r1 = 0x7ff8000000000000ULL		# quiet NaN (rz)
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0 (ry)
	;;
	fmind $r3 = $r1, $r2
	;;
	make $r4 = 0x7ff8000000000000ULL		# canonical NaN
	;;
	ccb.deq $r3, $r4 ? c14
	;;
	goto fail
	;;
c14:
	## case 14: fmaxnd(NaN, 3.0) = 3.0  (maxNum RETURNS THE NUMBER; distinguishes
	## the Num variant from the NaN-propagating fmaxd)
	make $r0 = 14
	;;
	make $r1 = 0x7ff8000000000000ULL		# quiet NaN (rz)
	;;
	make $r2 = 0x4008000000000000ULL		# 3.0 (ry)
	;;
	fmaxnd $r3 = $r1, $r2
	;;
	make $r4 = 0x4008000000000000ULL		# 3.0
	;;
	ccb.deq $r3, $r4 ? c15
	;;
	goto fail
	;;
c15:
	## case 15: fminnd(-0.0, +0.0) = -0.0  (RISC-V FMIN: -0.0 < +0.0)
	make $r0 = 15
	;;
	make $r1 = 0x8000000000000000ULL		# -0.0 (rz)
	;;
	make $r2 = 0x0000000000000000ULL		# +0.0 (ry)
	;;
	fminnd $r3 = $r1, $r2
	;;
	make $r4 = 0x8000000000000000ULL		# -0.0
	;;
	ccb.deq $r3, $r4 ? c16
	;;
	goto fail
	;;
c16:
	## case 16: fmaxnd(-0.0, +0.0) = +0.0  (RISC-V FMAX: +0.0 > -0.0)
	make $r0 = 16
	;;
	make $r1 = 0x8000000000000000ULL		# -0.0 (rz)
	;;
	make $r2 = 0x0000000000000000ULL		# +0.0 (ry)
	;;
	fmaxnd $r3 = $r1, $r2
	;;
	make $r4 = 0x0000000000000000ULL		# +0.0
	;;
	ccb.deq $r3, $r4 ? c17
	;;
	goto fail
	;;
c17:
	## case 17: fmind(-0.0, +0.0) = -0.0  (propagating min also honors -0 < +0)
	make $r0 = 17
	;;
	make $r1 = 0x8000000000000000ULL		# -0.0 (rz)
	;;
	make $r2 = 0x0000000000000000ULL		# +0.0 (ry)
	;;
	fmind $r3 = $r1, $r2
	;;
	make $r4 = 0x8000000000000000ULL		# -0.0
	;;
	ccb.deq $r3, $r4 ? done
	;;
	goto fail
	;;
done:
	## all cases matched
	make $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
