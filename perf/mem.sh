#!/bin/bash
# Peak-memory A/B: baseline binary (/tmp/baseline_bin) vs current build apps.
# Reports METIS gk-tracked heap high-water ("Max memory used", dbglvl=0) for each,
# plus OS max RSS via /usr/bin/time -l as a ground-truth cross-check.
# Usage: perf/mem.sh
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT" || exit 1
BASE=${METIS_BASELINE_BIN_DIR:-/tmp/baseline_bin}; CUR=${METIS_BIN_DIR:-${METIS_BUILD_DIR:-$ROOT/build/release}/apps}; SEED=12345
GRAPH_SOURCE=${METIS_GRAPH_DIR:-$ROOT/graphs}

WORK="${METIS_BUILD_DIR:-$ROOT/build/release}/perf"
mkdir -p "$WORK/graphs"
if [ -n "${METIS_CONFIG:-}" ] && [ -d "$CUR/$METIS_CONFIG" ]; then
  CUR="$CUR/$METIS_CONFIG"
fi

CONFIGS=(
  "kway_cit_10|gpmetis|graphs/cit-Patents.metis|10|"
  "kway_cit_100|gpmetis|graphs/cit-Patents.metis|100|"
  "kway_mdual_10|gpmetis|graphs/mdual.graph|10|"
  "kway_mdual_100|gpmetis|graphs/mdual.graph|100|"
  "rb_mdual_100|gpmetis|graphs/mdual.graph|100|-ptype=rb"
  "nd_mdual|ndmetis|graphs/mdual.graph||"
)
is_optional_graph() { [ "${1##*/}" = "cit-Patents.metis" ]; }
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

run() { local d=$1 tool=$2 g="$WORK/$3" np=$4 ex=$5
  local executable="$d/$tool" status memory
  local args=(-dbglvl=0 -seed=$SEED)
  [ -x "$executable" ] || { echo "MISSING EXECUTABLE: $executable" >&2; return 1; }
  [ -n "$ex" ] && args+=("$ex")
  args+=("$g")
  [ "$tool" != ndmetis ] && args+=("$np")
  "$executable" "${args[@]}" >"$WORK/m.out" 2>"$WORK/m.err"
  status=$?
  [ "$status" -eq 0 ] || { echo "WORKLOAD FAILED ($status): $executable" >&2; return "$status"; }
  memory=$(awk '/Max memory used/{print $4; exit}' "$WORK/m.out")
  is_number "$memory" || { echo "INVALID OR EMPTY MEMORY VALUE: $tool ${g##*/}" >&2; return 1; }
  printf '%s\n' "$memory"
}
runrss() { local d=$1 tool=$2 g="$WORK/$3" np=$4 ex=$5
  local executable="$d/$tool" status value
  local args=(-seed=$SEED)
  [ -x "$executable" ] || { echo "MISSING EXECUTABLE: $executable" >&2; return 1; }
  [ -x /usr/bin/time ] || { echo "MISSING RSS TIMER: /usr/bin/time" >&2; return 1; }
  [ -n "$ex" ] && args+=("$ex")
  args+=("$g")
  [ "$tool" != ndmetis ] && args+=("$np")
  /usr/bin/time -l "$executable" "${args[@]}" >"$WORK/rss.out" 2>"$WORK/rss.err"
  status=$?
  [ "$status" -eq 0 ] || { echo "RSS WORKLOAD FAILED ($status): $executable" >&2; return "$status"; }
  value=$(awk '/maximum resident set size/{printf "%.1f", $1/(1024*1024); exit}' "$WORK/rss.err")
  is_number "$value" || { echo "INVALID OR EMPTY RSS VALUE: $tool ${g##*/}" >&2; return 1; }
  printf '%s\n' "$value"
}

prepare_graphs || { echo "FAILED TO PREPARE GRAPH DATA" >&2; exit 1; }
printf "%-16s %12s %12s %8s | %10s %10s\n" config base_heapMB cur_heapMB delta% base_rssMB cur_rssMB
samples=0
for c in "${CONFIGS[@]}"; do
  IFS='|' read -r label tool graph nparts extra <<<"$c"
  if [ ! -f "$WORK/$graph" ]; then
    if is_optional_graph "$graph"; then echo "SKIP optional data: $graph"; continue; fi
    echo "MISSING REQUIRED DATA: $graph" >&2; exit 1
  fi
  bh=$(run "$BASE" "$tool" "$graph" "$nparts" "$extra") || exit 1
  ch=$(run "$CUR"  "$tool" "$graph" "$nparts" "$extra") || exit 1
  br=$(runrss "$BASE" "$tool" "$graph" "$nparts" "$extra") || exit 1
  cr=$(runrss "$CUR"  "$tool" "$graph" "$nparts" "$extra") || exit 1
  samples=$((samples+4))
  d=$(awk -v b="$bh" -v c="$ch" 'BEGIN{if(b>0)printf "%+.1f",100*(c-b)/b; else print "na"}')
  printf "%-16s %12s %12s %8s | %10s %10s\n" "$label" "$bh" "$ch" "$d" "$br" "$cr"
done
[ "$samples" -gt 0 ] || { echo "NO MEMORY SAMPLES WERE COLLECTED" >&2; exit 1; }
