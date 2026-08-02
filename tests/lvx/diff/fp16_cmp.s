	## LVX f16 (binary16) functional test — arithmetic, min/max, compare, classify.
	## Mirrors fp_cmp.s for the 16-bit width. $r0 holds the case number; exit code
	## is 0 on success or the first failing case (1..17). f16 results are masked to
	## 16 bits ($r5 = 0xffff) before comparison, since the register extension of a
	## 16-bit result is unspecified; booleans/small ints compare directly.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: faddh 2+3=5 (0x4500)
	maked $r0 = 1
	;;
	maked $r5 = 0xffff
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	faddh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4500		# 5.0
	;;
	ccb.deq $r3, $r4 ? c2
	;;
	goto fail
	;;
c2:
	## case 2: fsbfh ry-rz = 5-3 = 2 (0x4000)
	maked $r0 = 2
	;;
	maked $r1 = 0x4200		# 3.0 (rz)
	;;
	maked $r2 = 0x4500		# 5.0 (ry)
	;;
	fsbfh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4000		# 2.0
	;;
	ccb.deq $r3, $r4 ? c3
	;;
	goto fail
	;;
c3:
	## case 3: fmulh 2*3=6 (0x4600)
	maked $r0 = 3
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	fmulh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4600		# 6.0
	;;
	ccb.deq $r3, $r4 ? c4
	;;
	goto fail
	;;
c4:
	## case 4: ffmah 2*3+1=7 (0x4700)
	maked $r0 = 4
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	maked $r3 = 0x3c00		# 1.0 (accumulator)
	;;
	ffmah $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4700		# 7.0
	;;
	ccb.deq $r3, $r4 ? c5
	;;
	goto fail
	;;
c5:
	## case 5: ffmsh 1-2*3=-5 (0xc500)
	maked $r0 = 5
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	maked $r3 = 0x3c00		# 1.0 (accumulator)
	;;
	ffmsh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0xc500		# -5.0
	;;
	ccb.deq $r3, $r4 ? c6
	;;
	goto fail
	;;
c6:
	## case 6: fdivh 6/3=2 (0x4000)
	maked $r0 = 6
	;;
	maked $r1 = 0x4600		# 6.0 (rz)
	;;
	maked $r2 = 0x4200		# 3.0 (ry)
	;;
	fdivh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4000		# 2.0
	;;
	ccb.deq $r3, $r4 ? c7
	;;
	goto fail
	;;
c7:
	## case 7: fsqrth sqrt(4)=2 (0x4000)
	maked $r0 = 7
	;;
	maked $r1 = 0x4400		# 4.0
	;;
	fsqrth $r3 = $r1
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4000		# 2.0
	;;
	ccb.deq $r3, $r4 ? c8
	;;
	goto fail
	;;
c8:
	## case 8: frinth rint(2.75)=3 (0x4200)
	maked $r0 = 8
	;;
	maked $r1 = 0x4180		# 2.75
	;;
	frinth $r3 = $r1
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4200		# 3.0
	;;
	ccb.deq $r3, $r4 ? c9
	;;
	goto fail
	;;
c9:
	## case 9: fminh min(2,3)=2
	maked $r0 = 9
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	fminh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4000		# 2.0
	;;
	ccb.deq $r3, $r4 ? c10
	;;
	goto fail
	;;
c10:
	## case 10: fmaxh max(2,3)=3
	maked $r0 = 10
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	fmaxh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4200		# 3.0
	;;
	ccb.deq $r3, $r4 ? c11
	;;
	goto fail
	;;
c11:
	## case 11: fminnh minNum(2,3)=2
	maked $r0 = 11
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	fminnh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4000		# 2.0
	;;
	ccb.deq $r3, $r4 ? c12
	;;
	goto fail
	;;
c12:
	## case 12: fmaxnh maxNum(2,3)=3
	maked $r0 = 12
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	fmaxnh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4200		# 3.0
	;;
	ccb.deq $r3, $r4 ? c13
	;;
	goto fail
	;;
c13:
	## case 13: fminh(NaN,3) = canonical NaN (propagate)
	maked $r0 = 13
	;;
	maked $r1 = 0x7e00		# qNaN f16
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	fminh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x7e00		# canonical NaN
	;;
	ccb.deq $r3, $r4 ? c14
	;;
	goto fail
	;;
c14:
	## case 14: fmaxnh(NaN,3) = 3 (return number)
	maked $r0 = 14
	;;
	maked $r1 = 0x7e00		# qNaN f16
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	fmaxnh $r3 = $r1, $r2
	;;
	andd $r3 = $r3, $r5
	;;
	maked $r4 = 0x4200		# 3.0
	;;
	ccb.deq $r3, $r4 ? c15
	;;
	goto fail
	;;
c15:
	## case 15: fcomph.oeq(2,2)=1
	maked $r0 = 15
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4000		# 2.0
	;;
	fcomph.oeq $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c16
	;;
	goto fail
	;;
c16:
	## case 16: fcomph.olt(2,3)=1
	maked $r0 = 16
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	maked $r2 = 0x4200		# 3.0
	;;
	fcomph.olt $r3 = $r1, $r2
	;;
	maked $r4 = 1
	;;
	ccb.deq $r3, $r4 ? c17
	;;
	goto fail
	;;
c17:
	## case 17: fclassh classify(2.0) = +normal = bit 6 = 0x40
	maked $r0 = 17
	;;
	maked $r1 = 0x4000		# 2.0
	;;
	fclassh $r3 = $r1
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
