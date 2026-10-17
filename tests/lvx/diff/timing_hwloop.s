	# Timing fixture: a hardware loop's back edge costs nothing, even when
	# the bundle it falls off holds a BCU instruction.
	#
	# LOOPDO's back edge is the fetch engine's: LE is reached and fetch
	# continues at LS, no instruction having decided anything and nothing
	# having waited.  So it must be charged no taken-branch penalty -- and
	# must not run down a latency in flight either, since no cycles pass.
	#
	# The trap this pins is that the bundle LE falls off can perfectly well
	# *be* a control instruction: here it holds a `cb' whose condition is
	# false, so the branch is not taken and the loop engine takes the back
	# edge instead.  "Is the bundle a control instruction" therefore does not
	# answer the question, and while that was the whole test the ISS charged
	# minTaken on this loop 999 times a run -- two cycles a trip the hardware
	# does not spend.  It is the PC state that knows (freeRedirect).
	#
	# The body is six independent `addd' plus that `cb': seven bundles with no
	# dependence between them, so a trip costs seven cycles and any penalty
	# charged shows up whole rather than being absorbed by a stall.  Run at a
	# penalty of 0 and of minTaken, the cycle counts must be equal.
	.equ TRIPS, 1000
	.section .text
	.global _start
_start:
	maked $r9 = 1		# so cb.deqz $r9 is never taken
	;;
	maked $r1 = TRIPS
	;;
	loopdo $r1, .Lend
	;;
.Lstart:
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
	cb.deqz $r9 ? .Lout	# BCU, never taken; LE falls off this bundle
	;;
.Lend:
	maked $r0 = 0
	;;
	scall 1
	;;
.Lout:
	maked $r0 = 9		# reached only if the loop went wrong
	;;
	scall 1
	;;
1:	goto 1b
	;;
