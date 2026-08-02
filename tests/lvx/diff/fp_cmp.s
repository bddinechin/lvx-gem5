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
	##   frintd rint(2.75)       = 3.0   0x4008000000000000  (RN; also raises inexact, case 18)
	##   fmind  min(2.0,3.0)     = 2.0   |  fmaxd  max(2.0,3.0) = 3.0
	##   fminnd minNum(2.0,3.0)  = 2.0   |  fmaxnd maxNum(2.0,3.0) = 3.0
	##   fmind(NaN,3.0)  = canonical NaN 0x7ff8000000000000  (min PROPAGATES NaN)
	##   fmaxnd(NaN,3.0) = 3.0                                (maxNum RETURNS the number)
	## Same self-checking shape as ccb_cmp.s: $r0 holds the current case number, so
	## the process exit code is 0 on full success or the index (1..18) of the first
	## op whose result bits did not match. ccb.deq compares the raw 64-bit patterns.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: faddd  2.0 + 3.0 = 5.0
	maked $r0 = 1
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	faddd $r3 = $r1, $r2
	;;
	maked $r4 = 0x4014000000000000ULL		# 5.0
	;;
	ccb.deq $r3, $r4 ? c2
	;;
	goto fail
	;;
c2:
	## case 2: fsbfd  ry - rz = 5.0 - 3.0 = 2.0
	maked $r0 = 2
	;;
	maked $r1 = 0x4008000000000000ULL		# 3.0 (rz)
	;;
	maked $r2 = 0x4014000000000000ULL		# 5.0 (ry)
	;;
	fsbfd $r3 = $r1, $r2
	;;
	maked $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c3
	;;
	goto fail
	;;
c3:
	## case 3: fmuld  2.0 * 3.0 = 6.0
	maked $r0 = 3
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fmuld $r3 = $r1, $r2
	;;
	maked $r4 = 0x4018000000000000ULL		# 6.0
	;;
	ccb.deq $r3, $r4 ? c4
	;;
	goto fail
	;;
c4:
	## case 4: ffmad  2.0*3.0 + 1.0 = 7.0  (accumulator is the destination $r3)
	maked $r0 = 4
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	maked $r3 = 0x3ff0000000000000ULL		# 1.0 (accumulator)
	;;
	ffmad $r3 = $r1, $r2
	;;
	maked $r4 = 0x401c000000000000ULL		# 7.0
	;;
	ccb.deq $r3, $r4 ? c5
	;;
	goto fail
	;;
c5:
	## case 5: ffmsd  1.0 - 2.0*3.0 = -5.0  (product subtracted from accumulator)
	maked $r0 = 5
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	maked $r3 = 0x3ff0000000000000ULL		# 1.0 (accumulator)
	;;
	ffmsd $r3 = $r1, $r2
	;;
	maked $r4 = 0xc014000000000000ULL		# -5.0
	;;
	ccb.deq $r3, $r4 ? c6
	;;
	goto fail
	;;
c6:
	## case 6: fdivd  rz / ry = 6.0 / 3.0 = 2.0
	maked $r0 = 6
	;;
	maked $r1 = 0x4018000000000000ULL		# 6.0 (rz, numerator)
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0 (ry, denominator)
	;;
	fdivd $r3 = $r1, $r2
	;;
	maked $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c7
	;;
	goto fail
	;;
c7:
	## case 7: fsqrtd  sqrt(4.0) = 2.0
	maked $r0 = 7
	;;
	maked $r1 = 0x4010000000000000ULL		# 4.0
	;;
	fsqrtd $r3 = $r1
	;;
	maked $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c8
	;;
	goto fail
	;;
c8:
	## case 8: frintd  rint(2.75) = 3.0 (round to nearest even, reset $cs.RM = RN)
	maked $r0 = 8
	;;
	maked $r1 = 0x4006000000000000ULL		# 2.75
	;;
	frintd $r3 = $r1
	;;
	maked $r4 = 0x4008000000000000ULL		# 3.0
	;;
	ccb.deq $r3, $r4 ? c9
	;;
	goto fail
	;;
c9:
	## case 9: fmind  min(2.0, 3.0) = 2.0
	maked $r0 = 9
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fmind $r3 = $r1, $r2
	;;
	maked $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c10
	;;
	goto fail
	;;
c10:
	## case 10: fmaxd  max(2.0, 3.0) = 3.0
	maked $r0 = 10
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fmaxd $r3 = $r1, $r2
	;;
	maked $r4 = 0x4008000000000000ULL		# 3.0
	;;
	ccb.deq $r3, $r4 ? c11
	;;
	goto fail
	;;
c11:
	## case 11: fminnd  minNum(2.0, 3.0) = 2.0
	maked $r0 = 11
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fminnd $r3 = $r1, $r2
	;;
	maked $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c12
	;;
	goto fail
	;;
c12:
	## case 12: fmaxnd  maxNum(2.0, 3.0) = 3.0
	maked $r0 = 12
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fmaxnd $r3 = $r1, $r2
	;;
	maked $r4 = 0x4008000000000000ULL		# 3.0
	;;
	ccb.deq $r3, $r4 ? c13
	;;
	goto fail
	;;
c13:
	## case 13: fmind(NaN, 3.0) = canonical NaN  (min PROPAGATES NaN)
	maked $r0 = 13
	;;
	maked $r1 = 0x7ff8000000000000ULL		# quiet NaN (rz)
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0 (ry)
	;;
	fmind $r3 = $r1, $r2
	;;
	maked $r4 = 0x7ff8000000000000ULL		# canonical NaN
	;;
	ccb.deq $r3, $r4 ? c14
	;;
	goto fail
	;;
c14:
	## case 14: fmaxnd(NaN, 3.0) = 3.0  (maxNum RETURNS THE NUMBER; distinguishes
	## the Num variant from the NaN-propagating fmaxd)
	maked $r0 = 14
	;;
	maked $r1 = 0x7ff8000000000000ULL		# quiet NaN (rz)
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0 (ry)
	;;
	fmaxnd $r3 = $r1, $r2
	;;
	maked $r4 = 0x4008000000000000ULL		# 3.0
	;;
	ccb.deq $r3, $r4 ? c15
	;;
	goto fail
	;;
c15:
	## case 15: fminnd(-0.0, +0.0) = -0.0  (RISC-V FMIN: -0.0 < +0.0)
	maked $r0 = 15
	;;
	maked $r1 = 0x8000000000000000ULL		# -0.0 (rz)
	;;
	maked $r2 = 0x0000000000000000ULL		# +0.0 (ry)
	;;
	fminnd $r3 = $r1, $r2
	;;
	maked $r4 = 0x8000000000000000ULL		# -0.0
	;;
	ccb.deq $r3, $r4 ? c16
	;;
	goto fail
	;;
c16:
	## case 16: fmaxnd(-0.0, +0.0) = +0.0  (RISC-V FMAX: +0.0 > -0.0)
	maked $r0 = 16
	;;
	maked $r1 = 0x8000000000000000ULL		# -0.0 (rz)
	;;
	maked $r2 = 0x0000000000000000ULL		# +0.0 (ry)
	;;
	fmaxnd $r3 = $r1, $r2
	;;
	maked $r4 = 0x0000000000000000ULL		# +0.0
	;;
	ccb.deq $r3, $r4 ? c17
	;;
	goto fail
	;;
c17:
	## case 17: fmind(-0.0, +0.0) = -0.0  (propagating min also honors -0 < +0)
	maked $r0 = 17
	;;
	maked $r1 = 0x8000000000000000ULL		# -0.0 (rz)
	;;
	maked $r2 = 0x0000000000000000ULL		# +0.0 (ry)
	;;
	fmind $r3 = $r1, $r2
	;;
	maked $r4 = 0x8000000000000000ULL		# -0.0
	;;
	ccb.deq $r3, $r4 ? c18
	;;
	goto fail
	;;
c18:
	## case 18: frintd raises inexact (RISC-V FROUNDNX.D). Clear $cs, round an
	## inexact value (2.75 -> 3.0), and confirm the IN flag (bit 5 = 0x20) is set.
	maked $r0 = 18
	;;
	maked $r5 = 0
	;;
	set $cs = $r5					# clear exception flags + RM (= RN)
	;;
	maked $r1 = 0x4006000000000000ULL		# 2.75
	;;
	frintd $r3 = $r1				# -> 3.0, inexact
	;;
	get $r6 = $cs					# read CS
	;;
	maked $r7 = 0x20					# inexact flag (fin << 5)
	;;
	andd $r6 = $r6, $r7				# isolate the inexact bit
	;;
	ccb.deq $r6, $r7 ? done				# inexact raised?
	;;
	goto fail
	;;
done:
	## all cases matched
	maked $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
