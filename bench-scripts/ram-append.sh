#!/bin/bash
set -uxo pipefail

. ./env.sh

for n in ${append_ns[@]}; do
  for v in ${vs[@]}; do
    out="ram-v${v}-n${n}-ofs-append-res.csv"
    test -s ${out} || ${append_ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${append_s} -L ${append_lf} -i ${lvl} >${out}
  done
done
