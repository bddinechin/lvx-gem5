	.section .text
	.global _start
# FIXED (2026-07-30) -- originally a bug repro (SIGILL, core dump, from
# gem5's own process, not a simulated guest trap) for every floating-point
# opcode tried, found from an lvx-mlir session adding FFMAD/FFMSD/FFMAW/
# FFMSW codegen support that needed real execution to verify against (see
# lvx-mlir's docs/lvx/EndToEndValidation.md, "ffma/ffms accumulator
# coalescing, verified end to end"). Kept as a regression test now that
# the arithmetic side is fixed -- re-verified after the fix, on real
# gem5, with a real fused ffmad computation, not just "doesn't crash":
# `code=0`, which is exactly this result's low 32 bits (`0x401c000000000000`
# has all-zero low 32 bits -- see fcompd_crash_repro.s's driver for a case
# with a non-trivial, distinguishing low-32 check, since this one's zero
# result can't tell "correct" apart from "silently did nothing").
#
# `lvx-mbr-as`/`lvx-mbr-objdump` always round-tripped `faddd $r0 = $r5, $r0`
# and `ffmad $r3 = $r5, $r0` correctly -- the original bug was never an
# encoding mismatch, only gem5's own FPU-class execution.
#
# **Still open**: floating-point *comparisons* did NOT get fixed by
# whatever fixed arithmetic -- `fcompd`/`fcompw` still crash identically.
# See `fcompd_crash_repro.s` for an isolated repro of that narrower,
# still-open case.
_start:
	make $r5 = 0x4000000000000000ULL       # r5 = 2.0
	;;
	make $r0 = 0x4008000000000000ULL       # r0 = 3.0
	;;
	make $r3 = 0x3ff0000000000000ULL       # r3 = 1.0 (ffmad's accumulator)
	;;
	ffmad $r3 = $r5, $r0                   # r3 = r5*r0 + r3 = 2*3+1 = 7.0
	;;
	copyd $r0 = $r3
	;;
	scall 1
	;;
1:	goto 1b
	;;

# Minimal isolation (no ffma-specific logic at all -- uncomment in place of
# the block above to confirm the crash is FPU-general, not FMA-specific):
#
#	make $r5 = 0x4000000000000000ULL       # r5 = 2.0
#	;;
#	make $r0 = 0x4008000000000000ULL       # r0 = 3.0
#	;;
#	faddd $r0 = $r5, $r0                   # r0 = 5.0
#	;;
#	scall 1
#	;;
#1:	goto 1b
#	;;
