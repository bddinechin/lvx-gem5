	.section .text
	.global _start

# FROUND*, the round-to-integral that does NOT signal inexact, beside FRINT*,
# which does.  They are IEEE-754's roundToIntegral and roundToIntegralExact,
# C's nearbyint() and rint(), and RISC-V Zfa's fround and froundnx; the only
# difference between the two instructions is that one flag, so the only test
# that distinguishes them is one that looks at $cs.
#
# Why the ISA needed the second one: C requires nearbyint, floor, ceil, trunc,
# round and roundeven to leave the inexact flag alone, so none of them could be
# built from FRINT*.  lvx-gcc's scalar.md said so and provided only rint();
# roundeven was a ten-instruction emulation that saved $cs, converted out to an
# integer and back, and restored it.  See lvx-csw/docs/lvx-codegen.md section 3b
# for the full optab mapping.
#
# fflags is read and written through the RISC-V CSR instructions, which are an
# atomic self-swap: `csrrw $rX = 1' writes $rX into fflags and leaves the OLD
# fflags in $rX, and `csrrs $rX = 1' with $rX zero reads fflags without
# changing it.  NX (inexact) is fflags bit 0.
#
# Every check adds 1 to the running total in $r0, so the exit code doubles as a
# pass count and a crashed run (0) is distinguishable from success.
#
# Expect exit code 10.
_start:
	maked $r0 = 0
	;;
	maked $r30 = 0				# the value that clears fflags
	;;

# ---- 1,2: 2.5 is inexact as an integer.  frintd says so, froundd does not ---
	maked $r1 = 0x4004000000000000ULL	# 2.5
	;;
	maked $r2 = 0x4000000000000000ULL	# 2.0, expected at .rn
	;;
	csrrw $r30 = 1				# fflags = 0
	;;
	frintd.rn $r3 = $r1			# 2.5 -> 2.0, raising NX
	;;
	maked $r4 = 0
	;;
	csrrs $r4 = 1				# r4 = fflags
	;;
	andd $r4 = $r4, 1			# NX only
	;;
	compd.eq $r5 = $r4, 1			# check 1: frintd RAISED inexact
	;;
	addd $r0 = $r0, $r5
	;;
	maked $r30 = 0
	;;
	csrrw $r30 = 1				# fflags = 0
	;;
	froundd.rn $r6 = $r1			# 2.5 -> 2.0, raising nothing
	;;
	maked $r7 = 0
	;;
	csrrs $r7 = 1				# r7 = fflags
	;;
	compd.eq $r8 = $r7, 0			# check 2: froundd raised NOTHING
	;;
	addd $r0 = $r0, $r8
	;;

# ---- 3,4: and both still round to the same value ---------------------------
	compd.eq $r9 = $r3, $r2			# check 3: frintd  2.5 .rn -> 2.0
	;;
	compd.eq $r10 = $r6, $r2		# check 4: froundd 2.5 .rn -> 2.0
	;;
	addd $r0 = $r0, $r9
	;;
	addd $r0 = $r0, $r10
	;;

# ---- 5..8: the directed modes, which are what floor/ceil/trunc/round want --
	froundd.rz $r11 = $r1			# toward zero  -> 2.0  (trunc)
	;;
	froundd.ru $r12 = $r1			# upward       -> 3.0  (ceil)
	;;
	maked $r13 = 0xc004000000000000ULL	# -2.5
	;;
	froundd.rd $r14 = $r13			# downward     -> -3.0 (floor)
	;;
	froundd.rm $r15 = $r1			# ties away    -> 3.0  (round)
	;;
	maked $r16 = 0x4008000000000000ULL	# 3.0
	;;
	maked $r17 = 0xc008000000000000ULL	# -3.0
	;;
	compd.eq $r18 = $r11, $r2		# check 5
	;;
	compd.eq $r19 = $r12, $r16		# check 6
	;;
	compd.eq $r20 = $r14, $r17		# check 7
	;;
	compd.eq $r21 = $r15, $r16		# check 8
	;;
	addd $r0 = $r0, $r18
	;;
	addd $r0 = $r0, $r19
	;;
	addd $r0 = $r0, $r20
	;;
	addd $r0 = $r0, $r21
	;;

# ---- 9: an already-integral value is inexact to neither --------------------
	maked $r30 = 0
	;;
	csrrw $r30 = 1				# fflags = 0
	;;
	frintd.rn $r22 = $r2			# 2.0 -> 2.0, nothing raised
	;;
	maked $r23 = 0
	;;
	csrrs $r23 = 1
	;;
	compd.eq $r24 = $r23, 0			# check 9
	;;
	addd $r0 = $r0, $r24
	;;

# ---- 10: the word form behaves the same ------------------------------------
	maked $r25 = 0x40200000			# 2.5f
	;;
	maked $r30 = 0
	;;
	csrrw $r30 = 1				# fflags = 0
	;;
	froundw.rn $r26 = $r25			# 2.5f -> 2.0f, raising nothing
	;;
	maked $r27 = 0
	;;
	csrrs $r27 = 1
	;;
	maked $r28 = 0x40000000			# 2.0f
	;;
	compd.eq $r29 = $r26, $r28
	;;
	compd.eq $r27 = $r27, 0
	;;
	andd $r29 = $r29, $r27			# value right AND no flag raised
	;;
	addd $r0 = $r0, $r29
	;;

	scall 1					# exit(10) when all ten pass
	;;
1:	goto 1b
	;;
