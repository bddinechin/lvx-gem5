# Minimal LVX SE-mode run config for gem5.
# Usage: build/LVX/gem5.opt run_lvx.py <elf>
#
# CPU model is chosen by the LVX_CPU env var: "atomic" (default, functional and
# fast -- what the differential validation harness uses), "timing" (functional +
# memory-system timing), or "minor" (in-order pipeline; consumes the instruction
# register-dependency lists to produce cycle-accurate-ish timing -- much slower).
#
# LVX_TAKEN_BUBBLE overrides the taken-branch penalty of the "minor" model,
# whose default is the architectural LVX_MIN_TAKEN (Processor@minTaken, via the
# generated lvx_stages.py).  It exists to *measure* that penalty: running the
# same binary at 0 and at the real value isolates it exactly, which is how
# tests/lvx/diff/check_timing.sh pins it.  Do not set it for a performance run.
import os
import sys

import m5
from m5.objects import *

elf = sys.argv[1]
cpu_kind = os.environ.get("LVX_CPU", "atomic").lower()

system = System()
# 1 GHz is the LVX core clock, and lvx-newlib's _LVX_CPU_FREQ
# (newlib/libc/sys/mbr/include/mbr/lvx/cpu.h) must equal it: that constant is
# what clock(), times(), gettimeofday() and nanosleep() divide the cycle
# counter by, so a disagreement makes every guest-visible elapsed time wrong by
# the ratio -- silently, because all four divide by the same constant and so
# agree with each other. It was 800 MHz (inherited from KVX) against this
# 1 GHz until 2026-10-09, i.e. 1.25x too large. Change one, change both.
system.clk_domain = SrcClockDomain(clock="1GHz",
                                   voltage_domain=VoltageDomain())
# Atomic memory for the functional CPUs; timing memory for the pipeline model.
system.mem_mode = "atomic" if cpu_kind == "atomic" else "timing"
system.mem_ranges = [AddrRange("512MB")]

if cpu_kind == "minor":
    cpu = LvxMinorCPU()
    bubble = os.environ.get("LVX_TAKEN_BUBBLE")
    if bubble is not None:
        cpu.takenBranchBubble = int(bubble)
    # LVX_MINOR_PARAMS="name=value,..." overrides any other minor parameter,
    # for attributing a cycle count to one of them. Integers and the two bool
    # spellings; anything else is a typo and should fail loudly rather than be
    # set to a string. Like LVX_TAKEN_BUBBLE, this is for measurement -- do
    # not set it for a performance run.
    extra = os.environ.get("LVX_MINOR_PARAMS")
    if extra:
        for item in extra.split(","):
            name, _, value = item.partition("=")
            name, value = name.strip(), value.strip()
            if value in ("True", "False"):
                setattr(cpu, name, value == "True")
            else:
                setattr(cpu, name, int(value))
elif cpu_kind == "timing":
    cpu = LvxTimingSimpleCPU()
else:
    cpu = LvxAtomicSimpleCPU()
system.cpu = cpu

system.membus = SystemXBar()


# Minimal L1 caches on the timing path so a MinorCPU cycle count reflects the
# core pipeline (dependency stalls + FU latencies -- what the register lists
# drive) rather than being dominated by uncached instruction-fetch DRAM latency.
# The atomic CPU needs none (atomic memory has no timing).
class _L1(Cache):
    # 8-way, so 32kB of 64-byte lines is 64 sets. Replacement is gem5's
    # default LRU; there is no L2 and no prefetcher.
    assoc = 8
    tag_latency = 1
    data_latency = 1
    response_latency = 1
    mshrs = 4
    tgts_per_mshr = 8


# A second level, because the description says there is one. The `variant'
# modifier's `.u' carries MemoryLevel=2: the load goes to the next level
# without touching L1, and pays for that by committing at SR -- 15 cycles
# against 3. Two numbers, two levels. Until 2026-10-21 the configuration had
# an L1 and then DRAM, so an L1 miss cost about 48 cycles where the
# architecture has something at 15, and the ISS asserted the 15 from the
# description instead of producing it.
#
# The latencies are chosen so that the architectural numbers fall out: an L1
# hit near 3 cycles (CR - RR) and an L1 miss that hits here near 15 (SR - RR).
# They are a modelling choice, not a measurement of hardware -- what is not a
# choice is that there are two levels and roughly what each costs, which the
# description fixes.
class _L2(Cache):
    assoc = 16
    tag_latency = 11
    data_latency = 11
    response_latency = 3
    mshrs = 16
    tgts_per_mshr = 8


if cpu_kind == "atomic":
    cpu.icache_port = system.membus.cpu_side_ports
    cpu.dcache_port = system.membus.cpu_side_ports
else:
    cpu.icache = _L1(size="32kB")
    cpu.dcache = _L1(size="32kB")
    cpu.icache.cpu_side = cpu.icache_port
    cpu.dcache.cpu_side = cpu.dcache_port
    system.l2bus = L2XBar()
    cpu.icache.mem_side = system.l2bus.cpu_side_ports
    cpu.dcache.mem_side = system.l2bus.cpu_side_ports
    system.l2cache = _L2(size="256kB")
    system.l2cache.cpu_side = system.l2bus.mem_side_ports
    system.l2cache.mem_side = system.membus.cpu_side_ports
cpu.createInterruptController()

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8(range=system.mem_ranges[0])
system.mem_ctrl.port = system.membus.mem_side_ports
system.system_port = system.membus.cpu_side_ports

system.workload = SEWorkload.init_compatible(elf)
process = Process()
# argv[0] is the ELF, and anything after the script's own argument is the
# guest's.  The stack block that carries them is built by
# LvxISA::Process::argsInit.
process.cmd = [elf] + sys.argv[2:]
cpu.workload = process
cpu.createThreads()

root = Root(full_system=False, system=system)
m5.instantiate()
print("== LVX gem5 (%s): beginning execution of %s ==" % (cpu_kind, elf))
exit_event = m5.simulate()
ticks = m5.curTick()
cycles = ticks // 1000  # 1 GHz clock, gem5 default 1 ps/tick => 1000 ticks/cycle
print("== Exiting @ tick %d (%d cycles): %s (code=%d) =="
      % (ticks, cycles, exit_event.getCause(), exit_event.getCode()))
