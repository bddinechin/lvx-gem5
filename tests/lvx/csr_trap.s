	.section .text
	.global _start
# Machine trap CSRs as 64-bit RW storage: mepc(0x341)<->SPC, mtval(0x343)<->EA,
# mtvec(0x305)<->EV, mscratch(0x340)<->SR. csrrw swaps (rd=old csr, csr=rd).
# Write four distinct values, read two back, and sum -- proves independent
# read/write backing (not aliased) and the swap semantics.
# Expect exit code 0x99 = 153 (mepc 0x11 + mscratch 0x88).
_start:
	maked $r1 = 0x11
	;;
	csrrw $r1 = 0x341      # mepc     = 0x11
	;;
	maked $r2 = 0x22
	;;
	csrrw $r2 = 0x343      # mtval    = 0x22
	;;
	maked $r3 = 0x44
	;;
	csrrw $r3 = 0x305      # mtvec    = 0x44
	;;
	maked $r4 = 0x88
	;;
	csrrw $r4 = 0x340      # mscratch = 0x88
	;;
	maked $r5 = 0
	;;
	csrrs $r5 = 0x341      # read mepc     -> 0x11
	;;
	maked $r6 = 0
	;;
	csrrs $r6 = 0x340      # read mscratch -> 0x88
	;;
	addd $r0 = $r5, $r6    # 0x11 + 0x88 = 0x99
	;;
	scall 1                # exit(153)
	;;
1:	goto 1b
	;;
