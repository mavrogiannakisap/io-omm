#!/bin/bash
set -uxo pipefail

. ./env.sh
for n in ${append_ns[@]}; do
    for v in ${vs[@]}; do
        out="ram-v${v}-${n}-osm-append-res.csv"
        test -s ${out} || ${append_osm_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} >${out}
    done
done
