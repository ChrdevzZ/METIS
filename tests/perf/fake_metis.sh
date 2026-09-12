#!/bin/bash
set -u

tool=${0##*/}
graph=""
nparts=""
for argument in "$@"; do
  case "$argument" in
    *.graph|*.metis) graph=$argument ;;
    [0-9]*) nparts=$argument ;;
  esac
done
[ -n "$graph" ] || { echo "fake METIS received no graph" >&2; exit 64; }

if [ "$tool" = "ndmetis" ]; then
  output="$graph.iperm"
else
  [ -n "$nparts" ] || { echo "fake gpmetis received no partition count" >&2; exit 64; }
  output="$graph.part.$nparts"
fi

case "${FAKE_MODE:-valid}" in
  fail) exit 9 ;;
  missing-output) ;;
  *) printf 'stable output for %s %s\n' "$tool" "$nparts" >"$output" ;;
esac

if [ "${FAKE_MODE:-valid}" != "empty-timing" ]; then
  printf '  Partitioning: 0.125 sec (METIS time)\n'
fi
printf ' Multilevel: 0.120\n'
printf ' Coarsening: 0.030\n'
printf ' Matching: 0.010\n'
printf ' Contract: 0.020\n'
printf ' Initial Partition: 0.040\n'
printf ' Refinement: 0.050\n'
printf ' Projection: 0.010\n'
printf 'Max memory used: 12.5 MB\n'
if [ "$tool" = "ndmetis" ]; then
  printf 'Operation Count: 5.0e+03\n'
else
  printf -- '- Edgecut: 42, communication volume: 50.\n'
fi
