/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — stub decoder to reach a first link. The real Layer C
 * decoder (task #9) accumulates syllables to the bundle's parallel bit,
 * reassembles IMMX extensions, decodes each instruction via the MDS-generated
 * Decode.c, and builds LvxStaticInsts.
 */

#ifndef __ARCH_LVX_DECODER_HH__
#define __ARCH_LVX_DECODER_HH__

#include "arch/generic/decoder.hh"
#include "arch/lvx/pcstate.hh"
#include "arch/lvx/types.hh"
#include "cpu/static_inst.hh"

namespace gem5
{

struct LvxDecoderParams;

namespace LvxISA
{

class Decoder : public InstDecoder
{
  protected:
    MachInst machInst = 0;   // moreBytes() buffer: one 32-bit syllable
    ExtMachInst emi;         // instruction being assembled

    StaticInstPtr decode(ExtMachInst mach_inst, Addr addr);

  public:
    Decoder(const LvxDecoderParams &p);

    void reset() override;
    void moreBytes(const PCStateBase &pc, Addr fetchPC) override;
    StaticInstPtr decode(PCStateBase &pc) override;
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_DECODER_HH__
