	# The Dismissible load variants: `.s' and `.us' cannot fault, and a
	# fault becomes all-zero loaded bits.
	#
	# `Modifier@properties' gives `%0.US: MemoryLevel=2;Dismissible;
	# LoadedAt=SR' -- one property with three halves. The ISS had LoadedAt
	# (the functional unit's latency) and, since 2026-10-29, MemoryLevel
	# (no allocation in the first level). Dismissible is what makes the
	# variant useful to a compiler: the load does not fault, so it may be
	# hoisted above the test that guards it, and the value is only consumed
	# when the guard passes.
	#
	# Finding this needed a fault to convert, and there were none: the port
	# returned NoFault from initiateAcc unconditionally, which is Minor's
	# signal that the instruction *suppressed* a translation fault
	# deliberately. So every load from an unmapped address silently left its
	# destination untouched and the program ran on -- `ld' and `ld.s' were
	# indistinguishable, both wrong. Checks 1 and 2 below read 0x1234, the
	# value the register held beforehand, if that ever comes back.
	#
	# --defsym FAULT=1   use a plain `ld' on the bad address instead, which
	#                    MUST fault: that is the half that makes Dismissible
	#                    mean something, and check_dismiss.sh asserts the
	#                    run dies rather than exiting.
	.equ BAD, 0x40000000000		# no SE-mode mapping, and fixupFault
					# declines it -- verified, not assumed
	.section .data
	.align 8
cell:	.quad 0x5a5a5a5a
	.section .text
	.global _start
_start:
	maked $r3 = BAD
	;;
	maked $r4 = cell
	;;
.ifdef FAULT
	# ---- the discrimination: a non-Dismissible load still faults ------
	ld $r5 = 0[$r3]
	;;
	maked $r0 = 7			# reached only if the fault vanished
	;;
	scall 1
	;;
9:	goto 9b
	;;
.else
	# ---- 1. `.s' from an unmapped address loads zero ------------------
	maked $r5 = 0x1234
	;;
	ld.s $r5 = 0[$r3]
	;;
	cb.dnez $r5 ? .Lbad1
	;;

	# ---- 2. and so does `.us' -----------------------------------------
	maked $r5 = 0x1234
	;;
	ld.us $r5 = 0[$r3]
	;;
	cb.dnez $r5 ? .Lbad2
	;;

	# ---- 3. a mapped address is unaffected: `.s' reads what `ld' does --
	#         Dismissible converts the failure and nothing else, so the
	#         two must agree wherever an ordinary load succeeds.
	ld $r6 = 0[$r4]
	;;
	ld.s $r7 = 0[$r4]
	;;
	compd.ne $r8 = $r6, $r7
	;;
	cb.dnez $r8 ? .Lbad3
	;;

	# ---- 4. `.us' likewise, which is also MemoryLevel=2 ---------------
	ld.us $r7 = 0[$r4]
	;;
	compd.ne $r8 = $r6, $r7
	;;
	cb.dnez $r8 ? .Lbad4
	;;

	# ---- 5. and that value is the stored one, not a zero both agree on
	cb.deqz $r6 ? .Lbad5
	;;

	maked $r0 = 0
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad1:	maked $r0 = 1			# .s from an unmapped address: not zero
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad2:	maked $r0 = 2			# .us from an unmapped address: not zero
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad3:	maked $r0 = 3			# .s disagreed with ld on a mapped address
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad4:	maked $r0 = 4			# .us disagreed with ld on a mapped address
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad5:	maked $r0 = 5			# the mapped read was zero, so 3 and 4
	;;				# would have passed on a dismissed load
	scall 1
	;;
9:	goto 9b
	;;
.endif
