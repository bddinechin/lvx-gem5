	.section .text
	.global _start
# Read-only machine ID CSRs: mvendorid(0xF11), mhartid(0xF14), misa(0x301).
# All read 0, writes ignored, and any of csrrw/csrrs/csrrc may read them.
# Each reg is poisoned with 0xFF first; a working read must overwrite it with 0.
# (If a csrnumber failed to dispatch it would THROW -> the ISS would abort.)
# Expect exit code 0.
_start:
	maked $r0 = 0xFF
	;;
	csrrw $r0 = 0xF11      # mvendorid: r0 <- 0 (write of 0xFF ignored)
	;;
	maked $r1 = 0xFF
	;;
	csrrs $r1 = 0xF14      # mhartid:   r1 <- 0
	;;
	maked $r2 = 0xFF
	;;
	csrrc $r2 = 0x301      # misa:      r2 <- 0
	;;
	addd $r0 = $r0, $r1
	;;
	addd $r0 = $r0, $r2    # r0 = 0 iff all three read 0
	;;
	scall 1                # exit(0)
	;;
1:	goto 1b
	;;
