	## LVX $cs exception-flag test for the three FP families that used to drop
	## their exceptions (main returns 0 on success).
	##
	## The seeds, the compares and the widening conversions each computed an
	## IEEE result and threw the exceptions away, because the MDS helper
	## signature had no flag outputs to put them in -- the shim said so itself:
	## "recip7/rsqrte7's exceptions are discarded", "no flag is threaded
	## anyway".  They thread now, and nothing else in the suite would notice if
	## they stopped: every FP check beside this one compares computed values,
	## and the *value* these produce is the same either way.  A dropped flag is
	## invisible without a test that reads $cs.
	##
	## Flag positions are RISC-V's fflags positions, pinned by cs_flags.s:
	##
	##   bit 0 IN inexact    bit 1 UN underflow   bit 2 OV overflow
	##   bit 3 DZ divzero    bit 4 IO invalid     (bit 5 IC integer carry)
	##
	## Each case clears $cs, performs one operation, and requires $cs to equal
	## exactly the expected bits.  Half the cases expect *no* flag: fclass is
	## non-computational and raises nothing by IEEE 754, a quiet NaN compare
	## raises nothing, and widening is exact so a normal input raises nothing.
	## Those matter as much as the others -- they are what stops a future
	## change from raising flags that should not be raised.
	##
	## $r0 holds the case number, so the exit code is 0 on success or the first
	## failing case.  Expectations verified against Berkeley SoftFloat directly.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## ---- seeds: fsrec/fsrsr, RISC-V vfrec7/vfrsqrt7 ------------------

	## case 1: reciprocal seed of +0.0 is +inf -- DZ (bit 3) and nothing else.
	maked $r0 = 1
	;;
	maked $r10 = 0			## $cs := 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0			## +0.0
	;;
	fsrecd $r3 = $r1
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 8			## expect DZ only, bit 3
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## case 2: reciprocal-square-root seed of +0.0 is +inf -- DZ (bit 3).
	maked $r0 = 2
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0			## +0.0
	;;
	fsrsrd $r3 = $r1
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 8			## expect DZ only, bit 3
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## case 3: reciprocal-square-root seed of -1.0 is NaN -- IO (bit 4).
	maked $r0 = 3
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0xbff0000000000000	## -1.0
	;;
	fsrsrd $r3 = $r1
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 16			## expect IO only, bit 4
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## ---- compares: quiet on a quiet NaN, invalid on a signalling one -----

	## case 4: comparing a signalling NaN raises IO (bit 4).
	maked $r0 = 4
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0x7ff0000000000001	## signalling NaN
	;;
	maked $r2 = 0xbff0000000000000	## -1.0
	;;
	fcompd.oeq $r3 = $r1, $r2
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 16			## expect IO only, bit 4
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## case 5: comparing a QUIET NaN raises nothing.  LVX compares through
	## f*_lt_quiet/f*_eq, and quiet is the whole point of them.
	maked $r0 = 5
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0x7ff8000000000000	## quiet NaN
	;;
	maked $r2 = 0xbff0000000000000	## -1.0
	;;
	fcompd.oeq $r3 = $r1, $r2
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 0			## expect no flag at all
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## ---- widening: exact, so IO on a signalling NaN and nothing else -----

	## case 6: widening a signalling NaN raises IO (bit 4).
	maked $r0 = 6
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0x7f800001		## signalling NaN, binary 32
	;;
	fwidenwd $r3 = $r1
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 16			## expect IO only, bit 4
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## case 7: widening is EXACT -- every binary 32 is representable in binary
	## 64 -- so an ordinary value raises nothing, not even inexact.
	maked $r0 = 7
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0x3f800000		## 1.0, binary 32
	;;
	fwidenwd $r3 = $r1
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 0			## expect no flag at all
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## ---- classify: non-computational, raises nothing, not even on sNaN ---

	## case 8: fclass raises nothing on a signalling NaN.  This is the one FP
	## family deliberately left flag-free, and IEEE 754 is why: class is
	## non-computational.  Here so that "add the missing flags" never gets
	## applied to it.
	maked $r0 = 8
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0x7ff0000000000001	## signalling NaN
	;;
	fclassd $r3 = $r1
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 0			## expect no flag at all
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	maked $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
