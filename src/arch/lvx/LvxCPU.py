# LVX ISA support for gem5 — CPU bindings (ArchISA/MMU/Interrupts/Decoder).
# SPDX-License-Identifier: BSD-3-Clause

from m5.objects.BaseAtomicSimpleCPU import BaseAtomicSimpleCPU
from m5.objects.BaseMinorCPU import BaseMinorCPU
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


# In-order pipeline model. Consumes LvxStaticInst's src/dest register lists to
# stall a bundle until its sources are produced (RAW hazards) and applies the
# functional-unit latencies of MinorDefaultFUPool -- the model that actually
# turns the dependency lists into cycle counts. LVX is a statically scheduled
# in-order VLIW, so an in-order pipeline is the right abstraction (one bundle
# issues per cycle unless stalled). See docs/register-buffer-deps.md.
class LvxMinorCPU(BaseMinorCPU, LvxCPU):
    mmu = LvxMMU()
