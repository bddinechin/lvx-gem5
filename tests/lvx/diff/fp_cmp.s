	## LVX f64 IEEE-754 functional test (SoftFloat-backed ISS helpers).
	##
	## Exercises the five f64 operator helpers now implemented in the gem5 shim
	## (Behavior_f64_{add,sub,mul,mulAdd,mulnAdd}) with exact operands, so the
	## expected result is a known bit pattern independent of rounding mode:
	##   faddd  2.0 + 3.0        = 5.0   0x4014000000000000
	##   fsbfd  5.0 - 3.0        = 2.0   0x4000000000000000  (%2 subtracted from %3 => ry - rz)
	##   fmuld  2.0 * 3.0        = 6.0   0x4018000000000000
	##   ffmad  2.0*3.0 + 1.0    = 7.0   0x401c000000000000  (fused, accumulator = dest)
	##   ffmsd  1.0 - 2.0*3.0    = -5.0  0xc014000000000000  (product subtracted from acc)
	## Same self-checking shape as ccb_cmp.s: $r0 holds the current case number, so
	## the process exit code is 0 on full success or the index (1..5) of the first
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
