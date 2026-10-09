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

// The ten issue slots: BCU0 BCU1 ALU0 ALU1 LSU0 LSU1 EXT0 EXT1 EXT2 EXT3.
// LVX_MAXBUNDLEISSUE in lvx-binutils include/opcode/lvx.h, and the count the
// Exu enum in static_inst.cc enumerates -- a static_assert there ties the two
// together so they cannot drift.
inline constexpr unsigned MaxBundleIssue = 10;

// IMMX, the immediate-extension syllables: availability 8 in Resource.yml.
inline constexpr unsigned MaxBundleImmx = 8;

// So a bundle is at most 18 syllables -- one word per issue slot plus the
// extension words -- which is LVX_MAXBUNDLEWORDS, the bound gas sizes its own
// buffers from and the one bundle-size check it makes.
//
// This was 16, under a comment deriving it from "2 each of BCU/ALU/LSU/EXT
// instructions plus up to 8 IMMX".  That arithmetic is right for eight issue
// slots; LVX has ten, the two TINY slots being the ones it omits.  A derived
// number written out by hand in a second place, which then went stale -- and
// the way it failed was silent, see moreBytes() in decoder.cc.
inline constexpr unsigned MaxBundleSyllables = MaxBundleIssue + MaxBundleImmx;

// A fully-fetched LVX *bundle*: its syllables in binary order (parallel bit of
// the last one is 0), plus the count. This is the decode key handed to Layer C,
// which splits the bundle into instructions, reassembles IMMX extensions, and
// decodes each via the MDS-generated Decode.c.
//
// Modeling choice (see pcstate.hh / PORTING-PLAN): one LvxStaticInst == one
// bundle, so ExtMachInst is the whole bundle, not a single instruction.
struct ExtMachInst
{
    uint32_t syllables[MaxBundleSyllables] = {};
    uint8_t  nsyll = 0;
    bool     rv = false;   // fetched in RISC-V (PS.RV) mode: one fixed 32-bit word

    bool
    operator==(const ExtMachInst &o) const
    {
        if (nsyll != o.nsyll || rv != o.rv)
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
        size_t h = e.nsyll ^ (e.rv ? 0x9e3779b9ULL : 0);
        for (unsigned i = 0; i < e.nsyll; i++)
            h = h * 0x100000001b3ULL ^ e.syllables[i];
        return h;
    }
};
} // namespace std

#endif // __ARCH_LVX_TYPES_HH__
