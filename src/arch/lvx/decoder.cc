/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — stub decoder (see decoder.hh).
 */

#include "arch/lvx/decoder.hh"

#include "arch/lvx/behavior_iface.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "cpu/nop_static_inst.hh"
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
    // Layer C (#9) will build an LvxStaticInst from the decoded Opcode whose
    // execute() dispatches through lvxOpcodeBehavior(). For now we exercise the
    // MDS-generated decode + dispatch path end-to-end (so it links and we can
    // eyeball opcodes) and still return a generic nop.
    Opcode opcode = Opcode__UNDEF;
    switch (mach_inst.nsyll) {
      case 1:  opcode = Decode_Decoding_lvx_v1_simple(mach_inst.syllables); break;
      case 2:  opcode = Decode_Decoding_lvx_v1_double(mach_inst.syllables); break;
      default: opcode = Decode_Decoding_lvx_v1_triple(mach_inst.syllables); break;
    }

    Behavior exec = lvxOpcodeBehavior(opcode, BehaviorExecute);
    DPRINTF(LvxDecode, "decode @ %#x: syll[0]=%#010x nsyll=%u -> opcode=%u "
            "(execute=%s)\n", addr, mach_inst.syllables[0], mach_inst.nsyll,
            (unsigned)opcode, exec ? "yes" : "none");

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
