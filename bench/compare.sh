#!/bin/bash
# A/B timing comparison: runs both binaries interleaved, reports min total ms.
# Usage: compare.sh <binA> <binB> <voices> <buffers> [runs]
set -e
cd "$(dirname "$0")/.."
A="$1"; B="$2"; V="$3"; N="$4"; RUNS="${5:-7}"
declare -a TA TB
for i in $(seq 1 "$RUNS"); do
	ra=$(taskset -c 2 "$A" "$V" "$N" /tmp/cmp_a.raw)
	rb=$(taskset -c 2 "$B" "$V" "$N" /tmp/cmp_b.raw)
	ta=$(echo "$ra" | sed -n 's/.*total=\([0-9.]*\)ms.*/\1/p')
	tb=$(echo "$rb" | sed -n 's/.*total=\([0-9.]*\)ms.*/\1/p')
	ha=$(echo "$ra" | sed -n 's/.*hash=\([0-9a-f]*\).*/\1/p')
	hb=$(echo "$rb" | sed -n 's/.*hash=\([0-9a-f]*\).*/\1/p')
	TA+=("$ta"); TB+=("$tb")
	if [ "$ha" != "$hb" ]; then echo "HASH MISMATCH: $ha vs $hb"; fi
done
mina=$(printf '%s\n' "${TA[@]}" | sort -g | head -1)
minb=$(printf '%s\n' "${TB[@]}" | sort -g | head -1)
echo "voices=$V  A=$(basename $A) min=${mina}ms  B=$(basename $B) min=${minb}ms  (A runs: ${TA[*]})  (B runs: ${TB[*]})"
