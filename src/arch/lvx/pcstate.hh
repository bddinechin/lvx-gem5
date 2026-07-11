/*
 * LVX ISA support for gem5.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * DRAFT (Phase 1) — foundational header, not yet compile-verified.
 * The bundle/micro-op interplay is finalized with the Layer C decoder (task #9).
 */

#ifndef __ARCH_LVX_PCSTATE_HH__
#define __ARCH_LVX_PCSTATE_HH__

#include "arch/generic/pcstate.hh"

namespace gem5
{
namespace LvxISA
{

// LVX executes VLIW bundles. Modeling choice (see PORTING-PLAN / Layer C):
//   * a fetched bundle is the macroop; pc() is the bundle base address;
//   * the instructions within the bundle are microops, indexed by upc();
//   * pc advances by the whole bundle's byte size once the last microop ends.
//
// Because LVX is PC-relative on each instruction's *own* first syllable, each
// LvxStaticInst carries its syllable offset within the bundle; the runtime
// shim forms that instruction's PC as pc() + offset for PC-relative behavior.
// (So this PCState tracks the bundle; per-instruction PC is derived, not
// stored here.)
class PCState : public GenericISA::UPCState<4>
{
  protected:
    typedef GenericISA::UPCState<4> Base;

    // Byte size of the current bundle (== total syllables * 4). Populated by
    // the decoder once the bundle's parallel bit terminates it.
    unsigned _bundleSize = 4;

  public:
    PCState() = default;
    PCState(const PCState &other) : Base(other),
        _bundleSize(other._bundleSize) {}
    PCState &operator=(const PCState &other) = default;
    explicit PCState(Addr addr) { set(addr); }

    PCStateBase *clone() const override { return new PCState(*this); }

    void
    update(const PCStateBase &other) override
    {
        Base::update(other);
        _bundleSize = other.as<PCState>()._bundleSize;
    }

    unsigned bundleSize() const { return _bundleSize; }
    void bundleSize(unsigned s) { _bundleSize = s; }

    // Byte size of the current fetch unit (the bundle).
    Addr size() const { return _bundleSize; }

    void
    set(Addr val) override
    {
        Base::set(val);
        npc(val + _bundleSize);
    }

    // Advance the macro-PC across the whole bundle (used by uEnd()).
    void
    advance() override
    {
        _pc = _npc;
        _npc = _pc + _bundleSize;
    }

    bool
    branching() const override
    {
        return npc() != pc() + _bundleSize || nupc() != upc() + 1;
    }

    bool
    equals(const PCStateBase &other) const override
    {
        return Base::equals(other) &&
            _bundleSize == other.as<PCState>()._bundleSize;
    }

    void
    serialize(CheckpointOut &cp) const override
    {
        Base::serialize(cp);
        SERIALIZE_SCALAR(_bundleSize);
    }

    void
    unserialize(CheckpointIn &cp) override
    {
        Base::unserialize(cp);
        UNSERIALIZE_SCALAR(_bundleSize);
    }
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_PCSTATE_HH__
