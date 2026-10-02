# RV64G tests for the LVX ISS, built with the upstream riscv64-unknown-elf
# toolchain (ADR-0004: LVX assembles and compiles no RISC-V; upstream does).
#
#   make            build every .elf
#   make run        build and run them all on gem5-lvx1.opt
#   make run GEM5=../../../build/gem5-lvx2.opt
#
# RISCV points at the local prefix the toolchain was unpacked into; set it if
# yours is elsewhere, or put riscv64-unknown-elf-gcc on PATH and pass
# RISCV_PREFIX=riscv64-unknown-elf-.
RISCVDIR     ?= $(CURDIR)/../../../../riscv-toolchain
RISCV        ?= $(RISCVDIR)/usr/bin
RISCV_PREFIX ?= $(RISCV)/riscv64-unknown-elf-
CC            = $(RISCV_PREFIX)gcc
OBJDUMP       = $(RISCV_PREFIX)objdump

# The driver looks for its assembler and linker as plain `as'/`ld' in its own
# exec directories, and the Ubuntu package puts them in /usr/bin under the
# triple name -- which is not where they are when the .deb has been unpacked
# into a prefix of our own.  Without this -B, gcc silently falls through to
# the HOST as, whose complaint is the baffling "invalid -march= option:
# rv64imafd".  $(RISCVDIR)/gccexec holds as/ld symlinks for it; see that
# directory's README.
GCCEXEC ?= $(RISCVDIR)/gccexec/

# rv64imafd/lp64d is the profile ADR-0001 fixes, minus C: the ISS has no
# compressed decoder and is not getting one.  -mcmodel=medany keeps the
# addressing to AUIPC pairs.  No libc exists for this triple here, so every
# test is freestanding over start.S.
ARCH   ?= rv64imafd
ABI    ?= lp64d
CFLAGS  = -B$(GCCEXEC) -ffp-contract=off -march=$(ARCH) -mabi=$(ABI) -mcmodel=medany \
          -msmall-data-limit=0 -O2 -ffreestanding -fno-builtin -Wall -Wextra
LDFLAGS = -nostdlib -nostartfiles -Wl,-Ttext=0x10000 -Wl,-e,_start

HOSTCC ?= gcc
GEM5   ?= $(CURDIR)/../../../build/gem5-lvx1.opt
RUNCFG  = $(CURDIR)/../run_lvx.py

TESTS = rv64i slt rv64ma rv64csr
ELFS  = $(TESTS:%=%.elf)

all: $(ELFS)

%.elf: %.c start.S
	$(CC) $(CFLAGS) $(LDFLAGS) start.S $< -o $@

# fp.c is the exception to -fno-builtin: there the whole point is that
# __builtin_sqrt and friends become INSTRUCTIONS, and -fno-builtin turns them
# into calls to a libm this freestanding build does not have.  -fno-math-errno
# is what lets the compiler use the instruction rather than a call that can set
# errno.
FPFLAGS = -B$(GCCEXEC) -march=$(ARCH) -mabi=$(ABI) -mcmodel=medany \
          -msmall-data-limit=0 -O1 -ffreestanding -ffp-contract=off \
          -fno-math-errno -Wall -Wextra
fp.elf: fp.c start.S
	$(CC) $(FPFLAGS) $(LDFLAGS) start.S fp.c -o $@

# The Spike build of the same fp.c: HTIF instead of ecall for the output, and
# its own stack, because Spike runs it bare-metal in M-mode.  Same -Ttext, so
# the two ELFs differ only in the output path.
fp.spike.elf: fp.c start_spike.S htif.c
	$(CC) $(FPFLAGS) $(LDFLAGS) start_spike.S htif.c fp.c -o $@

fpflags.elf: fpflags.c start.S
	$(CC) $(FPFLAGS) $(LDFLAGS) start.S fpflags.c -o $@

fpflags.spike.elf: fpflags.c start_spike.S htif.c
	$(CC) $(FPFLAGS) $(LDFLAGS) start_spike.S htif.c fpflags.c -o $@

%.dis: %.elf
	$(OBJDUMP) -d $< > $@

# `run' is the self-checking tests; `diff' is the FP oracle.  Both, in order:
check: run diff spike

run: $(ELFS)
	@for e in $(ELFS); do \
	  echo "=== $$e"; \
	  $(GEM5) $(RUNCFG) $$e 2>&1 | grep -E "Exiting|panic|fatal|warn: LVX|^rv"; \
	done

# The differential FP check: the same fp.c built for RV64 and for the host, run
# on both, compared word for word by fpdiff.py.  -ffp-contract=off on both sides
# so neither compiler fuses a multiply-add the other does not, and
# -fno-math-errno so the FP calls in the program become instructions rather than
# calls to a libm the freestanding side does not have.
fp.host: fp.c
	$(HOSTCC) -DHOST -O1 -ffp-contract=off -fno-math-errno -Wall $< -o $@ -lm

diff: fp.elf fp.host
	@./fp.host > fp.host.out
	@$(GEM5) $(RUNCFG) fp.elf 2>/dev/null > fp.iss.out
	@python3 fpdiff.py fp.host.out fp.iss.out fp.elf

# The stronger oracle: Spike, which is the reference implementation of the
# semantics this port was written against -- and, unlike the host FPU, it has
# the same fflags.  -m maps memory where the ELF is linked, so the Spike build
# needs no different link address.
SPIKEDIR  ?= $(CURDIR)/../../../../spike-build
SPIKE     ?= $(SPIKEDIR)/spike
SPIKE_ISA ?= rv64imafd
# Spike runs dtc at startup, and it is unpacked beside it rather than installed
# (same reason as the RISC-V toolchain: no root here), so the recipe puts it on
# PATH.  Without it: "Failed to run dtc: No such file or directory".
SPIKE_PATH ?= $(SPIKEDIR)/tools/usr/bin
# Memory for the region the test is linked into, with slack BELOW the text: the
# ELF's first segment starts a page under _start, because the program headers
# are mapped with it.  The top stays under the CLINT at 0x2000000, or Spike
# refuses with "devices at ... overlap".
SPIKE_MEM ?= 0x8000:0x1ff7000

# Two tests go through Spike: fp (results, the same program the host oracle
# runs) and fpflags (results AND the fflags each operation raised, which only
# Spike can be asked for -- and the reason this oracle exists).
SPIKE_TESTS = fp fpflags

spike: $(SPIKE_TESTS:%=%.spike.elf) $(SPIKE_TESTS:%=%.elf)
	@for t in $(SPIKE_TESTS); do \
	  PATH=$(SPIKE_PATH):$$PATH $(SPIKE) --isa=$(SPIKE_ISA) -m$(SPIKE_MEM) \
	    $$t.spike.elf > $$t.spike.out; \
	  $(GEM5) $(RUNCFG) $$t.elf 2>/dev/null > $$t.iss.out; \
	  python3 fpdiff.py $$t.spike.out $$t.iss.out $$t.elf --raw-reference \
	    --name $$t || exit 1; \
	done

clean:
	rm -f *.elf *.dis *.host *.out *.raw

.PHONY: all check run diff spike clean
