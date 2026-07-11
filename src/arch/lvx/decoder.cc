/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — stub decoder (see decoder.hh).
 */

#include "arch/lvx/decoder.hh"

#include "base/logging.hh"
#include "cpu/nop_static_inst.hh"
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
    // STUB: treat each 32-bit syllable as a complete instruction. Real Layer C
    // accumulates syllables until the parallel bit (MSB) clears and reassembles
    // IMMX extension syllables before forming an ExtMachInst.
    emi.syllables[0] = letoh(machInst);
    emi.nsyll = 1;
    instDone = true;
    outOfBytes = true;
}

StaticInstPtr
Decoder::decode(ExtMachInst mach_inst, Addr addr)
{
    // STUB: real impl calls Decode_Decoding_lvx_v1_*(mach_inst.syllables) and
    // maps the resulting Opcode to an LvxStaticInst whose execute() dispatches
    // to the MDS-generated behavior body. Placeholder: a generic nop.
    return nopStaticInstPtr;
}

StaticInstPtr
Decoder::decode(PCStateBase &pc)
{
    if (!instDone)
        return nullptr;
    instDone = false;
    return decode(emi, pc.instAddr());
}

} // namespace LvxISA
} // namespace gem5
