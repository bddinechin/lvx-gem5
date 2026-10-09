/*
 * LVX ISA support for gem5 — SE-mode process (Layer D, milestone).
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "arch/lvx/process.hh"

#include <vector>

#include "arch/lvx/pcstate.hh"
#include "arch/lvx/regs/int.hh"
#include "base/loader/elf_object.hh"
#include "base/loader/object_file.hh"
#include "cpu/thread_context.hh"
#include "debug/Stack.hh"
#include "mem/page_table.hh"
#include "params/Process.hh"
#include "sim/aux_vector.hh"
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
// RISC-V ABI: x2 is the stack pointer, and ADR-0007 puts x2 at GRS 2.  The two
// personalities genuinely disagree here -- GRS 12 is x12, which is `a2' to a
// RISC-V program -- so entry has to pick by the image's arch.
constexpr RegIndex RvStackPointerReg = 2;
constexpr Addr PageBytes = 4096;

// The ABI's stack boundary is 256 bits -- lvx.h's STACK_BOUNDARY, and
// BIGGEST_ALIGNMENT with it -- so the stack pointer handed to _start must be a
// multiple of 32, not 16.  See the note in initState for what a compiler does
// when it is not.
constexpr Addr StackAlign = 32;

// Bytes of AT_RANDOM data.  Fixed rather than drawn from the RNG: this ISS is
// used as a differential oracle against native builds, and a program that
// hashes or prints its auxv must not change answer between two runs of the
// same binary.  glibc seeds its stack guard from here; newlib ignores it.
constexpr int RandomBytes = 16;
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

    argsInit(PageBytes);
}

// Build the initial process stack: the System V layout _start expects, which is
// what libgloss/lvx-mbr/crt0.c decodes.  Lowest address first, so the stack
// pointer points at argc:
//
//     sp ->  argc                      (8 bytes)
//            argv[0] .. argv[argc-1]   (8 bytes each)
//            NULL
//            envp[0] .. envp[n-1]
//            NULL
//            auxv pairs { type, val }, terminated by AT_NULL
//            ... the strings, and the AT_RANDOM bytes, above all of it ...
//
// Before 2026-10-09 none of this existed: initState set a stack pointer and
// stopped, so argc was whatever happened to be in the register -- zero in
// practice -- and crt0.c called main(0, NULL, NULL) with a comment saying the
// ISS passed no argument vector.  A program reading argv[1] then dereferenced
// NULL, which surfaced as `fatal: readBlob(0x8, ...) failed' from inside the
// simulator rather than as anything a user could interpret.
void
Process::argsInit(int pageSize)
{
    auto *elfObject = dynamic_cast<loader::ElfObject *>(objFile);

    memState->setStackMin(memState->getStackBase());

    // Work out how far down the strings, the AT_RANDOM block and the pointer
    // arrays reach, so the whole region can be mapped in one go.  The auxv is
    // sized first because AT_RANDOM has to point at the block.
    Addr stack_top = memState->getStackMin();
    stack_top -= RandomBytes;
    for (const std::string &arg : argv)
        stack_top -= arg.size() + 1;
    for (const std::string &env : envp)
        stack_top -= env.size() + 1;
    stack_top &= -(Addr)sizeof(uint64_t);

    std::vector<gem5::auxv::AuxVector<uint64_t>> auxv;
    if (elfObject != nullptr) {
        auxv.emplace_back(gem5::auxv::Entry, objFile->entryPoint());
        auxv.emplace_back(gem5::auxv::Phnum, elfObject->programHeaderCount());
        auxv.emplace_back(gem5::auxv::Phent, elfObject->programHeaderSize());
        auxv.emplace_back(gem5::auxv::Phdr, elfObject->programHeaderTable());
        auxv.emplace_back(gem5::auxv::Pagesz, PageBytes);
        auxv.emplace_back(gem5::auxv::Secure, 0);
        auxv.emplace_back(gem5::auxv::Random, stack_top);
        auxv.emplace_back(gem5::auxv::Null, 0);
    }

    const Addr pointer_bytes =
        (1 + argv.size()) * sizeof(uint64_t) +      // argv + its NULL
        (1 + envp.size()) * sizeof(uint64_t) +      // envp + its NULL
        sizeof(uint64_t) +                          // argc
        2 * sizeof(uint64_t) * auxv.size();
    stack_top -= pointer_bytes;
    stack_top &= -StackAlign;

    // A page below the argument block, so the first frame's prologue has
    // backing memory before any stack-growth fault would be taken.
    memState->setStackSize(memState->getStackBase() - stack_top + pageSize);
    memState->mapRegion(roundDown(stack_top - pageSize, pageSize),
                        roundUp(memState->getStackSize(), pageSize), "stack");

    // AT_RANDOM's bytes.  Fixed pattern -- see RandomBytes above.
    memState->setStackMin(memState->getStackMin() - RandomBytes);
    uint8_t at_random[RandomBytes];
    for (int i = 0; i < RandomBytes; i++)
        at_random[i] = (uint8_t)(0xa5u + i);
    initVirtMem->writeBlob(memState->getStackMin(), at_random, RandomBytes);

    // The argument and environment strings, highest first.
    std::vector<Addr> argPointers;
    for (const std::string &arg : argv) {
        memState->setStackMin(memState->getStackMin() - (arg.size() + 1));
        initVirtMem->writeString(memState->getStackMin(), arg.c_str());
        argPointers.push_back(memState->getStackMin());
        DPRINTF(Stack, "Wrote arg \"%s\" to %#x\n", arg,
                memState->getStackMin());
    }
    argPointers.push_back(0);

    std::vector<Addr> envPointers;
    for (const std::string &env : envp) {
        memState->setStackMin(memState->getStackMin() - (env.size() + 1));
        initVirtMem->writeString(memState->getStackMin(), env.c_str());
        envPointers.push_back(memState->getStackMin());
        DPRINTF(Stack, "Wrote env \"%s\" to %#x\n", env,
                memState->getStackMin());
    }
    envPointers.push_back(0);

    // Drop to the base of the pointer block and align.  32 bytes, not 16: the
    // ABI's stack boundary is 256 bits (lvx.h's STACK_BOUNDARY, and
    // BIGGEST_ALIGNMENT with it), and a compiler is entitled to believe it --
    // an alloca declared "align 32" is placed at a fixed offset from $r12 and
    // its address computed with a bitwise or rather than an add.  At 16 the
    // stack top came out 16 mod 32, and such an or then quietly returned the
    // wrong address: a 256-bit vector written lane by lane landed on two lanes
    // instead of four, and every program that did so read back its low half
    // twice.  Nothing diagnoses it; the value is just wrong.
    memState->setStackMin(memState->getStackMin() & -(Addr)sizeof(uint64_t));
    memState->setStackMin(memState->getStackMin() - pointer_bytes);
    memState->setStackMin(memState->getStackMin() & -StackAlign);

    Addr sp = memState->getStackMin();
    const auto push = [this, &sp](uint64_t data) {
        initVirtMem->write(sp, data, ByteOrder::little);
        sp += sizeof(data);
    };

    push((uint64_t)argv.size());
    for (const Addr &p : argPointers)
        push(p);
    for (const Addr &p : envPointers)
        push(p);
    for (const auto &aux : auxv) {
        push(aux.type);
        push(aux.val);
    }

    // RV64G personality: the loader tags an EM_RISCV image as LvxRv64, and
    // PCState.rv selects the RISC-V fetch/decode path (mirrors Arm/Thumb). A
    // native LVX (EM_LVX) image starts with rv clear -- the VLIW bundle path.
    const bool rv = objFile->getArch() == loader::LvxRv64;

    ThreadContext *tc = system->threads[contextIds[0]];
    tc->setReg(intRegClass[rv ? RvStackPointerReg : StackPointerReg],
               memState->getStackMin());

    PCState pc(getStartPC());
    pc.rv(rv);
    tc->pcState(pc);
}

} // namespace LvxISA
} // namespace gem5
