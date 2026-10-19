	# VLIW parallel semantics within one bundle.
	#
	# Every instruction of a bundle reads its sources at the same cycle and
	# writes its result afterwards, so a bundle is not a short sequence: a
	# reader in it sees the value a writer beside it is replacing, and two
	# instructions can exchange registers in one bundle with no temporary.
	# The ISS gets this structurally -- LvxStaticInst::execute runs every
	# sub-instruction's fetch phase, then every execute phase, then every
	# commit phase -- and nothing tested it.
	#
	# It is worth a test now because it is what the micro-op split would
	# break.  gem5's micro-ops execute in sequence, so splitting a bundle
	# into one StaticInst per syllable makes syllable k+1 see k's writes --
	# and the failure is a wrong answer, not a crash.  Every case below is
	# chosen so that sequential execution gives a different, specific result,
	# named in the comment; the exit code says which case failed.
	#
	# Exit 0 = all pass.  Otherwise the case number.
	.section .text
	.global _start
_start:
	# ---- 1. the parallel swap, which has no sequential equivalent -------
	maked $r1 = 11
	;;
	maked $r2 = 22
	;;
	copyd $r1 = $r2 ; copyd $r2 = $r1
	;;
	# sequential would give r1=22, r2=22
	compd.ne $r3 = $r1, 22
	;;
	compd.ne $r4 = $r2, 11
	;;
	iord $r3 = $r3, $r4
	;;
	maked $r0 = 1
	;;
	cb.dnez $r3 ? .Lfail
	;;

	# ---- 2. a reader beside a writer sees the old value ----------------
	maked $r5 = 7
	;;
	maked $r6 = 99 ; addd $r7 = $r5, 0
	;;
	# r7 must be 7.  Sequential is the same here, so make the writer and
	# the reader the SAME register instead:
	maked $r8 = 5
	;;
	maked $r8 = 50 ; addd $r9 = $r8, 1
	;;
	# r9 must be 6 (old r8 + 1); sequential would give 51.
	maked $r0 = 2
	;;
	compd.ne $r3 = $r9, 6
	;;
	cb.dnez $r3 ? .Lfail
	;;

	# ---- 3. a three-way rotate in one bundle ---------------------------
	maked $r10 = 1
	;;
	maked $r11 = 2
	;;
	maked $r12 = 3
	;;
	copyd $r10 = $r11 ; copyd $r11 = $r12 ; copyd $r12 = $r10
	;;
	# 1,2,3 -> 2,3,1.  Sequential would give 2,3,2.
	compd.ne $r3 = $r10, 2
	;;
	compd.ne $r4 = $r11, 3
	;;
	iord $r3 = $r3, $r4
	;;
	compd.ne $r4 = $r12, 1
	;;
	iord $r3 = $r3, $r4
	;;
	maked $r0 = 3
	;;
	cb.dnez $r3 ? .Lfail
	;;

	# ---- 4. two predicated writes to one register, disjoint ------------
	# Legal on LVX, and the write log is what makes it work: a false
	# predicate must not write, so whichever of the two is true decides the
	# register.  Both orders are exercised.
	maked $r13 = 0			# false
	;;
	maked $r14 = 1			# true
	;;
	maked $r15 = 0
	;;
	maked $r16 = 44
	;;
	maked $r17 = 55
	;;
	cmoved.dnez $r13? $r15 = $r16 ; cmoved.dnez $r14? $r15 = $r17
	;;
	# only the second predicate holds, so r15 must be 55
	maked $r0 = 4
	;;
	compd.ne $r3 = $r15, 55
	;;
	cb.dnez $r3 ? .Lfail
	;;
	maked $r15 = 0
	;;
	cmoved.dnez $r14? $r15 = $r16 ; cmoved.dnez $r13? $r15 = $r17
	;;
	# now only the first holds, so r15 must be 44
	maked $r0 = 5
	;;
	compd.ne $r3 = $r15, 44
	;;
	cb.dnez $r3 ? .Lfail
	;;

	maked $r0 = 0
	;;
.Lfail:
	scall 1
	;;
1:	goto 1b
	;;
