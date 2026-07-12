	.section .text
	.global _start
_start:
	make $r1 = 0x123456789abcULL   # >37 bits -> triple (2 IMMX)
	;;
	make $r2 = 100                 # simple make
	addw $r3 = $r2, $r2            # ALU, same bundle (2 instrs)
	;;
	addd $r0 = $r1, $r3            # r0 = 0x123456789abc + 200
	;;
	make $r0 = 7                   # overwrite: exit code 7
	;;
	scall 1
	;;
1:	goto 1b
	;;
