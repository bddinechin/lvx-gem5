	## LVX atomic alignment: the ALIGNED accesses, which must go straight
	## through (main returns 0 on success).
	##
	## Companion to misalign_trap.s, which covers the access that traps.  This
	## is the half that stops the check from firing on everything: a test that
	## only looked for the trap would pass with every atomic rejected.
	##
	## One case per access size, at its natural alignment, using the
	## compare-and-swap forms -- they are the atomics the ISS shim actually
	## implements (MEM_atomic_cas; MEM_atomic_load/store are still panic stubs,
	## which is why ald/asd cannot appear here).  Each seeds memory with a plain
	## store, swaps a new value in against the seed as the expected value, and
	## requires the swap to report success.  With the bare mnemonic the result
	## is 1 when the swap happened.
	##
	## $r0 holds the case number, so the exit code is 0 on success or the first
	## failing case.
	.section .data
	.align 8
buf:	.quad 0
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: byte.  Any address is aligned for a byte access, and the
	## description writes no check at all for these -- `address & 0` is not a
	## check -- so the odd address here also pins that it is accepted.
	maked $r0 = 1
	;;
	maked $r2 = buf
	;;
	addd $r2 = $r2, 1		## deliberately odd
	;;
	maked $r6 = 0x5a
	;;
	sb 0[$r2] = $r6			## seed
	;;
	maked $r4 = 0xa5		## update
	;;
	maked $r5 = 0x5a		## expected
	;;
	acswapb $r3, [$r2] = $r4r5
	;;
	maked $r7 = 1
	;;
	ccb.dne $r3, $r7 ? fail
	;;

	## case 2: half word at a multiple of 2.
	maked $r0 = 2
	;;
	maked $r2 = buf
	;;
	addd $r2 = $r2, 2
	;;
	maked $r6 = 0x1234
	;;
	sh 0[$r2] = $r6
	;;
	maked $r4 = 0x4321
	;;
	maked $r5 = 0x1234
	;;
	acswaph $r3, [$r2] = $r4r5
	;;
	maked $r7 = 1
	;;
	ccb.dne $r3, $r7 ? fail
	;;

	## case 3: word at a multiple of 4.
	maked $r0 = 3
	;;
	maked $r2 = buf
	;;
	addd $r2 = $r2, 4
	;;
	maked $r6 = 0x12345678
	;;
	sw 0[$r2] = $r6
	;;
	maked $r4 = 0x87654321
	;;
	maked $r5 = 0x12345678
	;;
	acswapw $r3, [$r2] = $r4r5
	;;
	maked $r7 = 1
	;;
	ccb.dne $r3, $r7 ? fail
	;;

	## case 4: double word at a multiple of 8.
	maked $r0 = 4
	;;
	maked $r2 = buf
	;;
	maked $r6 = 0x0123456789abcdef
	;;
	sd 0[$r2] = $r6
	;;
	maked $r4 = 0xfedcba9876543210
	;;
	maked $r5 = 0x0123456789abcdef
	;;
	acswapd $r3, [$r2] = $r4r5
	;;
	maked $r7 = 1
	;;
	ccb.dne $r3, $r7 ? fail
	;;

	maked $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
