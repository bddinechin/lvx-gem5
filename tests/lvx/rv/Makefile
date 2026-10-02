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
CFLAGS  = -B$(GCCEXEC) -march=$(ARCH) -mabi=$(ABI) -mcmodel=medany \
          -msmall-data-limit=0 -O2 -ffreestanding -fno-builtin -Wall -Wextra
LDFLAGS = -nostdlib -nostartfiles -Wl,-Ttext=0x10000 -Wl,-e,_start

GEM5   ?= $(CURDIR)/../../../build/gem5-lvx1.opt
RUNCFG  = $(CURDIR)/../run_lvx.py

TESTS = rv64i slt rv64ma
ELFS  = $(TESTS:%=%.elf)

all: $(ELFS)

%.elf: %.c start.S
	$(CC) $(CFLAGS) $(LDFLAGS) start.S $< -o $@

%.dis: %.elf
	$(OBJDUMP) -d $< > $@

run: $(ELFS)
	@for e in $(ELFS); do \
	  echo "=== $$e"; \
	  $(GEM5) $(RUNCFG) $$e 2>&1 | grep -E "Exiting|panic|fatal|warn: LVX|^rv"; \
	done

clean:
	rm -f *.elf *.dis

.PHONY: all run clean
