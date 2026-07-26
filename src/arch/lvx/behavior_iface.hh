/*
 * LVX ISA support for gem5 — C++ view of the MDS-generated C symbols.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Bridges the plain-C generated decode/behavior TUs (Decode.c, behavior.c)
 * into the C++ side (decoder.cc, StaticInst). Everything here is declared
 * extern "C"; the underlying definitions live in the .c files.
 */
#ifndef __ARCH_LVX_BEHAVIOR_IFACE_HH__
#define __ARCH_LVX_BEHAVIOR_IFACE_HH__

extern "C" {

#include "arch/lvx/behavior_rt.h"
#include "arch/lvx/generated/lvx_enums.h"

// Per-encoding-space decoders (from Decode.c). Each maps a raw syllable buffer
// to an Opcode enum value (Opcode__UNDEF if nothing matches). The entry names are
// core-agnostic (Decode.pl strips the core prefix), so this header is identical for
// whichever core's generated/ dir is compiled in -- one src/ tree, two binaries.
Opcode Decode_Decoding_simple(const void *buffer);
Opcode Decode_Decoding_double(const void *buffer);
Opcode Decode_Decoding_triple(const void *buffer);

// Dispatch table (from behavior.c): [opcode][phase], phase 0=fetch, 1=execute,
// 2=commit. Entries for opcodes without a body are null.
extern const Behavior lvx_Opcode_Behavior[][3];
extern const unsigned lvx_Opcode_Behavior_num;

} // extern "C"

namespace gem5
{
namespace LvxISA
{

// Behavior phases, matching the second index of lvx_Opcode_Behavior.
enum BehaviorPhase { BehaviorFetch = 0, BehaviorExecute = 1, BehaviorCommit = 2 };

// Look up a behavior body for (opcode, phase); null if none.
inline Behavior
lvxOpcodeBehavior(unsigned opcode, BehaviorPhase phase)
{
    if (opcode >= lvx_Opcode_Behavior_num)
        return nullptr;
    return lvx_Opcode_Behavior[opcode][phase];
}

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_BEHAVIOR_IFACE_HH__
