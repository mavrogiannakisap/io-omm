#!/bin/bash
set -uxo pipefail

. ./env.sh

for n in ${append_ns[@]}; do
  for v in ${vs[@]}; do
    rm -fr ${hdd_base_path}
    mkdir -p ${hdd_base_path}
    out="hdd-ad-v${v}-n${n}-ofs-append-res.csv"
    test -s ${out} || ${append_ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${append_s} -L ${append_lf} -i ${lvl} -p ${hdd_base_path} -d >${out}
  done
done
