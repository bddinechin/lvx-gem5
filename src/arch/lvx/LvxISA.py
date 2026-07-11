# LVX ISA support for gem5.
# SPDX-License-Identifier: BSD-3-Clause

from m5.objects.BaseISA import BaseISA


class LvxISA(BaseISA):
    type = "LvxISA"
    cxx_class = "gem5::LvxISA::ISA"
    cxx_header = "arch/lvx/isa.hh"
