#!/bin/bash
set -u
PATH=/usr/bin:/bin:$PATH
export PATH

if [ "$#" -ne 2 ] || [ -z "$1" ] || [ -z "$2" ]; then
  echo "Usage: run-tests.sh SOURCE_DIR OWNED_WORK_DIR" >&2
  exit 1
fi
ROOT=$(cd "$1" && pwd -P) || exit 1
WORK=$2

# Only this fixture's marked workspace may be cleared on repeated runs.
# Resolve physical paths before checking source ancestry; never follow a
# workspace symlink or adopt an existing unmarked directory.
if [ -L "$WORK" ] || { [ -e "$WORK" ] && [ ! -d "$WORK" ]; }; then
  echo "Invalid performance test work directory: $WORK" >&2
  exit 1
fi
if [ -d "$WORK" ] && { [ ! -f "$WORK/.metis-perf-test-work" ] ||
    [ -L "$WORK/.metis-perf-test-work" ]; }; then
  echo "Performance test directory is not owned by this fixture: $WORK" >&2
  exit 1
fi
mkdir -p "$WORK" || exit 1
WORK=$(cd "$WORK" && pwd -P) || exit 1
case "$ROOT/" in
  "$WORK/"*)
    echo "Performance test work directory must not contain the source tree" >&2
    exit 1
    ;;
esac
if [ "$WORK" = / ]; then
  echo "The filesystem root cannot be a performance test work directory" >&2
  exit 1
fi
TEST_DIR="$ROOT/tests/perf"
GRAPH_DIR="$WORK/input-graphs"
EMPTY_GRAPH_DIR="$WORK/empty-graphs"
BIN_DIR="$WORK/bin"
BIN_PATH=$BIN_DIR
if command -v cygpath >/dev/null 2>&1; then
  BIN_PATH=$(cygpath -u "$BIN_DIR")
fi
MISSING_BIN_DIR="$WORK/missing-bin"
REF_DIR="$WORK/ref"
LOG_DIR="$WORK/logs"

rm -rf "$WORK" || exit 1
mkdir -p "$GRAPH_DIR" "$EMPTY_GRAPH_DIR" "$BIN_DIR" "$LOG_DIR" || exit 1
: >"$WORK/.metis-perf-test-work" || exit 1
printf '1 0\n' >"$GRAPH_DIR/mdual.graph"
cp "$TEST_DIR/fake_metis.sh" "$BIN_DIR/gpmetis"
cp "$TEST_DIR/fake_metis.sh" "$BIN_DIR/ndmetis"
cp "$TEST_DIR/fake_cmake.sh" "$BIN_DIR/cmake"
chmod +x "$BIN_DIR/gpmetis" "$BIN_DIR/ndmetis" "$BIN_DIR/cmake"

failures=0
expect_success() {
  local name=$1
  shift
  if "$@" >"$LOG_DIR/$name.log" 2>&1; then
    printf 'PASS %s\n' "$name"
  else
    printf 'FAIL %s (expected success)\n' "$name" >&2
    failures=$((failures+1))
  fi
}
expect_failure() {
  local name=$1
  shift
  if "$@" >"$LOG_DIR/$name.log" 2>&1; then
    printf 'FAIL %s (expected failure)\n' "$name" >&2
    failures=$((failures+1))
  else
    printf 'PASS %s\n' "$name"
  fi
}
assert_file_absent() {
  if [ -e "$1" ]; then
    printf 'FAIL unexpected file: %s\n' "$1" >&2
    failures=$((failures+1))
  fi
}
assert_equal() {
  if [ "$1" != "$2" ]; then
    printf 'FAIL expected %s, got %s\n' "$1" "$2" >&2
    failures=$((failures+1))
  fi
}

run_harness() {
  local mode=$1 graph_dir=$2
  shift 2
  env FAKE_MODE="$mode" METIS_BUILD_DIR="$WORK/build" \
    METIS_BIN_DIR="$BIN_DIR" METIS_GRAPH_DIR="$graph_dir" \
    bash "$ROOT/perf/harness.sh" "$@"
}
run_compare() {
  local mode=$1 graph_dir=$2 baseline=$3
  shift 3
  env FAKE_MODE="$mode" METIS_BUILD_DIR="$WORK/compare-build" \
    METIS_BIN_DIR="$BIN_DIR" METIS_BASELINE_BIN_DIR="$baseline" \
    METIS_GRAPH_DIR="$graph_dir" bash "$ROOT/perf/compare.sh" "$@"
}
run_mem() {
  local baseline=$1
  env FAKE_MODE=valid METIS_BUILD_DIR="$WORK/mem-build" \
    METIS_BIN_DIR="$BIN_DIR" METIS_BASELINE_BIN_DIR="$baseline" \
    METIS_GRAPH_DIR="$GRAPH_DIR" bash "$ROOT/perf/mem.sh"
}
run_opt() {
  local mode=$1 build=$2
  shift 2
  env PATH="$BIN_PATH:$PATH" FAKE_MODE="$mode" \
    FAKE_CMAKE_MARKER="$build/cmake-called" METIS_BUILD_DIR="$build" \
    METIS_BIN_DIR="$BIN_DIR" METIS_GRAPH_DIR="$GRAPH_DIR" \
    METIS_REFERENCE_DIR="$REF_DIR" bash "$ROOT/perf/run_opt.sh" "$@"
}
run_opt_default_reference() {
  local mode=$1 build=$2
  shift 2
  env PATH="$BIN_PATH:$PATH" FAKE_MODE="$mode" \
    FAKE_CMAKE_MARKER="$build/cmake-called" METIS_BUILD_DIR="$build" \
    METIS_BIN_DIR="$BIN_DIR" METIS_GRAPH_DIR="$GRAPH_DIR" \
    bash "$ROOT/perf/run_opt.sh" "$@"
}

expect_success reference run_harness valid "$GRAPH_DIR" ref "$REF_DIR"
assert_equal 8 "$(wc -l <"$REF_DIR/manifest.txt")"
expect_success verify run_harness valid "$GRAPH_DIR" verify "$REF_DIR"

reference_checksum=$(cksum "$REF_DIR/kway_mdual_10")
mkdir -p "$WORK/build/perf/graphs"
cp "$REF_DIR/kway_mdual_10" "$WORK/build/perf/graphs/mdual.graph.part.10"
expect_failure failed-stale run_harness fail "$GRAPH_DIR" verify "$REF_DIR"
assert_file_absent "$WORK/build/perf/graphs/mdual.graph.part.10"
assert_equal "$reference_checksum" "$(cksum "$REF_DIR/kway_mdual_10")"

expect_failure missing-output run_harness missing-output "$GRAPH_DIR" verify "$REF_DIR"
expect_failure missing-required-data run_harness valid "$EMPTY_GRAPH_DIR" verify "$REF_DIR"

cp -R "$REF_DIR" "$WORK/ref-missing"
rm -f "$WORK/ref-missing/kway_mdual_10"
expect_failure missing-reference run_harness valid "$GRAPH_DIR" verify "$WORK/ref-missing"
cp -R "$REF_DIR" "$WORK/ref-extra"
printf 'extra\n' >"$WORK/ref-extra/unlisted-case"
expect_failure extra-reference run_harness valid "$GRAPH_DIR" verify "$WORK/ref-extra"

rm -f "$WORK/build/perf/RESULTS.tsv"
expect_failure empty-timing run_harness empty-timing "$GRAPH_DIR" bench empty 1
assert_file_absent "$WORK/build/perf/RESULTS.tsv"
expect_failure zero-harness-repeats run_harness valid "$GRAPH_DIR" bench zero 0

expect_success compare run_compare valid "$GRAPH_DIR" "$BIN_DIR" 1
expect_failure compare-empty-timing run_compare empty-timing "$GRAPH_DIR" "$BIN_DIR" 1
expect_failure compare-missing-executable run_compare valid "$GRAPH_DIR" "$MISSING_BIN_DIR" 1
expect_failure compare-missing-data run_compare valid "$EMPTY_GRAPH_DIR" "$BIN_DIR" 1
expect_failure zero-compare-repeats run_compare valid "$GRAPH_DIR" "$BIN_DIR" 0
expect_failure mem-missing-executable run_mem "$MISSING_BIN_DIR"

mkdir -p "$WORK/run-opt-zero"
expect_failure zero-run-opt-repeats run_opt valid "$WORK/run-opt-zero" zero 0
assert_file_absent "$WORK/run-opt-zero/cmake-called"
mkdir -p "$WORK/run-opt-failed"
expect_failure run-opt-failed-bench run_opt empty-timing "$WORK/run-opt-failed" failed 1
assert_file_absent "$WORK/run-opt-failed/perf/RESULTS.tsv"
mkdir -p "$WORK/run-opt-valid"
expect_success run-opt-valid run_opt valid "$WORK/run-opt-valid" valid 1
mkdir -p "$WORK/run-opt-default/perf/reference"
cp -R "$REF_DIR/." "$WORK/run-opt-default/perf/reference/"
expect_success run-opt-default-reference run_opt_default_reference valid \
  "$WORK/run-opt-default" default-reference 1

if [ "$failures" -ne 0 ]; then
  printf '%s performance script tests failed\n' "$failures" >&2
  exit 1
fi
