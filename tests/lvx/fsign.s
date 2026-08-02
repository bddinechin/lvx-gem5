	.section .text
	.global _start

# The FSIGN* family: the LVX spelling of RISC-V FSGNJ / FSGNJN / FSGNJX.
#
#   fsign<w>  $rW = $rZ, $rY   magnitude(rZ), sign(rY)                (FSGNJ)
#   fsignn<w> $rW = $rZ, $rY   magnitude(rZ), ~sign(rY)               (FSGNJN)
#   fsignm<w> $rW = $rZ, $rY   magnitude(rZ), sign(rZ) ^ sign(rY)     (FSGNJX)
#
# These are pure bit-shuffling: they never round and never raise a flag, so a
# wrong answer here is always a description bug, never a SoftFloat difference.
#
# Regression coverage for two bugs in the ISA description, both of which this
# test fails against:
#
#   1. The non-sign mask was one bit too narrow -- 0x3FFF.. instead of 0x7FFF..
#      -- so the top exponent bit was cleared along with the sign.  Every value
#      below is +/-2.0, whose top exponent bit IS set (0x4000000000000000,
#      0x40000000, 0x4000), so a narrow mask collapses the magnitude to zero and
#      every check fails.  This bug lived in both the execution: C and the
#      behavior: lambda-RTL, so no C-vs-ISS differential run could see it --
#      only a check against RISC-V semantics, i.e. this test.
#
#   2. FSIGNM*'s behavior: had a stray NOT that its execution: C did not, so the
#      ISS produced sign(rZ) ^ ~sign(rY) -- the inverted sign.  Here
#      sign(+2.0) ^ sign(-1.0) = 1, so the buggy form returns +2.0 where -2.0 is
#      correct, and checks 3, 6 and 9 fail.
#
# Every check adds 1 to the running total in $r0, so the exit code doubles as a
# pass count and a crashed/no-op run (0) is distinguishable from success.
#
# Expect exit code 9.
_start:
	maked $r0 = 0
	;;

# ---- 64-bit: fsignd / fsignnd / fsignmd -------------------------------------
	maked $r1 = 0x4000000000000000ULL	# +2.0
	;;
	maked $r2 = 0xbff0000000000000ULL	# -1.0
	;;
	maked $r10 = 0xc000000000000000ULL	# -2.0, expected
	;;
	fsignd  $r3 = $r1, $r2			# -> -2.0
	;;
	fsignnd $r4 = $r1, $r2			# -> +2.0
	;;
	fsignmd $r5 = $r1, $r2			# 0 ^ 1 = 1 -> -2.0
	;;
	compd.eq $r6 = $r3, $r10		# check 1
	;;
	compd.eq $r7 = $r4, $r1			# check 2
	;;
	compd.eq $r8 = $r5, $r10		# check 3
	;;
	addd $r0 = $r0, $r6
	;;
	addd $r0 = $r0, $r7
	;;
	addd $r0 = $r0, $r8
	;;

# ---- 32-bit: fsignw / fsignnw / fsignmw ------------------------------------
	maked $r11 = 0x40000000			# +2.0f
	;;
	maked $r12 = 0xbf800000			# -1.0f
	;;
	maked $r20 = 0xc0000000			# -2.0f, expected
	;;
	fsignw  $r13 = $r11, $r12		# -> -2.0f
	;;
	fsignnw $r14 = $r11, $r12		# -> +2.0f
	;;
	fsignmw $r15 = $r11, $r12		# -> -2.0f
	;;
	compw.eq $r16 = $r13, $r20		# check 4
	;;
	compw.eq $r17 = $r14, $r11		# check 5
	;;
	compw.eq $r18 = $r15, $r20		# check 6
	;;
	addd $r0 = $r0, $r16
	;;
	addd $r0 = $r0, $r17
	;;
	addd $r0 = $r0, $r18
	;;

# ---- 16-bit: fsignh / fsignnh / fsignmh ------------------------------------
# There is no comph, so mask each result down to its 16 bits and compare with
# compd -- this also keeps the check independent of whatever the upper bits of
# a half-word result hold.
	maked $r21 = 0x4000			# +2.0h
	;;
	maked $r22 = 0xbc00			# -1.0h
	;;
	maked $r30 = 0xc000			# -2.0h, expected
	;;
	maked $r31 = 0xffff			# half-word mask
	;;
	fsignh  $r23 = $r21, $r22		# -> -2.0h
	;;
	fsignnh $r24 = $r21, $r22		# -> +2.0h
	;;
	fsignmh $r25 = $r21, $r22		# -> -2.0h
	;;
	andd $r23 = $r23, $r31
	;;
	andd $r24 = $r24, $r31
	;;
	andd $r25 = $r25, $r31
	;;
	compd.eq $r26 = $r23, $r30		# check 7
	;;
	compd.eq $r27 = $r24, $r21		# check 8
	;;
	compd.eq $r28 = $r25, $r30		# check 9
	;;
	addd $r0 = $r0, $r26
	;;
	addd $r0 = $r0, $r27
	;;
	addd $r0 = $r0, $r28
	;;

	scall 1					# exit(9) when all nine pass
	;;
1:	goto 1b
	;;
