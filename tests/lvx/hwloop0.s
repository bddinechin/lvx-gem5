	.section .text
	.global _start
# Hardware loop, ZERO trip count -- the LOOPNEZ case. LC = 0 must SKIP the loop
# body entirely (LVX/Tensilica), where KVX LOOPDO would loop forever.
# Expect exit code 0 (body never runs).
_start:
	maked $r0 = 0
	;;
	maked $r1 = 0          # trip count 0
	;;
	loopdo $r1, .Lend     # count == 0 -> skip: next PC = .Lend
	;;
.Lstart:
	addd $r0 = $r0, 1      # loop body -- must NOT execute
	;;
.Lend:
	scall 1                # exit(r0) == exit(0)
	;;
1:	goto 1b
	;;
