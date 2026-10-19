	# Timing fixture: bundles of differing lengths must not cost anything
	# extra.
	#
	# A bundle's length is known only once it is decoded, so the PC state's
	# npc is computed from the *previous* bundle's.  Until 2026-10-19 the
	# LVX decoder never corrected it, and the consequence was not a wrong
	# fetch address but a squash: Fetch2 copies that PC state into the
	# instruction, Execute compares it against the PC the bundle actually
	# produced, and a disagreement is a taken branch.  So every change of
	# bundle length flushed the pipeline -- this loop measured 147.30 cycles
	# a trip against 24.27, with 20005 instruction streams over 1000 trips.
	#
	# Ten copies of a two-instruction bundle followed by a one-instruction
	# bundle, so the length alternates on every bundle boundary.  The chain
	# is copyd -> addd -> copyd at one cycle each, so a copy costs two
	# cycles and a trip costs 10*2 plus the latch and the taken branch.
	# Every other timing fixture here is uniform-length, which is exactly
	# why none of them caught this.
	.equ TRIPS, 1000
	.equ UNROLL, 10
	.section .data
	.align 8
buf:	.quad 0
	.section .text
	.global _start
_start:
	maked $r7 = buf
	;;
	maked $r5 = 0
	;;
	maked $r1 = TRIPS
	;;
.Lloop:
	.rept UNROLL
	copyd $r0 = $r5 ; ld $r6 = 0[$r7]	# two syllables
	;;
	addd $r5 = $r0, 1			# one syllable
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
