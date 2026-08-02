	.section .text
	.global _start
# RISC-V FP CSR round-trip over the CS (Compute Status) view.
# csrrw is an atomic self-swap (rd == rs1): rd receives the OLD csr, the csr
# receives rd's old value. We write fflags and frm independently, then read the
# combined fcsr, proving the three csrnumber arms and their bit layout:
#   fcsr[4:0] = fflags, fcsr[7:5] = frm.
# Expect exit code 0xff (255): fflags=0x1f | (frm=7 << 5)=0xe0.
_start:
	maked $r1 = 0x1f
	;;
	csrrw $r1 = 1          # fflags = 0x1f (r1); r1 = old fflags (0)
	;;
	maked $r2 = 7
	;;
	csrrw $r2 = 2          # frm = 7 (r2); r2 = old frm (0)  -- flags untouched
	;;
	maked $r4 = 0
	;;
	csrrs $r4 = 3          # fcsr |= 0 (unchanged); r4 = current fcsr
	;;
	addd $r0 = $r4, 0      # r0 = fcsr = 0x1f | (7<<5) = 0xff
	;;
	scall 1                # exit(255)
	;;
1:	goto 1b
	;;
