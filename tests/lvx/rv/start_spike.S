# Freestanding RV64 entry for SPIKE, the other side of the FP oracle.
#
# Spike runs a bare-metal M-mode program and talks to the host over HTIF, not
# over `ecall' -- an ecall in M-mode traps to mtvec, and there is no handler
# here.  So the same fp.c is built twice: once against start.S, whose rv_write
# is an ecall to the LVX ISS's syscall shim, and once against this, whose
# rv_write is an HTIF syscall to fesvr.  Only the output path differs; the
# floating point is the same source through the same compiler.
#
# Spike does not set sp (gem5 does), and the ELF's entry becomes its start_pc
# (riscv/sim.cc: cfg->start_pc.value_or(get_entry_point())), so this sets up
# its own stack.
	.section .text.init,"ax",@progbits
	.globl _start
	.type  _start, @function
_start:
	# Enable the FPU.  After reset mstatus.FS is Off, and Spike's require_fp
	# then makes every floating-point instruction an illegal instruction --
	# which, with mtvec still 0 and no handler, is an infinite trap loop and
	# looks exactly like a hang.  gem5's SE mode does not model FS at all, so
	# this is needed only on this side.  (FS is mstatus[14:13]; 3 is Dirty,
	# which is what the architecture lets an implementation report always.)
	li      t0, 0x6000
	csrs    mstatus, t0
.option push
.option norelax
	la      gp, __global_pointer$
.option pop
	la      sp, _stack_end
	li      s0, 0
	jal     main
	jal     spike_exit              # a0 = main()'s status
.Lhang:
	j       .Lhang                  # unreachable: HTIF stops the simulation
	.size  _start, .-_start

	.bss
	.align 4
_stack:
	.space 65536
_stack_end:
