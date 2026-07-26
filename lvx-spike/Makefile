# Phase 0 spike: prove the MDS-generated LVX execute bodies compile and run
# unmodified against a minimal runtime shim, and that the fetch/execute/commit
# split yields correct VLIW parallel (deferred-write-back) semantics.
#
#   make        # extract generated.c, build, and run
#   make clean

CC      ?= gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -Wno-unused-parameter
TUPLE   ?=

.PHONY: all run clean
all: run

generated.c: extract.sh
	./extract.sh $(TUPLE)

spike: main.c shim.c shim.h ../src/arch/lvx/int256.h generated.c
	$(CC) $(CFLAGS) -o $@ main.c shim.c

run: spike
	./spike

clean:
	rm -f spike generated.c *.o
