# LVX ISA support for gem5 — MMU/TLB SimObjects (SE-mode).
# SPDX-License-Identifier: BSD-3-Clause

from m5.objects.BaseMMU import BaseMMU
from m5.objects.BaseTLB import BaseTLB
from m5.params import *


class LvxTLB(BaseTLB):
    type = "LvxTLB"
    cxx_class = "gem5::LvxISA::TLB"
    cxx_header = "arch/lvx/tlb.hh"


class LvxMMU(BaseMMU):
    type = "LvxMMU"
    cxx_class = "gem5::LvxISA::MMU"
    cxx_header = "arch/lvx/mmu.hh"
    itb = LvxTLB(entry_type="instruction")
    dtb = LvxTLB(entry_type="data")
