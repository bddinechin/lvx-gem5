	# Timing fixture: operands read later than RR shorten the dependence
	# into them.
	#
	# Most operands are read at RR, but a store reads its *stored value* and
	# the integer MAC its *accumulator* one stage later, at E1 (Opcode@timing:
	# `SW %3: Read.2', `MADDW %3: Read.2;Written.3').  So a producer feeding
	# one of those is a cycle closer than its latency suggests -- which is
	# what lvx-gcc states as a `define_bypass' per producer class, and what
	# the ISS ignored until 2026-10-17, over-stalling every such edge by one.
	#
	# Two shapes, each a pair measured against its own control, so neither
	# assertion needs a latency written down:
	#
	#   (plain)              mulw into itself.  Read.1, Written.3: a chain of
	#                        E2 - RR cycles a step.  The control.
	#   --defsym FDIV=1      fsqrtw into itself.  It writes at FD, a stage of
	#                        its own, and there must be a functional-unit
	#                        bucket for every distinct write stage: there was
	#                        none for FD until 2026-10-19, so the nine
	#                        FDIV*/FSQRT* opcodes fell into SF's and were
	#                        modelled at 11 instead of 13.  A latency with no
	#                        exact bucket is charged a lower one silently,
	#                        which is why this is asserted against the stage
	#                        enum rather than against a number.
	#   --defsym MAC=1       maddw into itself.  Read.2, Written.3: the
	#                        accumulator is read a stage later, so the chain
	#                        is ONE cycle a step -- half the control, which is
	#                        why this is the sharper of the two.
	#   --defsym FMUL=1              fmulw (Written.5) then an ALU consumer
	#                                reading at RR: E4 - RR cycles a step.
	#   --defsym FMUL=1 --defsym STORE=1
	#                                the same fmulw then a store of it: one
	#                                cycle less, the stored value being read
	#                                at E1.  `ffmaw feeding a store is three
	#                                cycles apart, not four.'
	.equ TRIPS, 2000
	.equ UNROLL, 10
	.section .data
	.align 8
buf:	.quad 0
	.section .text
	.global _start
_start:
	maked $r8 = buf
	;;
	maked $r3 = 3
	;;
	maked $r4 = 5
	;;
	maked $r6 = 1
	;;
	maked $r7 = 1
	;;
	maked $r1 = TRIPS
	;;
.Lloop:
	.rept UNROLL
.ifdef FDIV
	fsqrtw $r3 = $r3	# writes at FD; the chain is one FD latency a step
	;;
.else
.ifdef FMUL
	fmulw $r5 = $r6, $r7
	;;
.ifdef STORE
	sw 0[$r8] = $r5		# stored value: read at E1
.else
	addd $r9 = $r5, 1	# an ordinary consumer: read at RR
.endif
	;;
.else
.ifdef MAC
	maddw $r2 = $r3, $r4	# accumulator $r2: read at E1, written at E2
.else
	mulw $r2 = $r2, $r3	# control: read at RR, written at E2
.endif
	;;
.endif
.endif
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
