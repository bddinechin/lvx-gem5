# Minimal LVX SE-mode run config for gem5.
# Usage: build/LVX/gem5.opt run_lvx.py <elf>
#
# CPU model is chosen by the LVX_CPU env var: "atomic" (default, functional and
# fast -- what the differential validation harness uses), "timing" (functional +
# memory-system timing), or "minor" (in-order pipeline; consumes the instruction
# register-dependency lists to produce cycle-accurate-ish timing -- much slower).
import os
import sys

import m5
from m5.objects import *

elf = sys.argv[1]
cpu_kind = os.environ.get("LVX_CPU", "atomic").lower()

system = System()
system.clk_domain = SrcClockDomain(clock="1GHz",
                                   voltage_domain=VoltageDomain())
# Atomic memory for the functional CPUs; timing memory for the pipeline model.
system.mem_mode = "atomic" if cpu_kind == "atomic" else "timing"
system.mem_ranges = [AddrRange("512MB")]

if cpu_kind == "minor":
    cpu = LvxMinorCPU()
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
    assoc = 2
    tag_latency = 1
    data_latency = 1
    response_latency = 1
    mshrs = 4
    tgts_per_mshr = 8


if cpu_kind == "atomic":
    cpu.icache_port = system.membus.cpu_side_ports
    cpu.dcache_port = system.membus.cpu_side_ports
else:
    cpu.icache = _L1(size="32kB")
    cpu.dcache = _L1(size="32kB")
    cpu.icache.cpu_side = cpu.icache_port
    cpu.dcache.cpu_side = cpu.dcache_port
    cpu.icache.mem_side = system.membus.cpu_side_ports
    cpu.dcache.mem_side = system.membus.cpu_side_ports
cpu.createInterruptController()

system.mem_ctrl = MemCtrl()
system.mem_ctrl.dram = DDR3_1600_8x8(range=system.mem_ranges[0])
system.mem_ctrl.port = system.membus.mem_side_ports
system.system_port = system.membus.cpu_side_ports

system.workload = SEWorkload.init_compatible(elf)
process = Process()
process.cmd = [elf]
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
