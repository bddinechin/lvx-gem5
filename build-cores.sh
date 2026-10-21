#!/bin/bash
# Build one gem5 ISS executable per LVX core (gem5-lvx-1.opt, gem5-lvx-2.opt).
#
# WHY TWO: lvx-gcc development needs an ISS for each core (lvx_1 and the
# 512-bit-SIMD lvx_2).  The MDS BE/GEM5 backend emits one core's decode +
# behavior into src/arch/lvx/generated/, and gem5's src/ tree is shared across
# build variants, so a single generated/ dir can hold only one core at a time.
# We therefore build sequentially: install core X into generated/, build, and
# copy the binary out under a per-core name.  The hand-written C/C++ consumers
# are core-agnostic (Decode_Decoding_{simple,double,triple}, Register_R0/PC,
# bare rfbase_<file>), so whichever core's generated/ is present drops in.
#
# The second core only recompiles src/arch/lvx (the rest of gem5 is unchanged),
# so the switch is cheap.  Note the leftover build/LVX afterwards holds the LAST
# core built, in the last variant built; the copied gem5-lvx-<N>.<variant> are
# the deliverables.
#
# Usage:  ./build-cores.sh [core ...]      (default: lvx_1 lvx_2)
#         JOBS=8 ./build-cores.sh          (default: a quarter of the CPUs)
#         VARIANT=fast ./build-cores.sh    (default: opt)
#
# VARIANT picks the gem5 build variant, and the two differ in what they give
# up rather than in optimisation: both are -O3. `opt' adds -g and
# TRACING_ON=1; `fast' adds NDEBUG and TRACING_ON=0, so it drops the asserts
# and the whole DPRINTF machinery. That makes `fast' the one to use for a long
# run and `opt' the one to develop against, because in `fast':
#
#   * --debug-flags is rejected -- the flags do not exist, so a trace is not
#     merely silent, the run does not start;
#   * assert() is gone. gem5's panic()/panic_if()/fatal() are NOT, so the
#     port's own invariants (the operand-snapshot guard in
#     LvxStaticInst::executeMicro, the register-list bounds) still hold.
#
# Both land under build/ as gem5-lvx-<N>.<variant>, and every harness defaults
# to the .opt name (GEM5/GEM5_LVX1/GEM5_LVX2 override it). So a `fast' binary
# is opt-in, which also means it is the one that can go stale unnoticed: it is
# not rebuilt by a plain `./build-cores.sh', and an ISS older than the source
# tree gives cycle counts for an ISA it no longer implements. Rebuild both
# variants together, or check the mtime against lvx-gem5 HEAD before trusting
# a number from one.
#
# JOBS caps scons's parallelism.  A full -j$(nproc) gem5 build (16 C++ jobs
# of a few GB each) has taken the machine down, and so has half of them, so
# the default is a quarter.  Check `free -g` before raising it.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
be="$here/../lvx-mds/build_lvx/BE/GEM5"
jobs="${JOBS:-$(( $(nproc) / 4 ))}"
variant="${VARIANT:-opt}"
case "$variant" in
    opt|fast|debug) ;;
    *) echo "error: VARIANT must be opt, fast or debug (got '$variant')" >&2
       exit 1 ;;
esac
if [ $# -eq 0 ]; then cores=(lvx_1 lvx_2); else cores=("$@"); fi

[ -d "$be" ] || { echo "error: BE/GEM5 build dir not found: $be" >&2
                  echo "run 'make config && make all' from lvx-csw/ first" >&2; exit 1; }

for core in "${cores[@]}"; do
    n="${core#lvx_}"                       # lvx_1 -> 1
    out="$here/build/gem5-lvx-${n}.${variant}"
    echo "=== $core: installing generated/ ==="
    # BE/GEM5 generates each core into its own $(core)/ subdir (glue included), so
    # `install GEM5_CORE=<core>` delivers exactly that core with no cross-core
    # staleness -- it builds the core if needed (install -> install-core -> all-core).
    make -C "$be" GEM5_CORE="$core" install
    echo "=== $core: building gem5.$variant ==="
    # gem5's SConstruct resolves the build path against GetLaunchDir() (the dir
    # scons was launched from), NOT the -C dir -- so we must cd into $here before
    # invoking scons, otherwise the build lands under <cwd>/build/LVX while the cp
    # below reads $here/build/LVX and silently copies a stale binary.  Running from
    # a subshell so the caller's cwd is unchanged.
    ( cd "$here" && scons "build/LVX/gem5.$variant" -j"$jobs" )
    cp "$here/build/LVX/gem5.$variant" "$out"
    echo "=== $core: -> $out ==="
done

echo
echo "Built:"
for core in "${cores[@]}"; do
    n="${core#lvx_}"
    ls -la "$here/build/gem5-lvx-${n}.${variant}"
done
