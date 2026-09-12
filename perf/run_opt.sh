#!/bin/bash
# Build -> bit-identical verify gate -> bench, in one shot. Run from anywhere.
# Usage: perf/run_opt.sh <label> [reps]
# Exits non-zero (and does NOT bench) if build fails or output is not bit-identical.
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT" || exit 1
LABEL=${1:?need label}; REP=${2:-5}
[[ $REP =~ ^[1-9][0-9]*$ ]] || { echo "REPEATS MUST BE A POSITIVE INTEGER: $REP" >&2; exit 2; }

echo "=== [$LABEL] build ==="
BUILD=${METIS_BUILD_DIR:-$ROOT/build/release}
CONFIG=${METIS_CONFIG:-Release}
if ! cmake --build "$BUILD" --config "$CONFIG" >"$BUILD/opt_build.log" 2>&1; then
  echo "BUILD FAILED"; grep -iE "error" "$BUILD/opt_build.log" | head; exit 1
fi
echo "build ok"

echo "=== [$LABEL] verify (bit-identical gate) ==="
if ! perf/harness.sh verify "${METIS_REFERENCE_DIR:-$ROOT/perf/ref}"; then
  echo ">>> VERIFY FAILED — output not bit-identical; NOT benching <<<"; exit 2
fi

echo "=== [$LABEL] bench (min of $REP) ==="
if ! perf/harness.sh bench "$LABEL" "$REP"; then
  echo ">>> BENCH FAILED — no results recorded <<<"; exit 3
fi
echo "=== [$LABEL] done ==="
