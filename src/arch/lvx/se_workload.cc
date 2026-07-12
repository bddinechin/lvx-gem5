/*
 * LVX ISA support for gem5 — SE-mode workload + process loader.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "arch/lvx/se_workload.hh"

#include "arch/lvx/process.hh"
#include "base/loader/object_file.hh"
#include "sim/process.hh"

namespace gem5
{
namespace LvxISA
{

// Registers an LVX SE-mode process for LVX 64-bit ELF images. A static instance
// adds itself to the loader registry; Process::tryLoaders() consults it.
class LvxLoader : public Process::Loader
{
  public:
    Process *
    load(const ProcessParams &params, loader::ObjectFile *obj) override
    {
        if (obj->getArch() != loader::Lvx64)
            return nullptr;
        return new LvxISA::Process(params, obj);
    }
};

LvxLoader lvxLoader;

} // namespace LvxISA
} // namespace gem5
