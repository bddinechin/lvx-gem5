	## LVX reciprocal / reciprocal-sqrt SEED test (RISC-V vfrec7/vfrsqrt7).
	##
	## fsrec{d,w} / fsrsr{d,w} are 7-bit estimates -- NOT exact reciprocals (e.g.
	## 1/2 estimates to ~0.498, not 0.5). Expected bit patterns were produced by
	## the vendored SoftFloat f{32,64}_recip7/rsqrte7 (the RISC-V reference), so
	## this checks the ISS matches RISC-V vfrec7/vfrsqrt7 exactly. $r0 = case
	## number; exit code 0 on success or first failing case (1..8). f64 results
	## use ccb.deq; f32 results (low 32) use ccb.weq.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: fsrecd rec(2.0) = 0x3fdfe00000000000
	maked $r0 = 1
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	fsrecd $r3 = $r1
	;;
	maked $r4 = 0x3fdfe00000000000ULL
	;;
	ccb.deq $r3, $r4 ? c2
	;;
	goto fail
	;;
c2:
	## case 2: fsrecd rec(3.0) = 0x3fd5400000000000
	maked $r0 = 2
	;;
	maked $r1 = 0x4008000000000000ULL		# 3.0
	;;
	fsrecd $r3 = $r1
	;;
	maked $r4 = 0x3fd5400000000000ULL
	;;
	ccb.deq $r3, $r4 ? c3
	;;
	goto fail
	;;
c3:
	## case 3: fsrsrd rsqrt(4.0) = 0x3fdfe00000000000
	maked $r0 = 3
	;;
	maked $r1 = 0x4010000000000000ULL		# 4.0
	;;
	fsrsrd $r3 = $r1
	;;
	maked $r4 = 0x3fdfe00000000000ULL
	;;
	ccb.deq $r3, $r4 ? c4
	;;
	goto fail
	;;
c4:
	## case 4: fsrsrd rsqrt(1.0) = 0x3fefe00000000000
	maked $r0 = 4
	;;
	maked $r1 = 0x3ff0000000000000ULL		# 1.0
	;;
	fsrsrd $r3 = $r1
	;;
	maked $r4 = 0x3fefe00000000000ULL
	;;
	ccb.deq $r3, $r4 ? c5
	;;
	goto fail
	;;
c5:
	## case 5: fsrecw rec(2.0f) = 0x3eff0000
	maked $r0 = 5
	;;
	maked $r1 = 0x40000000		# 2.0f
	;;
	fsrecw $r3 = $r1
	;;
	maked $r4 = 0x3eff0000
	;;
	ccb.weq $r3, $r4 ? c6
	;;
	goto fail
	;;
c6:
	## case 6: fsrecw rec(3.0f) = 0x3eaa0000
	maked $r0 = 6
	;;
	maked $r1 = 0x40400000		# 3.0f
	;;
	fsrecw $r3 = $r1
	;;
	maked $r4 = 0x3eaa0000
	;;
	ccb.weq $r3, $r4 ? c7
	;;
	goto fail
	;;
c7:
	## case 7: fsrsrw rsqrt(4.0f) = 0x3eff0000
	maked $r0 = 7
	;;
	maked $r1 = 0x40800000		# 4.0f
	;;
	fsrsrw $r3 = $r1
	;;
	maked $r4 = 0x3eff0000
	;;
	ccb.weq $r3, $r4 ? c8
	;;
	goto fail
	;;
c8:
	## case 8: fsrsrw rsqrt(1.0f) = 0x3f7f0000
	maked $r0 = 8
	;;
	maked $r1 = 0x3f800000		# 1.0f
	;;
	fsrsrw $r3 = $r1
	;;
	maked $r4 = 0x3f7f0000
	;;
	ccb.weq $r3, $r4 ? done
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
