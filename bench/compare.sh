#!/bin/bash
# Interleaved base-vs-new comparison, CPU-time per frame, min over R runs.
R=5
ITERS=5000
for cfg in simd scalar; do
  for v in 8 16 32; do
    bmin=999999; nmin=999999
    for r in $(seq $R); do
      b=$(taskset -c 9 ./bench/build/mixbench_${cfg}_base $v 1024 $ITERS | grep -o 'min=[0-9.]*' | cut -d= -f2)
      n=$(taskset -c 9 ./bench/build/mixbench_${cfg}      $v 1024 $ITERS | grep -o 'min=[0-9.]*' | cut -d= -f2)
      bmin=$(echo "$bmin $b" | awk '{print ($2<$1)?$2:$1}')
      nmin=$(echo "$nmin $n" | awk '{print ($2<$1)?$2:$1}')
    done
    awk -v c=$cfg -v v=$v -v b=$bmin -v n=$nmin 'BEGIN{printf "%-6s voices=%2d  base=%7.2fus  new=%7.2fus  delta=%+6.1f%%\n", c, v, b, n, (n-b)/b*100}'
  done
done
