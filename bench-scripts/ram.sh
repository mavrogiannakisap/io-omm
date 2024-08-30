#!/bin/bash
set -uxo pipefail

. ./env.sh
s=$1
lf=$2

for v in ${vs[@]}; do
  for lvl in ${lvls[@]}; do
    out="ram-v${v}-lvl${lvl}-s${s}-N${n}-ofs-res.csv"
    if [ "$s" -eq 1 ]; then
        test -s ${out} || ${ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${s} -L ${lf} -i ${lvl} >${out}
    else
        test -s ${out} || ${ofs_multi_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${s} -L ${lf} -i ${lvl} >${out}
    fi
  done
done
