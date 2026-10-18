	# Timing fixture: a rotated branch loop with no dependence stalls.
	#
	# Each trip issues six independent `addd' (distinct destinations, and the
	# registers start at zero so nothing has to be set up), then the latch
	# decrement and the conditional branch -- eight bundles, every one of them
	# issuable the cycle after the last, since an ALU result has a latency of 1.
	#
	# So the loop's only cost beyond one cycle per bundle is the taken branch,
	# and the penalty is measurable by changing nothing but minTaken.
	# check_timing.sh runs each variant twice, at a bubble of zero and at the
	# architectural value.
	#
	# Three shapes, because a branch costs what it costs according to *when it
	# settles the next PC* (Opcode@timing's `%0: Transfer.<c>'):
	#
	#   (plain)              a taken `cb' back edge.  It waits for its
	#                        comparison and transfers in RR, so minTaken + 1.
	#   --defsym UNCOND=1    a rotated loop: a `cb' to the exit that is never
	#                        taken, then a taken `goto' back edge.  `goto'
	#                        knows its target in ID, so minTaken exactly --
	#                        and the not-taken `cb' beside it costs nothing,
	#                        which this shape checks at the same time.
	#   --defsym NOTTAKEN=1  the plain loop plus one more `cb' that is never
	#                        taken: exactly one bundle dearer, no penalty.
	.equ TRIPS, 1000
	.section .text
	.global _start
_start:
	maked $r9 = 1		# so a cb.deqz $r9 is never taken
	;;
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
.ifdef NOTTAKEN
	cb.deqz $r9 ? .Lout	# never taken: one bundle, no penalty
	;;
.endif
	addd $r1 = $r1, -1
	;;
.ifdef UNCOND
	cb.deqz $r1 ? .Lout	# never taken (TRIPS is exhausted below)
	;;
	goto .Lloop		# unconditional: transfers in ID
	;;
.Lout:
.else
	cb.dnez $r1 ? .Lloop	# conditional: transfers in RR
	;;
.Lout:
.endif
	maked $r0 = 0
	;;
	scall 1
	;;
1:	goto 1b
	;;
