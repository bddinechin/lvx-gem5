	## LVX atomic alignment: the access that traps.
	##
	## Companion to misalign.s, which covers the aligned accesses.  The atomics
	## require their effective address to be a multiple of the access size, and
	## the description says so now -- MDS emits the check and THROWs MISALIGN,
	## the trap the architecture names HTO_DMIS ("Data MISalign access").  SE
	## mode has no ring to divert it to, so the ISS panics rather than
	## performing the access.  The test is that it does: reaching the exit at
	## all is the failure, and before the check existed that is exactly what
	## happened -- the port proxy does not care about alignment.
	.section .data
	.align 8
buf:	.quad 0
	.section .text
	.align 8
	.proc	main
	.global	main
main:
	maked $r2 = buf
	;;
	addd $r2 = $r2, 1		## one past an 8-aligned address
	;;
	maked $r1 = 0x0123456789abcdef
	;;
	asd [$r2] = $r1			## 8-byte atomic on an odd address -- must not return
	;;
	maked $r0 = 1
	;;
	ret
	;;
	.endp	main
