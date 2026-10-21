	# Timing fixture: what each level of the memory hierarchy costs.
	#
	# The description gives two load latencies, and they describe two levels:
	# `ld' writes at CR, 3 cycles after RR, which is an L1 hit; `ld.u'
	# carries MemoryLevel=2 and writes at SR, 15 cycles, which is the next
	# level reached without touching L1.  Two numbers, two levels.
	#
	# Until 2026-10-21 the configuration had an L1 and then DRAM, so an L1
	# miss cost about 48 cycles where the architecture has something at 15,
	# and the ISS asserted the 15 out of the description rather than
	# producing it.  With an L2 the architectural number falls out of the
	# configuration instead, and this is what keeps the two in step: a
	# configuration whose second level drifts away from SR - RR is no longer
	# describing this machine.
	#
	# The loop walks a working set in 64-byte strides and wraps, so after the
	# first pass every access hits in exactly one level.  The load feeds the
	# next address, so no cost can be hidden behind independent work.
	#
	# --defsym SET=n   working-set bytes: 4096 fits the 32kB L1, 65536 misses
	#                  it and fits the 256kB L2, 1048576 misses both.
	.ifndef SET
	.equ SET, 65536
	.endif
	.equ TRIPS, 8000
	.section .bss
	.align 64
buf:	.space SET
	.section .text
	.global _start
_start:
	maked $r6 = buf
	;;
	copyd $r3 = $r6
	;;
	maked $r1 = TRIPS
	;;
.Lloop:
	ld $r2 = 0[$r3]
	;;
	addd $r7 = $r7, 64
	;;
	andd $r7 = $r7, SET - 1
	;;
	addd $r3 = $r6, $r7
	;;
	addd $r5 = $r5, $r2
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
