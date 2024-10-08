#!/bin/bash
set -uxo pipefail

. ./env.sh
s=$1
lf=$2

for v in ${vs[@]}; do
  rm -fr ${hdd_base_path}
  mkdir -p ${hdd_base_path}
  out="hdd-ad-v${v}-s${s}-lf${lf}-lvl${lvl}-ofs-res.csv"
  if [ "$s" -eq 1 ]; then
    test -s ${out} || ${ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${s} -L ${lf} -p ${hdd_base_path} -i ${lvl} -d >${out}
  else
    test -s ${out} || ${ofs_multi_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${s} -L ${lf} -p ${hdd_base_path} -d -i ${lvl} >${out}
  fi
done
