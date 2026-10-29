	# Branch priority within one bundle: BCU0 wins over BCU1.
	#
	# A bundle has two BCU slots (BCU availability is 2), so two branches
	# in one bundle is a legal bundle and both may be taken.  The
	# architecture resolves it by slot and not by anything else: the
	# first slot that transfers is the one that does, conditional or not.
	# The two slots are the first two syllables of the bundle, so "first"
	# is the one written first in the source.
	#
	# The ISS scanned every syllable for a written NPC and kept assigning,
	# with no break -- so the *last* writer won, which is BCU1.  A bundle
	# with two taken branches went to the wrong target.  A single taken
	# branch in either slot was already right, which is why nothing else
	# caught this: it needs both slots taken at once, and no compiled code
	# emits that today (GCC's bundler puts at most one transfer per
	# bundle).  No reason for the ISS to disagree with the architecture.
	#
	# Each case lands somewhere different for each possible wrong answer,
	# and the exit code says which.  Exit 0 = all pass.
	.section .text
	.global _start
_start:
	maked $r1 = 0			# deqz $r1 is TAKEN
	;;
	maked $r2 = 1			# deqz $r2 is NOT taken
	;;

	# ---- 1. BCU0 taken, BCU1 not taken: BCU0's target ----------------
	cb.deqz $r1? .A0 ; cb.deqz $r2? .Abad
	;;
	maked $r0 = 1			# fell through: neither taken
	;;
	scall 1
	;;
.Abad:	maked $r0 = 2			# the not-taken BCU1 branch transferred
	;;
	scall 1
	;;

	# ---- 2. BCU0 not taken, BCU1 taken: BCU1's target ----------------
	# BCU0 falling through must NOT suppress BCU1 -- the priority is over
	# the slots that transfer, not over the slots that exist.
.A0:	cb.deqz $r2? .Bbad ; cb.deqz $r1? .B1
	;;
	maked $r0 = 3			# fell through: BCU1's branch was lost
	;;
	scall 1
	;;
.Bbad:	maked $r0 = 4			# the not-taken BCU0 branch transferred
	;;
	scall 1
	;;

	# ---- 3. both taken: BCU0 wins ------------------------------------
.B1:	cb.deqz $r1? .C0 ; cb.deqz $r1? .Cbad
	;;
	maked $r0 = 5			# fell through
	;;
	scall 1
	;;
.Cbad:	maked $r0 = 6			# BCU1 won: the bug this test exists for
	;;
	scall 1
	;;

	# ---- 4. both taken, unconditional in BCU0 ------------------------
	# "conditional or not": a taken conditional branch in BCU1 does not
	# beat an unconditional transfer in BCU0.
.C0:	goto .D0 ; cb.deqz $r1? .Dbad
	;;
	maked $r0 = 7			# fell through
	;;
	scall 1
	;;
.Dbad:	maked $r0 = 8			# BCU1's conditional beat BCU0's goto
	;;
	scall 1
	;;

	# ---- 5. both taken, unconditional in BCU1 ------------------------
	# The mirror: BCU0's conditional wins over an unconditional BCU1.
.D0:	cb.deqz $r1? .E0 ; goto .Ebad
	;;
	maked $r0 = 9			# fell through
	;;
	scall 1
	;;
.Ebad:	maked $r0 = 10			# BCU1's goto beat BCU0's taken branch
	;;
	scall 1
	;;

	# ---- all five correct --------------------------------------------
.E0:	maked $r0 = 0
	;;
	scall 1
	;;
1:	goto 1b
	;;
