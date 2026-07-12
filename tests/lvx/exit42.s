	.section .text
	.global _start
_start:
	make $r0 = 42
	;;
	scall 1
	;;
1:	goto 1b
	;;
