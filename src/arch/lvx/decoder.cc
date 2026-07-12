/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — stub decoder (see decoder.hh).
 */

#include "arch/lvx/decoder.hh"

#include "arch/lvx/static_inst.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/LvxDecode.hh"
#include "params/LvxDecoder.hh"

namespace gem5
{
namespace LvxISA
{

Decoder::Decoder(const LvxDecoderParams &p) : InstDecoder(p, &machInst)
{
    reset();
}

void
Decoder::reset()
{
    InstDecoder::reset();
    machInst = 0;
    emi = ExtMachInst();
}

void
Decoder::moreBytes(const PCStateBase &pc, Addr fetchPC)
{
    // The CPU has fetched one 32-bit syllable into machInst. Append it to the
    // bundle being assembled; the bundle ends when a syllable's parallel bit
    // (MSB) is 0. We always consume the buffered syllable (outOfBytes), and are
    // done only when the bundle terminates (or the buffer would overflow).
    uint32_t syllable = letoh(machInst);
    if (emi.nsyll < MaxBundleSyllables)
        emi.syllables[emi.nsyll++] = syllable;
    bool last = !((syllable >> 31) & 0x1) || emi.nsyll >= MaxBundleSyllables;
    instDone = last;
    outOfBytes = true;
}

StaticInstPtr
Decoder::decode(ExtMachInst mach_inst, Addr addr)
{
    return new LvxStaticInst(mach_inst);
}

StaticInstPtr
Decoder::decode(PCStateBase &pc)
{
    if (!instDone)
        return nullptr;
    instDone = false;
    StaticInstPtr si = decode(emi, pc.instAddr());
    emi = ExtMachInst(); // start a fresh bundle
    return si;
}

} // namespace LvxISA
} // namespace gem5
