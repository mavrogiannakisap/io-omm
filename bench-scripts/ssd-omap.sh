#!/bin/bash
set -uxo pipefail

. ./env.sh

# for v in ${vs[@]}; do
  rm -fr ${ssd_base_path}/*
  out="ssd-ad-v${v}-${n}-omap-res.csv"
  test -s ${out} || ${omap_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -p ${ssd_base_path} -a -d >${out}
# done
