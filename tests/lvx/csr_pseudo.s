	.section .text
	.global _start
# Same fcsr round-trip as csr_fcsr.s, but written with the RISC-V FP CSR
# pseudo-instructions (fsflags/fsrm = csrrw of fflags/frm; frcsr = csrrs of
# fcsr). Proves the assembler aliases assemble and execute end to end.
# Expect exit code 0xff (255): fflags=0x1f | (frm=7 << 5)=0xe0.
_start:
	maked $r1 = 0x1f
	;;
	fsflags $r1            # fflags = 0x1f (== csrrw $r1 = 1); r1 = old (0)
	;;
	maked $r2 = 7
	;;
	fsrm $r2               # frm = 7 (== csrrw $r2 = 2); r2 = old (0)
	;;
	maked $r4 = 0
	;;
	frcsr $r4              # r4 = fcsr (== csrrs $r4 = 3) = 0x1f | (7<<5) = 0xff
	;;
	addd $r0 = $r4, 0
	;;
	scall 1                # exit(255)
	;;
1:	goto 1b
	;;
