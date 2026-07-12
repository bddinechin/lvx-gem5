/*
 * LVX ISA support for gem5 — SE-mode workload (Layer D, milestone).
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __ARCH_LVX_SE_WORKLOAD_HH__
#define __ARCH_LVX_SE_WORKLOAD_HH__

#include "base/loader/object_file.hh"
#include "params/LvxSEWorkload.hh"
#include "sim/se_workload.hh"

namespace gem5
{
namespace LvxISA
{

class SEWorkload : public gem5::SEWorkload
{
  public:
    using Params = LvxSEWorkloadParams;

    SEWorkload(const Params &p) : gem5::SEWorkload(p, PageShift) {}

    loader::Arch getArch() const override { return loader::Lvx64; }
    ByteOrder byteOrder() const override { return ByteOrder::little; }

    // scall is handled inline in the runtime shim (Behavior_syscall) for the
    // milestone, so the workload's syscall entry is currently unused.
    void syscall(ThreadContext *tc) override {}

  private:
    static constexpr Addr PageShift = 12;
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_SE_WORKLOAD_HH__
