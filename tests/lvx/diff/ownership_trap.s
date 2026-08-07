	## LVX system-register ownership: the refusal that is a trap (US11995218).
	##
	## Companion to ownership.s, which covers the refusals that are not traps.
	## $men's werror is TRAP_PRIVILEGE and its owner MO.MEN resets to PL0, so a
	## write from PL1 is a privilege trap -- and SE mode has no ring to divert it
	## to, so the ISS panics rather than letting the write through.  The test is
	## that it does: reaching the exit at all is the failure.
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	maked $r3 = 1
	;;
	set $ps = $r3		## drop to PL1
	;;
	maked $r1 = 0xffff
	;;
	set $men = $r1		## PL0-owned, werror TRAP_PRIVILEGE -- must not return
	;;
	maked $r0 = 1
	;;
	ret
	;;
	.endp	main
