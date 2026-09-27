#!/usr/bin/env bash
# Compile examples/demo.c with clang -O0 and push it through the
# hand-written passes stage by stage, printing what each stage caught and
# the instruction count along the way. For calibration it also shows what
# the built-in default<O2> pipeline does to the same file.
#
#   ./examples/run-demo.sh
#   OPT=/path/to/opt CLANG=/path/to/clang ./examples/run-demo.sh

set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(dirname "$HERE")
BUILD=${BUILD:-$ROOT/build}
WORK=${WORK:-$BUILD/demo}

DEFAULT_CLANG="$ROOT/llvm-install/bin/clang"
[ -x "$DEFAULT_CLANG" ] || DEFAULT_CLANG=clang
DEFAULT_OPT="$ROOT/llvm-build/bin/opt"
[ -x "$DEFAULT_OPT" ] || DEFAULT_OPT="$ROOT/llvm-install/bin/opt"
[ -x "$DEFAULT_OPT" ] || DEFAULT_OPT=opt
CLANG=${CLANG:-$DEFAULT_CLANG}
OPT=${OPT:-$DEFAULT_OPT}

# -O0 sticks optnone on everything and the passes would skip the
# functions entirely, so ask clang not to emit it. Otherwise this is the
# raw frontend output the passes are meant to handle.
# (sccp-lite is not in the chain on purpose: clang -O0 pre-folds literal
# arithmetic, so there are no constant conditions left for it -- it needs
# hand-written .ll like examples/sccp.ll to show off.)
mkdir -p "$WORK"
$CLANG -O0 -Xclang -disable-O0-optnone -S -emit-llvm -o "$WORK/0-clang.ll" "$HERE/demo.c"

count_ir() {
  # roughly: one line per instruction (indented, not a label or comment).
  # grep -c exits 1 on zero matches, don't let that kill the script.
  grep -cE '^[[:space:]]+[^[:space:];]' "$1" || true
}

stage() { # name plugin passes in out
  local name=$1 plugin=$2 passes=$3 in=$4 out=$5
  echo "--- $name"
  "$OPT" -load-pass-plugin "$plugin" -passes="$passes" -S "$in" -o "$out"
  echo "    $(count_ir "$in") -> $(count_ir "$out") instructions"
}

stage mem2reg-lite      "$BUILD/15-mem2reg/LiteMem2Reg.so"            mem2reg-lite      "$WORK/0-clang.ll" "$WORK/1-mem2reg.ll"
stage instsimplify-demo "$BUILD/09-instcombine-lite/InstSimplifyDemo.so" instsimplify-demo "$WORK/1-mem2reg.ll" "$WORK/2-instsimplify.ll"
stage gvn-lite          "$BUILD/17-gvn-lite/GvnLite.so"              gvn-lite          "$WORK/2-instsimplify.ll" "$WORK/3-gvn.ll"
stage dse-lite          "$BUILD/16-dse-memoryssa/DseLite.so"         dse-lite          "$WORK/3-gvn.ll" "$WORK/4-dse.ll"

echo "--- the real thing, for reference"
"$OPT" -passes='default<O2>' -S "$WORK/0-clang.ll" -o "$WORK/o2-reference.ll" \
  > "$WORK/o2-diagnostics.txt"
echo "    $(count_ir "$WORK/0-clang.ll") -> $(count_ir "$WORK/o2-reference.ll") instructions"

echo
echo "files in $WORK:"
for f in "$WORK"/*.ll; do
  printf '  %-22s %4d instructions\n' "$(basename "$f")" "$(count_ir "$f")"
done
