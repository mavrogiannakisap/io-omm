#!/bin/bash
set -uxo pipefail

. ./env.sh

for v in ${vs[@]}; do
  rm -fr ${hdd_base_path}
  mkdir -p ${hdd_base_path}
  out="hdd-ad-v${v}-${n}-omap-res.csv"
  test -s ${out} || ${omap_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -p ${hdd_base_path} -a -d >${out}
done
