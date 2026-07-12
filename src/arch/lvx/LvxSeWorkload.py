# LVX ISA support for gem5 — SE-mode workload SimObject.
# SPDX-License-Identifier: BSD-3-Clause

from m5.objects.Workload import SEWorkload
from m5.params import *


class LvxSEWorkload(SEWorkload):
    type = "LvxSEWorkload"
    cxx_header = "arch/lvx/se_workload.hh"
    cxx_class = "gem5::LvxISA::SEWorkload"

    @classmethod
    def _is_compatible_with(cls, obj):
        return obj.get_arch() == "lvx64"
