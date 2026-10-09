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

// LP64 SE-mode process: loads the ELF image, builds the initial process stack
// (argc, argv, envp, auxv) and starts a single thread at the ELF entry with the
// stack pointer initialized -- $r12 for a native image, x2 for an RV64G one.
class Process : public gem5::Process
{
  public:
    Process(const ProcessParams &params, loader::ObjectFile *objFile);

    void initState() override;

  private:
    // Build the initial stack frame the ABI hands to _start.  Sets the stack
    // pointer and the PC, so it is the last thing initState does.
    void argsInit(int pageSize);
};

} // namespace LvxISA
} // namespace gem5

#endif // __ARCH_LVX_PROCESS_HH__
