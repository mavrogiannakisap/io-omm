#!/bin/bash
set -uxo pipefail

. ./env.sh
s=$1
lf=$2

for lvl in ${lvls[@]}; do
    rm -fr ${ssd_base_path}/*
    out="ssd-ad-n${n}-v${v}-s${s}-lf${lf}-ofs-res.csv"
    test -s ${out} || ${ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${s} -L ${lf} -i ${lvl} >${out}
done
