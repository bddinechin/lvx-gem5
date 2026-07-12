# Minimal LVX SE-mode run config for gem5.
# Usage: build/LVX/gem5.opt run_lvx.py <elf>
import sys
import m5
from m5.objects import *

elf = sys.argv[1]

system = System()
system.clk_domain = SrcClockDomain(clock="1GHz",
                                   voltage_domain=VoltageDomain())
system.mem_mode = "atomic"
system.mem_ranges = [AddrRange("512MB")]

cpu = LvxAtomicSimpleCPU()
system.cpu = cpu

system.membus = SystemXBar()
cpu.icache_port = system.membus.cpu_side_ports
cpu.dcache_port = system.membus.cpu_side_ports
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
print("== LVX gem5: beginning execution of %s ==" % elf)
exit_event = m5.simulate()
print("== Exiting @ tick %d: %s (code=%d) =="
      % (m5.curTick(), exit_event.getCause(), exit_event.getCode()))
