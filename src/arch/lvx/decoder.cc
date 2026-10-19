/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — stub decoder (see decoder.hh).
 */

#include "arch/lvx/decoder.hh"

#include "arch/lvx/pcstate.hh"
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
    panic_if(emi.nsyll >= MaxBundleSyllables,
             "LVX bundle at %#x exceeds %u syllables", fetchPC,
             MaxBundleSyllables);
    emi.syllables[emi.nsyll++] = syllable;
    if (pc.as<PCState>().rv()) {
        // RISC-V (PS.RV) mode: fixed 32-bit instructions, no VLIW bundle and no
        // parallel bit -- one fetched word is a complete instruction.
        emi.rv = true;
        instDone = true;
    } else {
        // The bundle ends when a syllable's parallel bit is clear -- and ONLY
        // then.  This used to also end it at MaxBundleSyllables, which was the
        // worst available response to running out of buffer: the bundle was
        // closed while the parallel bit still said more syllables followed, so
        // the rest decoded as a *fresh* bundle.  Two ways that is wrong and
        // neither is reported.  The truncated bundle loses its last
        // instruction's IMMX words, so a 64-bit immediate silently becomes
        // whatever the opcode alone decodes to; and bundleBytes is short, so
        // the next fetch address is wrong and execution derails from there.
        //
        // With the cap now equal to LVX_MAXBUNDLEWORDS, overflow means the
        // fetched words are not a legal LVX bundle at all -- a bad branch
        // target, or decoding data.  That is a panic, not something to paper
        // over: the guard above is the one place that can tell.
        instDone = !((syllable >> 31) & 0x1);
    }
    outOfBytes = true;
}

StaticInstPtr
Decoder::decodeInst(ExtMachInst mach_inst)
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

    // A bundle's length is known only here, and the PC state's npc was
    // computed from the *previous* bundle's -- fetch has no other way to
    // know how far to step.  gem5 calls this out at the call site ("the
    // decoder can update the given PC.  Remember not to assign it until
    // *after* calling decode"), and this decoder did not, so on the fetch
    // path npc was wrong whenever consecutive bundles differed in length.
    //
    // It cost far more than a wrong fetch address.  Fetch2 copies this state
    // into the instruction, Execute compares it against the PC the bundle
    // actually produced, and a disagreement is a taken branch: so every
    // change of bundle length squashed the pipeline.  A loop alternating a
    // two-syllable bundle with a one-syllable bundle took a squash per
    // bundle -- 20005 instruction streams over 1000 trips -- and measured
    // 147 cycles a trip where it should take 25.  Uniform-length code, which
    // is all the hand-written tests were, never showed it.
    PCState &lpc = pc.as<PCState>();
    lpc.bundleSize(si->size());
    lpc.npc(lpc.instAddr() + si->size());

    emi = ExtMachInst(); // start a fresh bundle
    return si;
}

} // namespace LvxISA
} // namespace gem5
