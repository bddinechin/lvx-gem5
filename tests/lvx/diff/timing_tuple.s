	# Timing fixture: a wide load's first use is ONE register of the tuple.
	#
	# A 128-bit value lives in an aligned GPR pair and a 256-bit one in a quad
	# (Storage.yml) -- there is no wide register file -- so `lq $r2r3' writes
	# two general registers and `lo $r4r5r6r7' writes four, and a later scalar
	# read of any ONE of them depends on the load.  The ISS has to see that:
	# the dependency table names the tuple operand, and the port expands it
	# into its member GPRs.  Until 2026-10-16 PGR and QGR were not modelled at
	# all, so these loads had no destination register and a scalar read of a
	# lane waited for nothing.
	#
	# Six load/use pairs a trip -- both lanes of a pair and all four of a quad
	# -- each chained into the base pointer, so every one of them is a real
	# dependence and a copy costs one load latency plus the use's own cycle.
	# One wrong lane in the expansion shows up as a sixth of the cost missing.
	# The loaded words are zero, so the pointer never moves.
	#
	# Assembled twice: plain, and with --defsym UNCACHED=1 for the `.u' forms.
	.equ TRIPS, 50
	.equ UNROLL, 10
.ifdef UNCACHED
	.macro LQW dst
	lq.u \dst = 0[$r0]
	.endm
	.macro LOW dst
	lo.u \dst = 0[$r0]
	.endm
.else
	.macro LQW dst
	lq \dst = 0[$r0]
	.endm
	.macro LOW dst
	lo \dst = 0[$r0]
	.endm
.endif
	.section .data
	.align 32
buf:	.quad 0
	.quad 0
	.quad 0
	.quad 0
	.section .text
	.global _start
_start:
	maked $r0 = buf
	;;
	maked $r1 = TRIPS
	;;
.Lloop:
	.rept UNROLL
	LQW $r2r3
	;;
	addd $r0 = $r0, $r2	# low half of the pair
	;;
	LQW $r2r3
	;;
	addd $r0 = $r0, $r3	# high half of the pair
	;;
	LOW $r4r5r6r7
	;;
	addd $r0 = $r0, $r4	# quad lane 0
	;;
	LOW $r4r5r6r7
	;;
	addd $r0 = $r0, $r5	# quad lane 1
	;;
	LOW $r4r5r6r7
	;;
	addd $r0 = $r0, $r6	# quad lane 2
	;;
	LOW $r4r5r6r7
	;;
	addd $r0 = $r0, $r7	# quad lane 3
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
