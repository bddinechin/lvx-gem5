# LVX ISA support for gem5 — CPU bindings (ArchISA/MMU/Interrupts/Decoder).
# SPDX-License-Identifier: BSD-3-Clause

from m5.objects.BaseAtomicSimpleCPU import BaseAtomicSimpleCPU
from m5.objects.BaseNonCachingSimpleCPU import BaseNonCachingSimpleCPU
from m5.objects.BaseTimingSimpleCPU import BaseTimingSimpleCPU
from m5.objects.LvxDecoder import LvxDecoder
from m5.objects.LvxInterrupts import LvxInterrupts
from m5.objects.LvxISA import LvxISA
from m5.objects.LvxMMU import LvxMMU


class LvxCPU:
    ArchDecoder = LvxDecoder
    ArchMMU = LvxMMU
    ArchInterrupts = LvxInterrupts
    ArchISA = LvxISA


class LvxAtomicSimpleCPU(BaseAtomicSimpleCPU, LvxCPU):
    mmu = LvxMMU()


class LvxNonCachingSimpleCPU(BaseNonCachingSimpleCPU, LvxCPU):
    mmu = LvxMMU()


class LvxTimingSimpleCPU(BaseTimingSimpleCPU, LvxCPU):
    mmu = LvxMMU()
