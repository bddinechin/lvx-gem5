	## lvx-2 XVR data-move functional test (main returns 0 on success).
	##
	## The lvx_v2 vector register file (XVR/XBR/XCR) is wired into gem5's register
	## model (arch/lvx/regs/vec.hh + the shim). This exercises it end to end with
	## the only lvx_v2 instructions that touch it today -- the data moves -- and
	## checks the results against the known inputs, returning 0 iff every lane
	## round-tripped. Any mismatch (or an unmodeled register) yields a nonzero exit.
	##
	## Part A: xmovetd writes each of the four 64-bit lanes of XVR $a0 from a GPR
	##         (GPR -> XCR lane); xmovefo reads the whole 256-bit $a0 into a GPR
	##         quad (XVR -> 4 GPRs). Proves the overlay: 4 XCR cells == 1 XVR reg.
	## Part B: xputqo writes XVR $a1 from two GPR pairs (2x128 -> 256); xmovefo
	##         reads it back. Proves the write-whole-register path.
	## Part C: xmovefd reads lane 0 of $a0 back (XCR -> GPR), cross-checking A.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## --- Part A: per-lane write (xmovetd), whole-reg read (xmovefo) ---
	maked $r0 = 17
	;;
	maked $r1 = 34
	;;
	maked $r2 = 51
	;;
	maked $r3 = 68
	;;
	xmovetd $a0.x = $r0
	;;
	xmovetd $a0.y = $r1
	;;
	xmovetd $a0.z = $r2
	;;
	xmovetd $a0.t = $r3
	;;
	xmovefo $r4r5r6r7 = $a0
	;;
	sbfd $r4 = $r0, $r4
	;;
	sbfd $r5 = $r1, $r5
	;;
	sbfd $r6 = $r2, $r6
	;;
	sbfd $r7 = $r3, $r7
	;;
	## --- Part B: whole-reg write from two GPR pairs (xputqo), read back ---
	maked $r16 = 1000
	;;
	maked $r17 = 2000
	;;
	maked $r18 = 3000
	;;
	maked $r19 = 4000
	;;
	xputqo $a1 = $r16r17, $r18r19
	;;
	xmovefo $r20r21r22r23 = $a1
	;;
	sbfd $r20 = $r16, $r20
	;;
	sbfd $r21 = $r17, $r21
	;;
	sbfd $r22 = $r18, $r22
	;;
	sbfd $r23 = $r19, $r23
	;;
	## --- Part C: read lane 0 of $a0 (xmovefd), cross-check Part A ---
	xmovefd $r8 = $a0.x
	;;
	sbfd $r8 = $r0, $r8
	;;
	## --- fold every per-lane difference into $r0 (0 == all matched) ---
	iord $r4 = $r4, $r5
	;;
	iord $r6 = $r6, $r7
	;;
	iord $r20 = $r20, $r21
	;;
	iord $r22 = $r22, $r23
	;;
	iord $r4 = $r4, $r6
	;;
	iord $r20 = $r20, $r22
	;;
	iord $r4 = $r4, $r20
	;;
	iord $r0 = $r4, $r8
	;;
	ret
	;;
	.endp	main
