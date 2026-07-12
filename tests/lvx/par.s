	.global _start
_start:
	make $r1 = 88 ; make $r2 = 100
	;;
	addd $r0 = $r1, $r2
	;;
	scall 1
	;;
1:	goto 1b
	;;
