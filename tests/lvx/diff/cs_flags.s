	## LVX $cs IEEE 754 exception-flag layout test (main returns 0 on success).
	##
	## The FP checks beside this one compare computed values, which are blind to
	## WHERE a raised flag lands in $cs -- every bit of the layout could be
	## permuted and they would all still pass.  This pins the positions, which
	## are RISC-V's fflags positions:
	##
	##   bit 0 IN inexact    bit 1 UN underflow   bit 2 OV overflow
	##   bit 3 DZ divzero    bit 4 IO invalid     (bit 5 IC integer carry)
	##
	## Each case clears $cs, performs one operation that raises exactly one
	## flag, and requires $cs to equal exactly that bit -- so a flag landing in
	## the wrong place fails whether it moved into another flag's bit or into
	## the carry.  $r0 holds the case number, so the exit code is 0 on success
	## or the first failing case.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: 1.0 / 0.0 raises DZ (bit 3) and nothing else.
	maked $r0 = 1
	;;
	maked $r10 = 0			## $cs := 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0x3ff0000000000000	## 1.0
	;;
	maked $r2 = 0			## 0.0
	;;
	fdivd $r3 = $r1, $r2
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 8			## expect DZ only, bit 3
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## case 2: 1.0 / 3.0 is inexact -- IN (bit 0) and nothing else.
	maked $r0 = 2
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r2 = 0x4008000000000000	## 3.0
	;;
	fdivd $r3 = $r1, $r2
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 1			## expect IN only, bit 0
	;;
	ccb.dne $r4, $r5 ? fail
	;;

	## case 3: 0.0 / 0.0 is invalid -- IO (bit 4) and nothing else.
	maked $r0 = 3
	;;
	maked $r10 = 0
	;;
	set $cs = $r10
	;;
	maked $r1 = 0
	;;
	maked $r2 = 0
	;;
	fdivd $r3 = $r1, $r2
	;;
	get $r4 = $cs
	;;
	andd $r4 = $r4, 0x3f
	;;
	maked $r5 = 16			## expect IO only, bit 4
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
