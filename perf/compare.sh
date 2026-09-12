#!/bin/bash
# Interleaved A/B: baseline binary (/tmp/baseline_bin) vs current (./build/release/apps).
# Alternates the two per repeat so machine-state drift hits both equally.
# Reports min-of-N METIS time per config and the delta. Usage: perf/compare.sh [reps]
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT" || exit 1
BASE=${METIS_BASELINE_BIN_DIR:-/tmp/baseline_bin}
CUR=${METIS_BIN_DIR:-${METIS_BUILD_DIR:-$ROOT/build/release}/apps}
GRAPH_SOURCE=${METIS_GRAPH_DIR:-$ROOT/graphs}
REP=${1:-7}
SEED=12345

WORK="${METIS_BUILD_DIR:-$ROOT/build/release}/perf"
mkdir -p "$WORK/graphs"
if [ -n "${METIS_CONFIG:-}" ] && [ -d "$CUR/$METIS_CONFIG" ]; then
  CUR="$CUR/$METIS_CONFIG"
fi

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
    [ ! -f "$source" ] || cp "$source" "$destination" || return 1
  done
}

run() { # bindir tool graph nparts extra -> echo metis time
  local d=$1 tool=$2 g="$WORK/$3" np=$4 ex=$5 executable status timing
  local args=(-seed=$SEED)
  executable="$d/$tool"
  [ -x "$executable" ] || { echo "MISSING EXECUTABLE: $executable" >&2; return 1; }
  [ -n "$ex" ] && args+=("$ex")
  args+=("$g")
  [ "$tool" != "ndmetis" ] && args+=("$np")
  rm -f "$WORK/cmp.out" || return 1
  "$executable" "${args[@]}" >"$WORK/cmp.out" 2>&1
  status=$?
  [ "$status" -eq 0 ] || { echo "WORKLOAD FAILED ($status): $executable" >&2; return "$status"; }
  timing=$(awk '/\(METIS time\)/{print $2; exit}' "$WORK/cmp.out")
  is_number "$timing" || { echo "INVALID OR EMPTY TIMING: $tool ${g##*/}" >&2; return 1; }
  printf '%s\n' "$timing"
}
mn() { sort -g | head -1; }

is_positive_integer "$REP" || { echo "REPEATS MUST BE A POSITIVE INTEGER: $REP" >&2; exit 2; }
prepare_graphs || { echo "FAILED TO PREPARE GRAPH DATA" >&2; exit 1; }

printf "%-16s %9s %9s %7s\n" config baseline current "delta%"
samples=0
for c in "${CONFIGS[@]}"; do
  IFS='|' read -r label tool graph nparts extra <<<"$c"
  if [ ! -f "$WORK/$graph" ]; then
    if is_optional_graph "$graph"; then echo "SKIP optional data: $graph"; continue; fi
    echo "MISSING REQUIRED DATA: $graph" >&2; exit 1
  fi
  bf="$WORK/cmp_b.txt"; cf="$WORK/cmp_c.txt"; : >"$bf"; : >"$cf"
  for r in $(seq 1 "$REP"); do
    run "$BASE" "$tool" "$graph" "$nparts" "$extra" >>"$bf" || exit 1
    run "$CUR"  "$tool" "$graph" "$nparts" "$extra" >>"$cf" || exit 1
    samples=$((samples+2))
  done
  [ "$(wc -l <"$bf")" -eq "$REP" ] && [ "$(wc -l <"$cf")" -eq "$REP" ] || {
    echo "MISSING TIMING SAMPLES: $label" >&2; exit 1;
  }
  b=$(mn <"$bf"); c2=$(mn <"$cf")
  d=$(awk -v b="$b" -v c="$c2" 'BEGIN{ if(b>0) printf "%+.1f", 100*(c-b)/b; else print "na"}')
  printf "%-16s %9s %9s %7s\n" "$label" "$b" "$c2" "$d"
done
[ "$samples" -gt 0 ] || { echo "NO TIMING SAMPLES WERE COLLECTED" >&2; exit 1; }
