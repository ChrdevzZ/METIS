#!/bin/bash
# METIS bit-identical optimization harness.
# Modes:
#   harness.sh ref   <refdir>            capture reference outputs into refdir
#   harness.sh verify <refdir>           run each config once, cmp output vs refdir (bit-identical gate)
#   harness.sh bench  <label> <repeats>  serial timing, min-of-N, append to the build tree perf/RESULTS.tsv
#
# Run from repo root. Single-threaded build assumed (openmp off -> deterministic).

set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT" || exit 1
BUILD=${METIS_BUILD_DIR:-$ROOT/build/release}
BIN=${METIS_BIN_DIR:-$BUILD/apps}
GRAPH_SOURCE=${METIS_GRAPH_DIR:-$ROOT/graphs}
if [ -n "${METIS_CONFIG:-}" ] && [ -d "$BIN/$METIS_CONFIG" ]; then
  BIN="$BIN/$METIS_CONFIG"
fi
GP="$BIN/gpmetis"
ND="$BIN/ndmetis"
WORK="$BUILD/perf"
mkdir -p "$WORK/graphs"
SEED=12345
TMPOUT="$WORK/metis_run.out"
MANIFEST=manifest.txt

# label | tool | graph | nparts | extra
CONFIGS=(
  "kway_cit_10|gpmetis|graphs/cit-Patents.metis|10|"
  "kway_cit_50|gpmetis|graphs/cit-Patents.metis|50|"
  "kway_cit_100|gpmetis|graphs/cit-Patents.metis|100|"
  "kway_mdual_10|gpmetis|graphs/mdual.graph|10|"
  "kway_mdual_50|gpmetis|graphs/mdual.graph|50|"
  "kway_mdual_100|gpmetis|graphs/mdual.graph|100|"
  "rb_mdual_10|gpmetis|graphs/mdual.graph|10|-ptype=rb"
  "rb_mdual_50|gpmetis|graphs/mdual.graph|50|-ptype=rb"
  "rb_mdual_100|gpmetis|graphs/mdual.graph|100|-ptype=rb"
  "nd_mdual|ndmetis|graphs/mdual.graph||"
  "nd_mdual_cc|ndmetis|graphs/mdual.graph||-ccorder"
)

is_optional_graph() { [ "${1##*/}" = "cit-Patents.metis" ]; }
is_positive_integer() { [[ $1 =~ ^[1-9][0-9]*$ ]]; }
is_number() {
  [[ $1 =~ ^[+]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][-+]?[0-9]+)?$ ]]
}

prepare_graphs() {
  local c label tool graph nparts extra source destination
  for c in "${CONFIGS[@]}"; do
    IFS='|' read -r label tool graph nparts extra <<<"$c"
    source="$GRAPH_SOURCE/${graph##*/}"
    destination="$WORK/$graph"
    rm -f "$destination"
    if [ -f "$source" ]; then
      cp "$source" "$destination" || return 1
    fi
  done
}

select_configs() {
  local c label tool graph nparts extra
  ENABLED_CONFIGS=()
  ENABLED_LABELS=()
  for c in "${CONFIGS[@]}"; do
    IFS='|' read -r label tool graph nparts extra <<<"$c"
    if [ -f "$WORK/$graph" ]; then
      ENABLED_CONFIGS+=("$c")
      ENABLED_LABELS+=("$label")
    elif is_optional_graph "$graph"; then
      echo "SKIP optional data: $graph"
    else
      echo "MISSING REQUIRED DATA: $graph" >&2
      return 1
    fi
  done
  [ "${#ENABLED_CONFIGS[@]}" -gt 0 ]
}

write_manifest() {
  printf '%s\n' "${ENABLED_LABELS[@]}" >"$1"
}

verify_reference_set() {
  local dir=$1 expected=$2 entry
  local actual_files=() expected_files=("$MANIFEST")
  [ -d "$dir" ] || { echo "MISSING REFERENCE DIRECTORY: $dir" >&2; return 1; }
  [ -f "$dir/$MANIFEST" ] || { echo "MISSING REFERENCE MANIFEST: $dir/$MANIFEST" >&2; return 1; }
  cmp -s "$expected" "$dir/$MANIFEST" || {
    echo "REFERENCE MANIFEST DOES NOT MATCH ENABLED CASES: $dir/$MANIFEST" >&2
    return 1
  }
  expected_files+=("${ENABLED_LABELS[@]}")
  shopt -s nullglob dotglob
  for entry in "$dir"/*; do
    if [ ! -f "$entry" ]; then
      echo "UNEXPECTED REFERENCE ENTRY: $entry" >&2
      shopt -u nullglob dotglob
      return 1
    fi
    actual_files+=("${entry##*/}")
  done
  shopt -u nullglob dotglob
  [ "$(printf '%s\n' "${actual_files[@]}" | LC_ALL=C sort)" = \
    "$(printf '%s\n' "${expected_files[@]}" | LC_ALL=C sort)" ] || {
    echo "REFERENCE FILE SET DOES NOT MATCH MANIFEST: $dir" >&2
    return 1
  }
}

validate_ref_target() {
  local dir=$1 entry name c label tool graph nparts extra known
  [ ! -e "$dir" ] && return 0
  [ -d "$dir" ] || { echo "REFERENCE TARGET IS NOT A DIRECTORY: $dir" >&2; return 1; }
  shopt -s nullglob dotglob
  for entry in "$dir"/*; do
    name=${entry##*/}
    known=0
    [ "$name" = "$MANIFEST" ] && known=1
    for c in "${CONFIGS[@]}"; do
      IFS='|' read -r label tool graph nparts extra <<<"$c"
      [ "$name" = "$label" ] && known=1
    done
    if [ "$known" -eq 0 ] || [ ! -f "$entry" ]; then
      echo "REFUSING TO REPLACE UNRECOGNIZED REFERENCE ENTRY: $entry" >&2
      shopt -u nullglob dotglob
      return 1
    fi
  done
  shopt -u nullglob dotglob
}

run_one() { # tool graph nparts extra -> sets OUTFILE, writes stdout to TMPOUT
  local tool=$1 graph="$WORK/$2" nparts=$3 extra=$4 executable status
  local args=(-dbglvl=2 -seed=$SEED)
  if [ "$tool" = "ndmetis" ]; then
    executable=$ND
    OUTFILE="$graph.iperm"
  else
    executable=$GP
    OUTFILE="$graph.part.$nparts"
  fi
  rm -f "$OUTFILE" "$TMPOUT" || return 1
  [ -x "$executable" ] || { echo "MISSING EXECUTABLE: $executable" >&2; return 1; }
  [ -n "$extra" ] && args+=("$extra")
  args+=("$graph")
  [ "$tool" != "ndmetis" ] && args+=("$nparts")
  "$executable" "${args[@]}" >"$TMPOUT" 2>&1
  status=$?
  if [ "$status" -ne 0 ]; then
    echo "WORKLOAD FAILED ($status): $tool ${graph##*/}" >&2
    return "$status"
  fi
  [ -f "$OUTFILE" ] || { echo "MISSING OUTPUT: $OUTFILE" >&2; return 1; }
}

metric() { awk -v p="$1" '$0 ~ p {print $NF; exit}' "$TMPOUT"; }
metis_time() { awk '/\(METIS time\)/{print $2; exit}' "$TMPOUT"; }
edgecut() { awk '/Edgecut:/{gsub(",","",$3); print $3; exit}' "$TMPOUT"; }
opcount() { awk '/Operation Count:/{print $NF; exit}' "$TMPOUT"; }

read_sample() {
  local values
  values="$(metis_time) $(metric 'Multilevel:') $(metric 'Coarsening:') $(metric 'Matching:') $(metric 'Contract:') $(metric 'Initial Partition:') $(metric 'Refinement:') $(metric 'Projection:') $(edgecut)$(opcount)"
  read -r -a SAMPLE <<<"$values"
  [ "${#SAMPLE[@]}" -eq 9 ] || return 1
  local value
  for value in "${SAMPLE[@]}"; do
    is_number "$value" || return 1
  done
}

MODE=${1:-}
case "$MODE" in
  ref|verify)
    DIR=${2:?need refdir}
    prepare_graphs || { echo "FAILED TO PREPARE GRAPH DATA" >&2; exit 1; }
    select_configs || exit 1
    EXPECTED_MANIFEST="$WORK/expected-cases.$$"
    write_manifest "$EXPECTED_MANIFEST"
    trap 'rm -f "$EXPECTED_MANIFEST"; [ -n "${REF_STAGE:-}" ] && rm -rf "$REF_STAGE"' EXIT
    if [ "$MODE" = "ref" ]; then
      validate_ref_target "$DIR" || exit 1
      REF_STAGE="$WORK/ref-stage.$$"
      rm -rf "$REF_STAGE"
      mkdir -p "$REF_STAGE" || exit 1
    else
      verify_reference_set "$DIR" "$EXPECTED_MANIFEST" || exit 1
    fi
    pass=0; fail=0
    for c in "${ENABLED_CONFIGS[@]}"; do
      IFS='|' read -r label tool graph nparts extra <<<"$c"
      if ! run_one "$tool" "$graph" "$nparts" "$extra"; then
        echo "FAIL $label  (workload did not complete)"
        fail=$((fail+1))
        continue
      fi
      if [ "$MODE" = "ref" ]; then
        cp "$OUTFILE" "$REF_STAGE/$label" || { fail=$((fail+1)); continue; }
        echo "ref  $label -> $DIR/$label  ($(metis_time)s, cut=$(edgecut)$(opcount))"
        pass=$((pass+1))
      else
        if cmp -s "$OUTFILE" "$DIR/$label"; then echo "PASS $label"; pass=$((pass+1));
        else echo "FAIL $label  (output differs from reference)"; fail=$((fail+1)); fi
      fi
    done
    echo "----"; echo "pass=$pass fail=$fail"
    [ "$fail" -eq 0 ] && [ "$pass" -eq "${#ENABLED_CONFIGS[@]}" ] || exit 1
    if [ "$MODE" = "ref" ]; then
      write_manifest "$REF_STAGE/$MANIFEST"
      mkdir -p "$DIR" || exit 1
      for c in "${CONFIGS[@]}"; do
        IFS='|' read -r label tool graph nparts extra <<<"$c"
        rm -f "$DIR/$label" || exit 1
      done
      rm -f "$DIR/$MANIFEST" || exit 1
      cp "$REF_STAGE"/* "$DIR/" || exit 1
    fi
    ;;
  bench)
    LABEL=${2:?need label}; REP=${3:-3}
    is_positive_integer "$REP" || { echo "REPEATS MUST BE A POSITIVE INTEGER: $REP" >&2; exit 2; }
    prepare_graphs || { echo "FAILED TO PREPARE GRAPH DATA" >&2; exit 1; }
    select_configs || exit 1
    OUT="$WORK/RESULTS.tsv"
    STAGED_RESULTS="$WORK/results.$$"
    : >"$STAGED_RESULTS"
    trap 'rm -f "$STAGED_RESULTS"' EXIT
    echo "== bench '$LABEL' (min of $REP) =="
    printf "%-16s %8s %8s %8s %8s %8s %8s\n" config metis coarsen contract initpart refine
    samples=0
    for c in "${ENABLED_CONFIGS[@]}"; do
      IFS='|' read -r label tool graph nparts extra <<<"$c"
      runs="$WORK/metis_runs.txt"; : >"$runs"
      for r in $(seq 1 "$REP"); do
        run_one "$tool" "$graph" "$nparts" "$extra" || exit 1
        read_sample || { echo "INVALID OR EMPTY TIMING SAMPLE: $label" >&2; exit 1; }
        printf '%s\n' "${SAMPLE[*]}" >>"$runs"
        samples=$((samples+1))
      done
      [ "$(wc -l <"$runs")" -eq "$REP" ] || { echo "MISSING TIMING SAMPLES: $label" >&2; exit 1; }
      best=$(sort -g "$runs" | head -1)
      read -r mt ml co ma ct ip rf pj cut <<<"$best"
      printf "%-16s %8s %8s %8s %8s %8s\n" "$label" "$mt" "$co" "$ct" "$ip" "$rf"
      printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$LABEL" "$label" "$mt" "$ml" "$co" "$ma" "$ct" "$ip" "$rf" "$pj" "$cut" >>"$STAGED_RESULTS"
    done
    [ "$samples" -gt 0 ] || { echo "NO TIMING SAMPLES WERE COLLECTED" >&2; exit 1; }
    [ -f "$OUT" ] || printf 'variant\tconfig\tmetis_t\tmultilevel\tcoarsen\tmatch\tcontract\tinitpart\trefine\tproject\tcut\n' >"$OUT"
    cat "$STAGED_RESULTS" >>"$OUT"
    ;;
  *) echo "usage: harness.sh {ref|verify <dir>|bench <label> [reps]}"; exit 2;;
esac
