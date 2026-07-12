/*
 * LVX ISA support for gem5 — SE-mode process (Layer D, milestone).
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __ARCH_LVX_PROCESS_HH__
#define __ARCH_LVX_PROCESS_HH__

#include "sim/process.hh"

namespace gem5
{
namespace LvxISA
{

// Minimal LP64 SE-mode process: loads the ELF image, sets up a stack, and
// starts a single thread at the ELF entry point with the stack pointer ($r12)
// initialized. argv/env/auxv are not yet populated (freestanding programs);
// TODO(#10+): full argsInit for hosted (newlib) programs.
class Process : public gem5::Process
{
  public:
    Process(const ProcessParams &params, loader::ObjectFile *objFile);

    void initState() override;
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_PROCESS_HH__
