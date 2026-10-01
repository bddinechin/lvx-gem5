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
        # Both personalities of the one core: "lvx64" is a native VLIW image,
        # "lvx-rv64" an EM_RISCV one the loader claimed for RV64G mode (the
        # C++ LvxLoader in se_workload.cc accepts the same pair).  Leaving the
        # second out here is not a quiet degradation -- it makes a RISC-V ELF
        # fail with "No SE workload is compatible", before any decode.
        return obj.get_arch() in ("lvx64", "lvx-rv64")
