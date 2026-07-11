# LVX ISA support for gem5.
# SPDX-License-Identifier: BSD-3-Clause

from m5.objects.InstDecoder import InstDecoder


class LvxDecoder(InstDecoder):
    type = "LvxDecoder"
    cxx_class = "gem5::LvxISA::Decoder"
    cxx_header = "arch/lvx/decoder.hh"
