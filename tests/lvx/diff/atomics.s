	## LVX atomic read-modify-write: every operation the AL* family provides
	## (main returns 0 on success, or the number of the first failing case).
	##
	## Until 2026-09-11 the ISS implemented exactly one atomic, MEM_atomic_cas;
	## the other twelve helpers were lvx_behavior_unimpl() panic stubs, so the
	## whole AL*/AS* family aborted the simulation.  This is the test that they
	## work, and it checks BOTH halves of each: the previous value the
	## instruction returns into its operand register, and what is left in memory.
	## Checking only the return would miss a store of the wrong thing, and
	## checking only memory would miss the return, which is the half the AL*
	## forms exist for.
	##
	## The signed/unsigned pairs are the point of the byte cases.  MIN/MAX order
	## their operands as signed AT THE ACCESS WIDTH, so a byte 0xff is -1, not
	## 255 -- sign-extending from the access size is part of the operation, and a
	## shim that extends from 64 bits instead (or not at all) gets the same
	## answer for every non-negative input and the wrong one here.
	.section .data
	.align 8
buf:	.quad 0
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: aladdd -- returns the old value, memory holds old + operand.
	maked $r0 = 1
	;;
	maked $r2 = buf
	;;
	maked $r6 = 100
	;;
	sd 0[$r2] = $r6
	;;
	maked $r3 = 5
	;;
	aladdd [$r2] = $r3		## $r3 := previous (100), [buf] := 105
	;;
	maked $r7 = 100
	;;
	ccb.dne $r3, $r7 ? fail
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = 105
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 2: alandd and aliord, on a value where both bite.
	maked $r0 = 2
	;;
	maked $r6 = 0xff00
	;;
	sd 0[$r2] = $r6
	;;
	maked $r3 = 0x0ff0
	;;
	alandd [$r2] = $r3		## [buf] := 0x0f00
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = 0x0f00
	;;
	ccb.dne $r4, $r7 ? fail
	;;
	maked $r3 = 0x00ff
	;;
	aliord [$r2] = $r3		## [buf] := 0x0fff
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = 0x0fff
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 3: aleord.
	maked $r0 = 3
	;;
	maked $r6 = 0xf0f0
	;;
	sd 0[$r2] = $r6
	;;
	maked $r3 = 0xffff
	;;
	aleord [$r2] = $r3
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = 0x0f0f
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 4: almind is SIGNED -- min(-1, 1) is -1.
	maked $r0 = 4
	;;
	maked $r6 = -1
	;;
	sd 0[$r2] = $r6
	;;
	maked $r3 = 1
	;;
	almind [$r2] = $r3
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = -1
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 5: alminud on the same bits is UNSIGNED -- min(2^64-1, 1) is 1.
	maked $r0 = 5
	;;
	maked $r6 = -1
	;;
	sd 0[$r2] = $r6
	;;
	maked $r3 = 1
	;;
	alminud [$r2] = $r3
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = 1
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 6: almaxb at BYTE width -- 0xff is -1 there, so max(-1, 1) is 1.
	## This is the case that catches sign-extension from the wrong width.
	maked $r0 = 6
	;;
	maked $r6 = 0xff
	;;
	sb 0[$r2] = $r6
	;;
	maked $r3 = 1
	;;
	almaxb [$r2] = $r3
	;;
	lbz $r4 = 0[$r2]
	;;
	maked $r7 = 1
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 7: almaxub on the same byte is unsigned -- max(255, 1) is 255.
	maked $r0 = 7
	;;
	maked $r6 = 0xff
	;;
	sb 0[$r2] = $r6
	;;
	maked $r3 = 1
	;;
	almaxub [$r2] = $r3
	;;
	lbz $r4 = 0[$r2]
	;;
	maked $r7 = 0xff
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 8: aldusd decrements, and saturates at zero instead of wrapping.
	maked $r0 = 8
	;;
	maked $r6 = 10
	;;
	sd 0[$r2] = $r6
	;;
	maked $r3 = 3
	;;
	aldusd [$r2] = $r3		## 10 - 3
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = 7
	;;
	ccb.dne $r4, $r7 ? fail
	;;
	maked $r3 = 99			## more than is there: saturate, do not wrap
	;;
	aldusd [$r2] = $r3
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = 0
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 9: aswapd returns the old value and stores the new one.
	maked $r0 = 9
	;;
	maked $r6 = 0x1234
	;;
	sd 0[$r2] = $r6
	;;
	maked $r3 = 0x5678
	;;
	aswapd [$r2] = $r3		## $r3 := 0x1234, [buf] := 0x5678
	;;
	maked $r7 = 0x1234
	;;
	ccb.dne $r3, $r7 ? fail
	;;
	ld $r4 = 0[$r2]
	;;
	maked $r7 = 0x5678
	;;
	ccb.dne $r4, $r7 ? fail
	;;

	## case 10: the plain atomic load and store round-trip.
	maked $r0 = 10
	;;
	maked $r1 = 0x0123456789abcdef
	;;
	asd [$r2] = $r1
	;;
	ald $r4 = [$r2]
	;;
	ccb.dne $r4, $r1 ? fail
	;;

	maked $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
