	# Memory ordering within one bundle: a load reads memory as it was
	# before the bundle, whatever slot the store beside it landed in.
	#
	# A bundle's two LSU slots can hold a load and a store to the same
	# address.  They issue together, so the load must observe the
	# pre-bundle contents and the store must still take effect -- the same
	# rule as for registers (see parallel.s), one level down.
	#
	# The ISS ran both MEM_load and MEM_store from the *execute* phase, in
	# syllable order, so `sd ... ; ld ...' let the load see the stored
	# value while `ld ... ; sd ...' did not: the answer depended on which
	# slot the assembler picked.  Cases 1 and 2 are the ones that failed.
	#
	# No compiled code hits this today -- of 50,631 bundles across libc and
	# the validation binaries, none holds two memory accesses -- which is
	# why it went unnoticed, and is no reason for the ISS to disagree with
	# the architecture.
	#
	# Exit 0 = all pass.  Otherwise the case number.
	.section .data
	.align 8
buf:	.quad 0
buf2:	.quad 0

	.section .text
	.global _start
_start:
	maked $r1 = buf
	;;
	maked $r2 = buf2
	;;

	# ---- 1/2. store in the first slot, load in the second --------------
	maked $r6 = 17
	;;
	sd 0[$r1] = $r6			# seed
	;;
	maked $r7 = 34
	;;
	sd 0[$r1] = $r7 ; ld $r3 = 0[$r1]
	;;
	# the load must have read 17, the pre-bundle value
	maked $r0 = 1
	;;
	compd.ne $r4 = $r3, 17
	;;
	cb.dnez $r4 ? .Lfail
	;;
	# and the store must still have landed
	ld $r5 = 0[$r1]
	;;
	maked $r0 = 2
	;;
	compd.ne $r4 = $r5, 34
	;;
	cb.dnez $r4 ? .Lfail
	;;

	# ---- 3/4. the other slot order, which must give the same answer ----
	sd 0[$r1] = $r6			# re-seed to 17
	;;
	ld $r3 = 0[$r1] ; sd 0[$r1] = $r7
	;;
	maked $r0 = 3
	;;
	compd.ne $r4 = $r3, 17
	;;
	cb.dnez $r4 ? .Lfail
	;;
	ld $r5 = 0[$r1]
	;;
	maked $r0 = 4
	;;
	compd.ne $r4 = $r5, 34
	;;
	cb.dnez $r4 ? .Lfail
	;;

	# ---- 5. disjoint addresses are untouched by the ordering -----------
	# A fix that serialised every bundle's accesses the other way, or that
	# dropped the store, would still pass 1-4 on a lucky value; this one
	# says the load and the store are otherwise independent.
	maked $r8 = 99
	;;
	sd 0[$r2] = $r8
	;;
	sd 0[$r1] = $r6 ; ld $r3 = 0[$r2]
	;;
	maked $r0 = 5
	;;
	compd.ne $r4 = $r3, 99
	;;
	cb.dnez $r4 ? .Lfail
	;;
	ld $r5 = 0[$r1]
	;;
	compd.ne $r4 = $r5, 17
	;;
	cb.dnez $r4 ? .Lfail
	;;

	maked $r0 = 0
	;;
.Lfail:
	scall 1
	;;
1:	goto 1b
	;;
