# Freestanding RV64 entry for the LVX ISS's RV64G personality.
#
# gem5 enters at the ELF entry with x2 (sp) already pointing at the top of a
# mapped stack page (arch/lvx/process.cc picks x2 rather than the native $r12
# for an EM_RISCV image), so there is no stack to set up -- just call main and
# turn its return value into SYS_exit's status.
#
# No libc: Ubuntu's gcc-riscv64-unknown-elf ships no newlib, and an ISA test
# does not want one.  Anything a test needs from the environment it asks for
# with ecall, as rv_write below does.
	.text
	.globl _start
	.type  _start, @function
_start:
	# gp (x3) is the global pointer GCC addresses small data through, and the
	# linker defines __global_pointer$ for a crt0 to load -- which there is
	# none of here, so load it.  Left at zero, a gp-relative access goes to a
	# wild address; the symptom is not a clean fault but whatever the nearest
	# mapped page happens to hold.  (CFLAGS also passes
	# -msmall-data-limit=0 so nothing *needs* gp; this is the belt to that
	# brace, since any object built without the flag would.)
.option push
.option norelax
	la      gp, __global_pointer$
.option pop
	li      s0, 0                   # a frame pointer nothing walks
	jal     main
	li      a7, 93                  # SYS_exit(main())
	ecall
.Lhang:
	j       .Lhang                  # unreachable: exit stops the simulation
	.size  _start, .-_start

# ssize_t rv_write(int fd, const void *buf, size_t n)
	.globl rv_write
	.type  rv_write, @function
rv_write:
	li      a7, 64                  # SYS_write
	ecall
	ret
	.size  rv_write, .-rv_write
