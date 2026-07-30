	## LVX f64 conversion test (SoftFloat-backed ISS helpers, RISC-V FCVT.*).
	##
	## Exercises the f64<->integer and f64<->f32 conversion helpers now in the
	## gem5 shim. Values are exact integers / halves so the result is independent
	## of the rounding mode, plus two RISC-V out-of-range saturation cases:
	##   floatwd  i32->f64  5        -> 5.0
	##   floatuwd u32->f64  5        -> 5.0
	##   floatd   i64->f64  5        -> 5.0
	##   floatud  u64->f64  5        -> 5.0
	##   floatwd  i32->f64  -2       -> -2.0
	##   fwidenwd f32->f64  2.0f     -> 2.0
	##   fixeddw  f64->i32  4.0      -> 4
	##   fixedd   f64->i64  9.0      -> 9
	##   fixedudw f64->u32  10.0     -> 10
	##   fixedud  f64->u64  10.0     -> 10
	##   fnarrowdw f64->f32 1.5      -> 0x3fc00000
	##   fixeddw  f64->i32  NaN      -> 0x7fffffff  (RISC-V saturation)
	##   fixedudw f64->u32  -1.0     -> 0           (RISC-V: negative -> 0)
	## $r0 holds the case number; exit code is 0 on success or the first failing
	## case (1..13). 64-bit results use ccb.deq; 32-bit results use ccb.weq (which
	## compares only the low word, so result-register extension does not matter).
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: floatwd  i32->f64  5 -> 5.0
	make $r0 = 1
	;;
	make $r1 = 5
	;;
	floatwd $r3 = $r1
	;;
	make $r4 = 0x4014000000000000ULL		# 5.0
	;;
	ccb.deq $r3, $r4 ? c2
	;;
	goto fail
	;;
c2:
	## case 2: floatuwd  u32->f64  5 -> 5.0
	make $r0 = 2
	;;
	make $r1 = 5
	;;
	floatuwd $r3 = $r1
	;;
	make $r4 = 0x4014000000000000ULL		# 5.0
	;;
	ccb.deq $r3, $r4 ? c3
	;;
	goto fail
	;;
c3:
	## case 3: floatd  i64->f64  5 -> 5.0
	make $r0 = 3
	;;
	make $r1 = 5
	;;
	floatd $r3 = $r1
	;;
	make $r4 = 0x4014000000000000ULL		# 5.0
	;;
	ccb.deq $r3, $r4 ? c4
	;;
	goto fail
	;;
c4:
	## case 4: floatud  u64->f64  5 -> 5.0
	make $r0 = 4
	;;
	make $r1 = 5
	;;
	floatud $r3 = $r1
	;;
	make $r4 = 0x4014000000000000ULL		# 5.0
	;;
	ccb.deq $r3, $r4 ? c5
	;;
	goto fail
	;;
c5:
	## case 5: floatwd  i32->f64  -2 -> -2.0  (signed source)
	make $r0 = 5
	;;
	make $r1 = -2
	;;
	floatwd $r3 = $r1
	;;
	make $r4 = 0xc000000000000000ULL		# -2.0
	;;
	ccb.deq $r3, $r4 ? c6
	;;
	goto fail
	;;
c6:
	## case 6: fwidenwd  f32->f64  2.0f -> 2.0
	make $r0 = 6
	;;
	make $r1 = 0x40000000				# 2.0f
	;;
	fwidenwd $r3 = $r1
	;;
	make $r4 = 0x4000000000000000ULL		# 2.0
	;;
	ccb.deq $r3, $r4 ? c7
	;;
	goto fail
	;;
c7:
	## case 7: fixeddw  f64->i32  4.0 -> 4
	make $r0 = 7
	;;
	make $r1 = 0x4010000000000000ULL		# 4.0
	;;
	fixeddw $r3 = $r1
	;;
	make $r4 = 4
	;;
	ccb.weq $r3, $r4 ? c8
	;;
	goto fail
	;;
c8:
	## case 8: fixedd  f64->i64  9.0 -> 9
	make $r0 = 8
	;;
	make $r1 = 0x4022000000000000ULL		# 9.0
	;;
	fixedd $r3 = $r1
	;;
	make $r4 = 9
	;;
	ccb.deq $r3, $r4 ? c9
	;;
	goto fail
	;;
c9:
	## case 9: fixedudw  f64->u32  10.0 -> 10
	make $r0 = 9
	;;
	make $r1 = 0x4024000000000000ULL		# 10.0
	;;
	fixedudw $r3 = $r1
	;;
	make $r4 = 10
	;;
	ccb.weq $r3, $r4 ? c10
	;;
	goto fail
	;;
c10:
	## case 10: fixedud  f64->u64  10.0 -> 10
	make $r0 = 10
	;;
	make $r1 = 0x4024000000000000ULL		# 10.0
	;;
	fixedud $r3 = $r1
	;;
	make $r4 = 10
	;;
	ccb.deq $r3, $r4 ? c11
	;;
	goto fail
	;;
c11:
	## case 11: fnarrowdw  f64->f32  1.5 -> 0x3fc00000
	make $r0 = 11
	;;
	make $r1 = 0x3ff8000000000000ULL		# 1.5
	;;
	fnarrowdw $r3 = $r1
	;;
	make $r4 = 0x3fc00000				# 1.5f
	;;
	ccb.weq $r3, $r4 ? c12
	;;
	goto fail
	;;
c12:
	## case 12: fixeddw  f64->i32  NaN -> 0x7fffffff  (RISC-V saturation)
	make $r0 = 12
	;;
	make $r1 = 0x7ff8000000000000ULL		# quiet NaN
	;;
	fixeddw $r3 = $r1
	;;
	make $r4 = 0x7fffffff				# INT32_MAX
	;;
	ccb.weq $r3, $r4 ? c13
	;;
	goto fail
	;;
c13:
	## case 13: fixedudw  f64->u32  -1.0 -> 0  (RISC-V: negative saturates to 0)
	make $r0 = 13
	;;
	make $r1 = 0xbff0000000000000ULL		# -1.0
	;;
	fixedudw $r3 = $r1
	;;
	make $r4 = 0
	;;
	ccb.weq $r3, $r4 ? done
	;;
	goto fail
	;;
done:
	## all conversions matched
	make $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
