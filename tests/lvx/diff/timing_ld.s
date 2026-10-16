	# Timing fixture: the load-to-first-use distance, and a taken branch in it.
	#
	# COPIES copies of
	#
	#	ld{.u} $r2 = 0[$r3]	# issues in cycle T
	#	[goto over a bundle]	# optional, and taken
	#	addd $r3 = $r3, $r2	# the FIRST USE of the loaded value
	#
	# chained through $r3: the loaded word is zero, so the pointer never moves
	# and every access hits the same line -- what is under test is the *write
	# stage of the loaded operand*, not the memory system.  The use is the next
	# bundle, so it cannot issue before the load has written $r2 and a copy
	# costs one load latency plus the use's own cycle: 1 + (CR - RR) = 4 for
	# `ld', 1 + (SR - RR) = 16 for `ld.u'.  The stall is the whole of the
	# difference, and it is the stall `ld.u' is supposed to have.
	#
	# Assembled three ways by check_timing.sh:
	#   (plain)                  `ld', the L1 hit
	#   --defsym UNCACHED=1      `ld.u', which bypasses the L1 and returns at SR
	#   --defsym UNCACHED=1 --defsym BRANCH=1
	#                            the same, with a taken branch between the load
	#                            and its first use
	#
	# The third is the one that says the taken-branch penalty is a *delay to
	# issue* and not a surcharge: the branch issues while the load is still in
	# flight, so its minTaken stall cycles elapse against the load's latency and
	# the copy costs the same 16 it cost without the branch.  Equivalently the
	# load looks (SR - RR) - minTaken cycles long to a use on the far side of a
	# taken branch.  Running this binary at a penalty of 0 and of minTaken
	# (LVX_TAKEN_BUBBLE) must give the same cycle count to the cycle.
	#
	# The `goto' jumps *over* a bundle rather than to the next one: a branch
	# whose target is its own fall-through does not redirect the PC and so is
	# not a taken branch at all.  The skipped bundle is never executed.
	.equ TRIPS, 50
	.equ UNROLL, 20
	.section .data
	.align 8
buf:	.quad 0
	.section .text
	.global _start
_start:
	maked $r3 = buf
	;;
	maked $r1 = TRIPS
	;;
.Lloop:
	.rept UNROLL
.ifdef UNCACHED
	ld.u $r2 = 0[$r3]
.else
	ld $r2 = 0[$r3]
.endif
	;;
.ifdef BRANCH
	goto 8f
	;;
	addd $r31 = $r31, 1	# jumped over, never executed
	;;
8:
.endif
	addd $r3 = $r3, $r2	# first use of the loaded value
	;;
	.endr
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
