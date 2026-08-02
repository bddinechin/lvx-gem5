	## LVX f64 comparison test (fcompd / floatcomp_64 ISS helper).
	##
	## Exercises all eight ordered/unordered predicates. fcompd writes a boolean
	## (0/1) to $rw; $r0 holds the case number so the exit code is 0 on success or
	## the first failing case (1..13). Operands: 2.0<3.0 (ordered), 2.0==2.0, and
	## a quiet NaN (unordered).
	##   oeq(2,2)=1  oeq(2,3)=0  olt(2,3)=1  olt(3,2)=0  oge(3,2)=1  oge(2,2)=1
	##   one(2,3)=1  une(2,2)=0  olt(NaN,3)=0  ult(NaN,3)=1  ueq(NaN,3)=1
	##   uge(NaN,3)=1  oeq(NaN,3)=0
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: oeq(2.0, 2.0) = 1
	maked $r0 = 1
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4000000000000000ULL		# 2.0
	;;
	fcompd.oeq $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c2
	;;
	goto fail
	;;
c2:
	## case 2: oeq(2.0, 3.0) = 0
	maked $r0 = 2
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fcompd.oeq $r3 = $r1, $r2
	;;
	maked $r4 = 0
	;;
	ccb.deq $r3, $r4 ? c3
	;;
	goto fail
	;;
c3:
	## case 3: olt(2.0, 3.0) = 1
	maked $r0 = 3
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fcompd.olt $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c4
	;;
	goto fail
	;;
c4:
	## case 4: olt(3.0, 2.0) = 0
	maked $r0 = 4
	;;
	maked $r1 = 0x4008000000000000ULL		# 3.0
	;;
	maked $r2 = 0x4000000000000000ULL		# 2.0
	;;
	fcompd.olt $r3 = $r1, $r2
	;;
	maked $r4 = 0
	;;
	ccb.deq $r3, $r4 ? c5
	;;
	goto fail
	;;
c5:
	## case 5: oge(3.0, 2.0) = 1
	maked $r0 = 5
	;;
	maked $r1 = 0x4008000000000000ULL		# 3.0
	;;
	maked $r2 = 0x4000000000000000ULL		# 2.0
	;;
	fcompd.oge $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c6
	;;
	goto fail
	;;
c6:
	## case 6: oge(2.0, 2.0) = 1  (equal satisfies >=)
	maked $r0 = 6
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4000000000000000ULL		# 2.0
	;;
	fcompd.oge $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c7
	;;
	goto fail
	;;
c7:
	## case 7: one(2.0, 3.0) = 1  (ordered and not equal)
	maked $r0 = 7
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fcompd.one $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c8
	;;
	goto fail
	;;
c8:
	## case 8: une(2.0, 2.0) = 0  (ordered and equal => not (unord or !=))
	maked $r0 = 8
	;;
	maked $r1 = 0x4000000000000000ULL		# 2.0
	;;
	maked $r2 = 0x4000000000000000ULL		# 2.0
	;;
	fcompd.une $r3 = $r1, $r2
	;;
	maked $r4 = 0
	;;
	ccb.deq $r3, $r4 ? c9
	;;
	goto fail
	;;
c9:
	## case 9: olt(NaN, 3.0) = 0  (unordered => ordered predicate false)
	maked $r0 = 9
	;;
	maked $r1 = 0x7ff8000000000000ULL		# quiet NaN
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fcompd.olt $r3 = $r1, $r2
	;;
	maked $r4 = 0
	;;
	ccb.deq $r3, $r4 ? c10
	;;
	goto fail
	;;
c10:
	## case 10: ult(NaN, 3.0) = 1  (unordered => unordered predicate true)
	maked $r0 = 10
	;;
	maked $r1 = 0x7ff8000000000000ULL		# quiet NaN
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fcompd.ult $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c11
	;;
	goto fail
	;;
c11:
	## case 11: ueq(NaN, 3.0) = 1
	maked $r0 = 11
	;;
	maked $r1 = 0x7ff8000000000000ULL		# quiet NaN
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fcompd.ueq $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c12
	;;
	goto fail
	;;
c12:
	## case 12: uge(NaN, 3.0) = 1
	maked $r0 = 12
	;;
	maked $r1 = 0x7ff8000000000000ULL		# quiet NaN
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fcompd.uge $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c13
	;;
	goto fail
	;;
c13:
	## case 13: oeq(NaN, 3.0) = 0
	maked $r0 = 13
	;;
	maked $r1 = 0x7ff8000000000000ULL		# quiet NaN
	;;
	maked $r2 = 0x4008000000000000ULL		# 3.0
	;;
	fcompd.oeq $r3 = $r1, $r2
	;;
	maked $r4 = 0
	;;
	ccb.deq $r3, $r4 ? done
	;;
	goto fail
	;;
done:
	## all predicates matched
	maked $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
