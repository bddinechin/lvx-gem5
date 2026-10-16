	# Timing fixture: a rotated branch loop with no dependence stalls.
	#
	# Each trip issues six independent `addd' (distinct destinations, and the
	# registers start at zero so nothing has to be set up), then the latch
	# decrement and the conditional branch -- eight bundles, every one of them
	# issuable the cycle after the last, since an ALU result has a latency of 1.
	#
	# So the loop's only cost beyond one cycle per bundle is the taken branch,
	# which makes the whole program 8 * TRIPS + TRIPS * minTaken cycles and the
	# penalty measurable by changing nothing but minTaken.  check_timing.sh runs
	# it twice, with the bubble at zero and at the architectural value.
	.equ TRIPS, 1000
	.section .text
	.global _start
_start:
	maked $r1 = TRIPS
	;;
.Lloop:
	addd $r20 = $r20, 1
	;;
	addd $r21 = $r21, 1
	;;
	addd $r22 = $r22, 1
	;;
	addd $r23 = $r23, 1
	;;
	addd $r24 = $r24, 1
	;;
	addd $r25 = $r25, 1
	;;
	addd $r1 = $r1, -1
	;;
	cb.dnez $r1 ? .Lloop
	;;
	maked $r0 = 0
	;;
	scall 1
	;;
1:	goto 1b
	;;
