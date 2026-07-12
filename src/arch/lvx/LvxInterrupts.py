# LVX ISA support for gem5 — interrupt controller SimObject (SE-mode stub).
# SPDX-License-Identifier: BSD-3-Clause

from m5.objects.BaseInterrupts import BaseInterrupts


class LvxInterrupts(BaseInterrupts):
    type = "LvxInterrupts"
    cxx_class = "gem5::LvxISA::Interrupts"
    cxx_header = "arch/lvx/interrupts.hh"
