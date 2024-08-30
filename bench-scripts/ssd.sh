#!/bin/bash
set -uxo pipefail

. ./env.sh
s=$1
lf=$2

for lvl in ${lvls[@]}; do
    rm -fr ${ssd_base_path}/*
    out="ssd-ad-n${n}-v${v}-s${s}-lf${lf}-ofs-res.csv"
    if [ "$s" -eq 1 ]; then
        test -s ${out} || ${ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${s} -L ${lf} -i ${lvl} -p ${ssd_base_path} -d >${out}
    else
        test -s ${out} || ${ofs_multi_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${s} -L ${lf} -i ${lvl} -p ${ssd_base_path} -d >${out}
    fi
done
