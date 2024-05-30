#!/bin/bash
set -uxo pipefail

ex=../cmake-build-relwithdebinfo/src/bench/crimes_omap
num_runs=2
bbs=64
ns=$(seq 1 24)

ssd_base_path=~/Development/ofs-tester-delete-me/tests/ssd/
rm -rf ${ssd_base_path}
for n in ${ns[@]}; do
  mkdir -p ${ssd_base_path}
  ${ex} --print_csv_headers -r ${num_runs} -v ${bbs} -N ${n} -p ${ssd_base_path} -a -d >>ssd-${n}-omap-res.csv
  rm -rf ${ssd_base_path}
done

