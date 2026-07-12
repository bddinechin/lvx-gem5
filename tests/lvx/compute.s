	.section .text
	.global _start
_start:
	make $r0 = 5
	;;
	sllw $r1 = $r0, 1      # r1 = 10
	;;
	addw $r0 = $r1, $r0    # r0 = 10 + 5 = 15
	;;
	addd $r0 = $r0, 4      # r0 = 19
	;;
	scall 1
	;;
1:	goto 1b
	;;
