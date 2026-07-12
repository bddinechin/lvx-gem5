#!/bin/bash
# Regenerate the MDS BE/LAO artifacts the LVX gem5 port reuses verbatim.
# These are Kalray-copyright generated files, kept OUT of git (see .gitignore)
# and regenerated from the sibling lvx-mds checkout on demand.
set -euo pipefail
cd "$(dirname "$0")"

# lvx-mds sits alongside lvx-gem5 under lvx-csw. Override with $1 if elsewhere.
SRC="${1:-../../../../../lvx-mds/refs/BE/LAO/lvx_v1}"
if [ ! -d "$SRC" ]; then
    echo "error: lvx-mds LAO output not found at '$SRC'" >&2
    echo "usage: $0 [path/to/lvx-mds/refs/BE/LAO/lvx_v1]" >&2
    exit 1
fi

for f in Decode.c Behavior.tuple Opcode.tuple Register.tuple; do
    cp "$SRC/$f" "./$f"
    echo "regenerated $f"
done

# Derive uniform panic-stub definitions for every helper from Behavior.tuple's
# declaration section. We take the extern DECLARE(...) variant of each helper
# (the #else branch, i.e. no BehaviorDeclareAlt_* defined) and turn its
# prototype into a stub body. Real implementations replace these in the shim.
# HELPER(name) is left intact; behavior.c compiles this with
# #define HELPER(r) Behavior_##r.
grep -E '^BehaviorDeclare\([^,]+,DECLARE\(' Behavior.tuple \
  | sed -E 's/^BehaviorDeclare\([^,]*,DECLARE\((.*);\)\)$/\1 { lvx_behavior_unimpl(); }/' \
  | sort -u > helper_stubs.inc
echo "generated helper_stubs.inc ($(wc -l < helper_stubs.inc) stubs)"
