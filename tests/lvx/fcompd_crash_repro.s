	.section .text
	.global _start
# FIXED (2026-07-30, same day it was found -- Behavior_floatcomp_64 added
# to shim_fp.cc). Originally a narrower follow-on to fpu_crash_repro.s:
# fixing floating-point *arithmetic* (faddd/fsbfd/fmuld/fdivd/ffmad/ffmsd)
# didn't fix floating-point *comparisons* -- `fcompd`/`fcompw` crashed the
# gem5 process itself identically to the original bug. Kept as a
# regression test now that it's fixed too: `code=1` (-6.6 < 0.0 is true).
#
# Found while verifying an lvx-mlir fix's *sign* correctness (does
# `ffmsd` real hardware compute `c - a*b`, not `a*b - c`) -- the natural
# check (`fcompd olt result, 0.0`) crashed at the time, so that
# verification fell back to an integer right-shift of the raw result bits
# instead (see lvx-mlir's docs/lvx/EndToEndValidation.md, "ffma/ffms
# accumulator coalescing, verified end to end", for that workaround).
#
# `lvx-mbr-as`/`lvx-mbr-objdump` always round-tripped
# `fcompd.olt $r1 = $r3, $r0` correctly -- never an encoding mismatch,
# same pattern as the original arithmetic bug.
_start:
	maked $r3 = 0xc01a666666666666ULL       # r3 = -6.6
	;;
	maked $r0 = 0                           # r0 = 0.0
	;;
	fcompd.olt $r1 = $r3, $r0               # r1 = (r3 < r0) ? 1 : 0
	;;
	copyd $r0 = $r1
	;;
	scall 1
	;;
1:	goto 1b
	;;
