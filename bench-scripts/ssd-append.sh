#!/bin/bash
set -uxo pipefail

. ./env.sh

mkdir -p ${ssd_base_path}
for n in ${append_ns[@]}; do
  for v in ${vs[@]}; do
    rm -fr ${ssd_base_path}/*
    out="ssd-ad-v${v}-n${n}-ofs-append-res.csv"
    test -s ${out} || ${append_ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${append_s} -L ${append_lf} -p ${ssd_base_path} -i ${lvl} -d >${out}
  done
done
