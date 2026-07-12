#!/bin/bash
# Refresh the MDS-generated files this directory holds (Decode.c, the tuples,
# behavior.c, MDT/MDT_.h, helper_stubs.inc).
#
# These are now produced and delivered by the MDS BE/GEM5 back-end, which owns
# the gem5 glue (see lvx-mds/MDS/BE/GEM5). This script is a thin convenience
# wrapper: it builds the LAO + GEM5 back-ends and installs their output here.
#
# The derivation logic that used to live in this script (deriving the panic
# stubs, filtering shim-implemented helpers) now lives in
# lvx-mds/MDS/BE/GEM5/BIN/helper-stubs.pl + lvx-family/BE/GEM5/shim-helpers.
set -euo pipefail
cd "$(dirname "$0")"

# lvx-mds sits alongside lvx-gem5 under lvx-csw. Override with $1 if elsewhere.
BUILD="${1:-../../../../../lvx-mds/build_lvx}"
if [ ! -d "$BUILD/BE/GEM5" ]; then
    echo "error: lvx-mds build dir not found at '$BUILD'" >&2
    echo "usage: $0 [path/to/lvx-mds/build_lvx]   (run 'make config' first)" >&2
    exit 1
fi

# BE/GEM5 consumes BE/LAO's output, so build LAO first, then GEM5, then install
# GEM5 (which delivers the LAO tuples + Decode.c + the gem5 glue into here).
make -C "$BUILD/BE/LAO" all
make -C "$BUILD/BE/GEM5" all
make -C "$BUILD/BE/GEM5" install
echo "refreshed $(pwd) from MDS BE/GEM5"
