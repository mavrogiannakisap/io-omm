#!/bin/bash
set -uxo pipefail

ex=../cmake-build-relwithdebinfo/src/bench/crimes_omap
num_runs=2
bbs=64
ns=$(seq 1 24)

ssd_base_path=/home/ubuntu/data/tests/ssd/nocache/
rm -rf ${ssd_base_path}
for n in ${ns[@]}; do
  mkdir -p ${ssd_base_path}
  ${ex} --print_csv_headers -r ${num_runs} -v ${bbs} -N ${n} -p ${ssd_base_path} -a -d >>nocache-ssd-${n}-omap-res.csv
  rm -rf ${ssd_base_path}
done

