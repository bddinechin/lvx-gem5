	## LVX CCB fused compare-and-branch functional test.
	##
	## Exercises the ISS `ccbcomp` helper (Behavior_ccbcomp) across the two axes
	## the 4-bit ccbcomp code splits on:
	##   - code & 7  selects the relation  (LT GE LTU GEU EQ NE ANY NONE), signed
	##               vs unsigned per relation;
	##   - code & 8  selects the width     (0-7 = D/64-bit, 8-15 = W/32-bit).
	## Each case is built so that ONLY the correct branch decision reaches the next
	## case; a wrong decision falls into `fail`.  $r0 holds the number of the case
	## in progress, so the process exit code is 0 on full success or the index of
	## the first failing case (1..10) otherwise.  Cases 3/4/5 pin the signed vs
	## unsigned split; 6/7/8 pin the 32- vs 64-bit width (high bits set so a D-vs-W
	## confusion flips the result).
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	## case 1: .deq taken  (5 == 5)
	make $r0 = 1
	;;
	make $r1 = 5
	;;
	make $r2 = 5
	;;
	ccb.deq $r1, $r2 ? c1
	;;
	goto fail
	;;
c1:
	## case 2: .dne taken  (5 != 6)
	make $r0 = 2
	;;
	make $r1 = 5
	;;
	make $r2 = 6
	;;
	ccb.dne $r1, $r2 ? c2
	;;
	goto fail
	;;
c2:
	## case 3: .dlt SIGNED taken  (-1 < 1).  Unsigned view (0xffff..f < 1) is false,
	## so this only branches if the relation is treated as signed.
	make $r0 = 3
	;;
	make $r1 = -1
	;;
	make $r2 = 1
	;;
	ccb.dlt $r1, $r2 ? c3
	;;
	goto fail
	;;
c3:
	## case 4: .dltu NOT taken  (0xffff..f <u 1 is false) -- same regs as case 3.
	make $r0 = 4
	;;
	ccb.dltu $r1, $r2 ? fail
	;;
	## case 5: .dgeu taken  (0xffff..f >=u 1)
	make $r0 = 5
	;;
	ccb.dgeu $r1, $r2 ? c5
	;;
	goto fail
	;;
c5:
	## case 6: .weq taken ignoring the high 32 bits (low32 == 5 for both operands).
	make $r0 = 6
	;;
	make $r1 = 0x100000005
	;;
	make $r2 = 0x200000005
	;;
	ccb.weq $r1, $r2 ? c6
	;;
	goto fail
	;;
c6:
	## case 7: .wlt SIGNED-32 taken  (low32: 0xffffffff = -1  <  1).
	## As a 64-bit compare (0x1ffffffff vs 0x100000001) this is false, so it only
	## branches if the width is 32-bit.
	make $r0 = 7
	;;
	make $r1 = 0x1ffffffff
	;;
	make $r2 = 0x100000001
	;;
	ccb.wlt $r1, $r2 ? c7
	;;
	goto fail
	;;
c7:
	## case 8: .dlt on the SAME regs is NOT taken (64-bit: 0x1ffffffff > 0x100000001).
	make $r0 = 8
	;;
	ccb.dlt $r1, $r2 ? fail
	;;
	## case 9: .dany taken  (0b1010 & 0b0010 = 0b0010 != 0)
	make $r0 = 9
	;;
	make $r1 = 10
	;;
	make $r2 = 2
	;;
	ccb.dany $r1, $r2 ? c9
	;;
	goto fail
	;;
c9:
	## case 10: .dnone taken  (0b1000 & 0b0001 = 0)
	make $r0 = 10
	;;
	make $r1 = 8
	;;
	make $r2 = 1
	;;
	ccb.dnone $r1, $r2 ? c10
	;;
	goto fail
	;;
c10:
	## all cases behaved correctly
	make $r0 = 0
	;;
	ret
	;;
fail:
	ret
	;;
	.endp	main
