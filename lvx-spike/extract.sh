#!/bin/bash
# Regenerate generated.c: the VERBATIM MDS-generated execute bodies the spike
# links against. Kept out of git (Kalray-copyright); regenerated on demand from
# the sibling lvx-mds checkout.
set -euo pipefail
cd "$(dirname "$0")"

TUPLE="${1:-../../lvx-mds/lvx-refs/BE/GEM5/lvx_v1/behavior_bodies.inc}"
if [ ! -f "$TUPLE" ]; then
    echo "error: behavior_bodies.inc not found at '$TUPLE'" >&2
    echo "usage: $0 [path/to/behavior_bodies.inc]" >&2
    exit 1
fi

# Print one C function, given its name: the preceding 'static void' line through
# the first line that is a lone '}'.
extract_fn() {
    awk -v name="$1" '
        prev=="static void" && index($0, name"(")==1 { print prev; grab=1 }
        grab { print; if ($0=="}") exit }
        { prev=$0 }
    ' "$TUPLE"
}

{
    echo '/* VERBATIM extract from lvx-mds .../BE/GEM5/lvx_v1/behavior_bodies.inc - do not edit. */'
    echo '#include "shim.h"'
    echo
    extract_fn execute_lvx_v1_AWAIT_simple
    echo
    extract_fn execute_lvx_v1_ADDW_signextw_registerW_registerZ_registerY_simple
} > generated.c

echo "wrote generated.c from $TUPLE"
