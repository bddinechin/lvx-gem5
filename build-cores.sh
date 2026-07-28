#!/bin/bash
# Build one gem5 ISS executable per LVX core (gem5-lvx1.opt, gem5-lvx2.opt).
#
# WHY TWO: lvx-gcc development needs an ISS for each core (lvx_v1 and the
# 512-bit-SIMD lvx_v2).  The MDS BE/GEM5 backend emits one core's decode +
# behavior into src/arch/lvx/generated/, and gem5's src/ tree is shared across
# build variants, so a single generated/ dir can hold only one core at a time.
# We therefore build sequentially: install core X into generated/, build, and
# copy the binary out under a per-core name.  The hand-written C/C++ consumers
# are core-agnostic (Decode_Decoding_{simple,double,triple}, Register_R0/PC,
# bare rfbase_<file>), so whichever core's generated/ is present drops in.
#
# The second core only recompiles src/arch/lvx (the rest of gem5 is unchanged),
# so the switch is cheap.  Note the leftover build/LVX afterwards holds the LAST
# core built; the copied gem5-lvx1.opt / gem5-lvx2.opt are the deliverables.
#
# Usage:  ./build-cores.sh [core ...]      (default: lvx_v1 lvx_v2)
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
be="$here/../lvx-mds/build_lvx/BE/GEM5"
jobs="$(nproc)"
if [ $# -eq 0 ]; then cores=(lvx_v1 lvx_v2); else cores=("$@"); fi

[ -d "$be" ] || { echo "error: BE/GEM5 build dir not found: $be" >&2
                  echo "run 'make config && make all' from lvx-csw/ first" >&2; exit 1; }

for core in "${cores[@]}"; do
    n="${core#lvx_v}"                       # lvx_v1 -> 1
    out="$here/build/gem5-lvx${n}.opt"
    echo "=== $core: installing generated/ ==="
    # BE/GEM5 generates each core into its own $(core)/ subdir (glue included), so
    # `install GEM5_CORE=<core>` delivers exactly that core with no cross-core
    # staleness -- it builds the core if needed (install -> install-core -> all-core).
    make -C "$be" GEM5_CORE="$core" install
    echo "=== $core: building gem5.opt ==="
    # gem5's SConstruct resolves the build path against GetLaunchDir() (the dir
    # scons was launched from), NOT the -C dir -- so we must cd into $here before
    # invoking scons, otherwise the build lands under <cwd>/build/LVX while the cp
    # below reads $here/build/LVX and silently copies a stale binary.  Running from
    # a subshell so the caller's cwd is unchanged.
    ( cd "$here" && scons build/LVX/gem5.opt -j"$jobs" )
    cp "$here/build/LVX/gem5.opt" "$out"
    echo "=== $core: -> $out ==="
done

echo
echo "Built:"
for core in "${cores[@]}"; do
    n="${core#lvx_v}"
    ls -la "$here/build/gem5-lvx${n}.opt"
done
