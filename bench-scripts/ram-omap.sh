#!/bin/bash
set -uxo pipefail

. ./env.sh

for v in ${vs[@]}; do
  out="ram-v${v}-${n}-omap-res.csv"
  test -s ${out} || ${omap_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} >${out}
done
