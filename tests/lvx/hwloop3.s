	.section .text
	.global _start
# Hardware loop, non-zero trip count. LC = 3, body adds 1 to r0 each pass.
# Expect exit code 3 (loop body runs exactly 3 times).
_start:
	maked $r0 = 0
	;;
	maked $r1 = 3          # trip count
	;;
	loopdo $r1, .Lend     # LS = next bundle, LE = .Lend, LC = 3
	;;
.Lstart:
	addd $r0 = $r0, 1      # loop body (one bundle); back-edge fires at .Lend
	;;
.Lend:
	scall 1                # exit(r0)
	;;
1:	goto 1b
	;;
