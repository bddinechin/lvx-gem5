	.global _start
_start:
	maked $r0 = 0x1000000bc
	;;
	scall 1
	;;
1:	goto 1b
	;;
