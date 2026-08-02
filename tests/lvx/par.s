	.global _start
_start:
	maked $r1 = 88 ; maked $r2 = 100
	;;
	addd $r0 = $r1, $r2
	;;
	scall 1
	;;
1:	goto 1b
	;;
