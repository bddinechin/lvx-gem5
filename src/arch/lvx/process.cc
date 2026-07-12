/*
 * LVX ISA support for gem5 — SE-mode process (Layer D, milestone).
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "arch/lvx/process.hh"

#include "arch/lvx/regs/int.hh"
#include "base/loader/object_file.hh"
#include "cpu/thread_context.hh"
#include "mem/page_table.hh"
#include "params/Process.hh"
#include "sim/process_impl.hh"
#include "sim/syscall_return.hh"
#include "sim/system.hh"

namespace gem5
{
namespace LvxISA
{

namespace
{
// kv4-v1 ABI: $r12 is the stack pointer.
constexpr RegIndex StackPointerReg = 12;
constexpr Addr PageBytes = 4096;
} // anonymous namespace

Process::Process(const ProcessParams &params, loader::ObjectFile *objFile)
    : gem5::Process(params,
          new EmulationPageTable(params.name, params.pid, PageBytes), objFile)
{
    const Addr stack_base = 0x7FFFFFFFFFFFFFFFULL;
    const Addr max_stack_size = params.maxStackSize;
    const Addr next_thread_stack_base = stack_base - max_stack_size;
    const Addr brk_point = roundUp(image.maxAddr(), PageBytes);
    const Addr mmap_end = 0x4000000000000000ULL;
    memState = std::make_shared<MemState>(this, brk_point, stack_base,
            max_stack_size, next_thread_stack_base, mmap_end);
}

void
Process::initState()
{
    gem5::Process::initState();

    // Reserve a page of stack so the first frame's prologue has backing memory,
    // then start the thread at the ELF entry with SP at the stack top.
    Addr sp = roundDown(memState->getStackBase() - PageBytes, 16);
    memState->setStackMin(sp);
    allocateMem(roundDown(sp, PageBytes), PageBytes);

    ThreadContext *tc = system->threads[contextIds[0]];
    tc->setReg(intRegClass[StackPointerReg], sp);
    tc->pcState(getStartPC());
}

} // namespace LvxISA
} // namespace gem5
