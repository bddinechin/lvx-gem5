	# Freestanding crt for the LVX SE-mode differential tests.
	#
	# gem5's LvxProcess::initState already puts $r12 (SP) at the stack top with a
	# page mapped, so there is nothing to set up here: call main, then exit via the
	# kv4-v1 syscall ABI (number in the scall operand, args in r0.., return in r0).
	# main's int result is already in $r0 on return, and syscall 1 is exit, whose
	# code argument is r0 -- so the process exit code IS main's return value (& 255).
	.section .text
	.global _start
_start:
	call main
	;;
	scall 1
	;;
1:	goto 1b
	;;
