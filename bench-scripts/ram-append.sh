#!/bin/bash
set -uxo pipefail

. ./env.sh

for append_n in ${append_ns[@]}; do
  for v in ${vs[@]}; do
    out="ram-v${v}-n${append_n}-lvl${lvl}-ofs-append-res.csv"
    if [ "$append_s" -eq 1 ]; then
        test -s ${out} || ${append_ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${append_n} -s ${append_s} -L ${append_lf} -i ${lvl} >${out}
    else
        test -s ${out} || ${append_ofs_multi_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${append_n} -s ${append_s} -L ${append_lf} -i ${lvl}  >${out}
  done
done
