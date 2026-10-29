	# The MemoryLevel=2 load variants: `.u' and `.us' must not allocate a
	# line in the L1, but must still read one that is already there.
	#
	# Modifier.yml gives `%0.U: MemoryLevel=2;LoadedAt=SR'.  Only the
	# LoadedAt half used to reach the ISS, as the functional unit's latency
	# (3 cycles for `ld', 15 for `ld.u'), so a `.u' load was charged for a
	# round trip past the first level and then served by it -- and it
	# allocated, polluting the cache it exists to spare.
	#
	# Both halves of the behaviour are checked here, because the obvious
	# way to implement a bypass breaks the second one.  `.u' is NOT
	# uncacheable: the bypass path snoops the L1, so an uncached load sees
	# an earlier cached store to the same line whatever PCR_L1CE says
	# (Modifier.yml's note -- that bit is coherency against other agents,
	# not against the core's own stores).  A request marked
	# Request::UNCACHEABLE, or a cache that declined the lookup rather than
	# just the allocation, would return memory's stale value and pass every
	# timing assertion while doing it.
	#
	# Part 1 is the correctness half and is self-checking: exit code 0 only
	# if every variant read back what was just stored.  Part 2 is the
	# allocation half, counted from outside -- the loop makes TRIPS
	# accesses to one line and check_noalloc.sh asserts on the D-cache
	# misses, which is TRIPS for a variant that never allocates against 1
	# for one that does.
	#
	# --defsym UNCACHED=1   part 2 uses `ld.u'  (expect 2 * LINES misses)
	# --defsym SPECUL=1     part 2 uses `ld.us' (the same)
	#                       neither: `ld'       (expect LINES)
	.equ LINES, 64			# lines walked in part 2, one load each
	.equ SETSZ, LINES * 64		# 4kB, comfortably inside the 32kB L1
	.section .data
	.align 64
cell:	.quad 0
	.align 64
buf:	.space SETSZ
	.section .text
	.global _start
_start:
	# ---- 1a. `.u' must see a store that is sitting dirty in the L1 ----
	maked $r3 = cell
	;;
	maked $r4 = 0x5a5a
	;;
	sd 0[$r3] = $r4			# allocates and dirties the line
	;;
	ld.u $r5 = 0[$r3]		# must snoop it, not read memory
	;;
	compd.ne $r6 = $r5, $r4
	;;
	cb.dnez $r6 ? .Lbad1
	;;

	# ---- 1b. and so must `.us', the dismissible twin ------------------
	maked $r4 = 0x3c3c
	;;
	sd 0[$r3] = $r4
	;;
	ld.us $r5 = 0[$r3]
	;;
	compd.ne $r6 = $r5, $r4
	;;
	cb.dnez $r6 ? .Lbad2
	;;

	# ---- 1c. a `.u' load that missed must still not have allocated, so
	#          a second one reads the same value and not a stale line ---
	maked $r4 = 0x7e7e
	;;
	sd 0[$r3] = $r4
	;;
	ld.u $r5 = 0[$r3]
	;;
	ld.u $r7 = 0[$r3]
	;;
	compd.ne $r6 = $r5, $r7
	;;
	cb.dnez $r6 ? .Lbad3
	;;
	compd.ne $r6 = $r7, $r4
	;;
	cb.dnez $r6 ? .Lbad4
	;;

	# ---- 2. the allocation half: two passes over a buffer that fits ---
	#
	# One load per line, LINES lines, twice -- and a *second* buffer, not
	# the cell part 1 has already installed.  Two passes and not one
	# because allocation is only observable on a miss: a hit does not fill,
	# so it never asks whether to allocate, and a loop hammering one
	# resident line counts the same for every variant.  The first pass
	# misses LINES times whatever the variant; what the second pass costs
	# is the measurement.  A variant that allocates finds all of it still
	# there (the set is 4kB against a 32kB cache, so nothing else evicts
	# it) and misses nothing; one that allocates nothing misses LINES
	# again.
	maked $r9 = buf
	;;
	maked $r7 = 0
	;;
	maked $r1 = 2 * LINES
	;;
.ifdef NOLOOP
	goto .Ldone
	;;
.endif
.Lloop:
	addd $r8 = $r9, $r7
	;;
.ifdef UNCACHED
	ld.u $r2 = 0[$r8]
.else
.ifdef SPECUL
	ld.us $r2 = 0[$r8]
.else
	ld $r2 = 0[$r8]
.endif
.endif
	;;
	addd $r7 = $r7, 64
	;;
	andd $r7 = $r7, SETSZ - 1
	;;
	addd $r1 = $r1, -1
	;;
	cb.dnez $r1 ? .Lloop
	;;

	# Each exit traps after its `scall' rather than falling into the next
	# block.  Under the pipeline model the exit races the bundle behind it
	# and can lose: this fixture read 0 and then committed `.Lbad1',
	# reporting a failure that had not happened, and whether it did
	# depended on whether a 15-cycle `ld.u' or a 3-cycle `ld' was still in
	# flight in the loop above.  Every other fixture here ends with one
	# trailing `goto' to its own address, which masks the same race by
	# spinning instead of executing something; one per exit is what it
	# takes when there is more than one.
.Ldone:
	maked $r0 = 0
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad1:	maked $r0 = 1			# .u did not see the cached store
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad2:	maked $r0 = 2			# .us did not see the cached store
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad3:	maked $r0 = 3			# two .u loads of one address disagreed
	;;
	scall 1
	;;
9:	goto 9b
	;;
.Lbad4:	maked $r0 = 4			# .u read a stale value, not the store
	;;
	scall 1
	;;
9:	goto 9b
	;;
