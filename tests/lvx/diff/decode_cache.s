	# The decoder caches a decoded bundle by its syllables, so one
	# LvxStaticInst is now shared by every address those bytes appear at.
	# That is sound only because nothing in a decoded bundle is absolute:
	# each sub-instruction holds its byte offset *within* the bundle and the
	# PC comes from the execution context, so a PC-relative branch resolves
	# against wherever it is executing.  This checks that it still does.
	#
	# The two `goto' bundles below are byte-identical -- same opcode, same
	# relative displacement -- at two different addresses, and each must
	# reach its own target.  If a decoded bundle ever baked in an absolute
	# address, the second would jump to the first's target instead, skipping
	# an increment (or worse, looping).  $r0 counts the landings, so the exit
	# code is 2 when both are right and 1 or a hang when they are not.
	.section .text
	.global _start
_start:
	maked $r0 = 0
	;;
	goto 1f
	;;
	maked $r0 = 100		# jumped over
	;;
1:
	addd $r0 = $r0, 1
	;;
	goto 2f			# byte-identical to the goto above
	;;
	maked $r0 = 100		# jumped over
	;;
2:
	addd $r0 = $r0, 1
	;;
	scall 1			# exit($r0) == 2
	;;
3:	goto 3b
	;;
