/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — foundational header, not yet compile-verified.
 */

#ifndef __ARCH_LVX_TYPES_HH__
#define __ARCH_LVX_TYPES_HH__

#include <cstdint>
#include <functional>

namespace gem5
{
namespace LvxISA
{

// A MachInst is one 32-bit LVX syllable. The decoder's moreBytes() is fed one
// syllable at a time; it accumulates them into a bundle (until the parallel
// bit clears) and then into per-instruction words (1..3 syllables).
typedef uint32_t MachInst;

// LVX instructions span 1..3 syllables (simple/double/triple encoding spaces).
// LVX_MAXSYLLABLES == 3 (see include/opcode/lvx.h in lvx-binutils).
inline constexpr unsigned MaxInstSyllables = 3;

// A fully-assembled single LVX instruction: its 1..3 syllables in logical
// order (main syllable first, then immediate-extension payloads), plus the
// count. This is the decode key handed to the MDS-generated Decode.c.
//
// NOTE: this is the decoded *instruction*, not the whole bundle. Bundle
// grouping and IMMX reassembly happen in the decoder (Layer C) before an
// ExtMachInst is formed.
struct ExtMachInst
{
    uint32_t syllables[MaxInstSyllables] = {0, 0, 0};
    uint8_t  nsyll = 0;

    bool
    operator==(const ExtMachInst &o) const
    {
        if (nsyll != o.nsyll)
            return false;
        for (unsigned i = 0; i < nsyll; i++)
            if (syllables[i] != o.syllables[i])
                return false;
        return true;
    }

    bool operator!=(const ExtMachInst &o) const { return !(*this == o); }
};

} // namespace LvxISA
} // namespace gem5

// Enable use as a key in gem5's decode_cache::InstMap (optional optimization;
// functional SE mode can decode directly if this is later dropped).
namespace std
{
template <>
struct hash<gem5::LvxISA::ExtMachInst>
{
    size_t
    operator()(const gem5::LvxISA::ExtMachInst &e) const
    {
        size_t h = e.nsyll;
        for (unsigned i = 0; i < e.nsyll; i++)
            h = h * 0x100000001b3ULL ^ e.syllables[i];
        return h;
    }
};
} // namespace std

#endif // __ARCH_LVX_TYPES_HH__
