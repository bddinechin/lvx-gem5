	# Timing/plumbing fixture: the data cache must actually see the program's
	# accesses.
	#
	# Until 2026-10-20 it saw nothing at all.  The shim served every load and
	# store functionally inside the behaviour, through a port proxy, so no
	# request reached the hierarchy and every D-cache counter was zero
	# whatever the program did -- 20,000 stores included.  Nothing said so;
	# a counter that reads zero looks like a program that made no accesses.
	#
	# A loop of TRIPS iterations, each with one load and one store to the
	# same word, so the loop makes 2*TRIPS accesses and they all hit after
	# the first.  --defsym DOUBLE=1 runs twice as many: check_dcache.sh
	# asserts on the *difference* between the two, which cancels whatever
	# the startup code and the exit path do.
	.ifdef DOUBLE
	.equ TRIPS, 1000
	.else
	.equ TRIPS, 500
	.endif
	.section .data
	.align 8
buf:	.quad 7
	.section .text
	.global _start
_start:
	maked $r3 = buf
	;;
	maked $r1 = TRIPS
	;;
.Lloop:
	ld $r2 = 0[$r3]
	;;
	addd $r2 = $r2, 1
	;;
	sd 0[$r3] = $r2
	;;
	addd $r1 = $r1, -1
	;;
	cb.dnez $r1 ? .Lloop
	;;
	# the loaded value must have been incremented TRIPS times
	ld $r2 = 0[$r3]
	;;
	compd.ne $r0 = $r2, TRIPS + 7
	;;
	scall 1
	;;
1:	goto 1b
	;;
