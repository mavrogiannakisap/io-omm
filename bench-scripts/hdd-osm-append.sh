#!/bin/bash
set -uxo pipefail

. ./env.sh
for n in ${append_ns[@]}; do
    for v in ${vs[@]}; do
        rm -fr ${hdd_base_path}
        mkdir -p ${hdd_base_path}
        out="hdd-ad-v${v}-${n}-osm-append-res.csv"
        test -s ${out} || ${append_osm_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -p ${hdd_base_path} -d >${out}
    done
done
