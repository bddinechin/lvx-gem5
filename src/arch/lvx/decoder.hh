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

#include "arch/generic/decode_cache.hh"
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

    // Decoding a bundle is not cheap -- split it into instructions by
    // steering, reassemble the IMMX extensions, run the generated operand
    // decode for each, and build the bundle's register lists -- and it is a
    // pure function of the syllables: an LvxStaticInst holds each
    // sub-instruction's byte offset *within* the bundle, never an address, so
    // the same bytes decode the same at every PC. Until 2026-10-17 it was
    // redone on every fetch all the same, which is what made a loop pay for
    // its body's decode on every trip.
    //
    // ExtMachInst has had operator== and a std::hash since the port was
    // written, for exactly this and unused until now; they compare and hash
    // nsyll, rv and the live syllables only, so a stale word past the end of
    // a short bundle cannot make two equal bundles miss.
    GenericISA::BasicDecodeCache<Decoder, ExtMachInst> defaultCache;
    friend class GenericISA::BasicDecodeCache<Decoder, ExtMachInst>;

    /// Decode a bundle, for real. Called by the cache on a miss.
    StaticInstPtr decodeInst(ExtMachInst mach_inst);

    StaticInstPtr
    decode(ExtMachInst mach_inst, Addr addr)
    {
        return defaultCache.decode(this, mach_inst, addr);
    }

  public:
    Decoder(const LvxDecoderParams &p);

    void reset() override;
    void moreBytes(const PCStateBase &pc, Addr fetchPC) override;
    StaticInstPtr decode(PCStateBase &pc) override;
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_DECODER_HH__
