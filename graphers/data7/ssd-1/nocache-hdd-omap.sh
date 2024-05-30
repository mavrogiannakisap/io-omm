#!/bin/bash
set -uxo pipefail

ex=../cmake-build-relwithdebinfo/src/bench/crimes_omap
num_runs=2
bbs=64
ns=$(seq 1 24)

hdd_base_path=/data/ubuntu/tests/hdd/nocache/
rm -rf ${hdd_base_path}
for n in ${ns[@]}; do
  mkdir -p ${hdd_base_path}
  ${ex} --print_csv_headers -r ${num_runs} -v ${bbs} -N ${n} -p ${hdd_base_path} -a -d >>nocache-hdd-${n}-omap-res.csv
  rm -rf ${hdd_base_path}
done

