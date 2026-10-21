	# Timing fixture: what a hardware loop's trip costs, as a function of its
	# body's length and of where the body sits.
	#
	# Neither should matter.  A trip is its bundles, one per cycle, and
	# LOOPDO's back edge is the fetch engine's -- no instruction decides
	# anything and nothing waits.  The architecture goes further than that:
	# LOOPDO's own behaviour invalidates a *prefetch buffer*
	# ((EFFECT.ID.invalpfb) in Instruction.yml), so the body is replayed from
	# it and the back edge costs no fetch at all.
	#
	# Measured on 2026-10-21, before that buffer was modelled, it mattered
	# twice over:
	#
	#   * a body of 2, 3 or 4 bundles all cost 4.09 cycles a trip; 5 or 6
	#     cost 6.12; only from 7 up did a trip cost its bundles.  That floor
	#     is the pipeline between Fetch2 deciding the redirect and Execute
	#     issuing the target -- three forward delays and the signal back.
	#   * the same four-bundle body cost 4.07 inside one 64-byte line and
	#     6.09 across two, because each trip re-fetched it.
	#
	# Both are the ISS re-fetching a body the machine replays, and together
	# they cost lvx-mlir's software-pipelined VEC=4 `mymma' its whole gain:
	# 6.03 cycles a trip against the 4 its scheduler had planned, which is
	# what this fixture exists to stop happening again.
	#
	# The body is BUNDLES independent `addd', so there is no dependence to
	# absorb a fetch cost and any of it shows up whole.  PAD places the body:
	# the loop is reached by a `goto' over .balign/.space, so the body starts
	# at a known offset in a 64-byte line and a long enough body crosses into
	# the next one.
	#
	# --defsym BUNDLES=n   bundles in the body (default 4)
	# --defsym PAD=n       bytes of padding before the loopdo (default 0, so
	#                      the body starts 4 bytes into a line)
	.ifndef BUNDLES
	.equ BUNDLES, 4
	.endif
	.ifndef PAD
	.equ PAD, 0
	.endif
	.equ TRIPS, 2000
	.section .text
	.global _start
_start:
	maked $r1 = TRIPS
	;;
	goto Lloop
	;;
	.balign 64
	.if PAD > 0
	.space PAD
	.endif
Lloop:
	loopdo $r1, Lend
	;;
	.global Lstart
Lstart:
	.set n, 0
	.irp r, 20,21,22,23,24,25,26,27,28,29
	.if n < BUNDLES
	addd $r\r = $r\r, 1
	;;
	.endif
	.set n, n + 1
	.endr
	.global Lend
Lend:
	maked $r0 = 0
	;;
	scall 1
	;;
1:	goto 1b
	;;
