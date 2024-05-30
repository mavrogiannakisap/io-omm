#!/bin/bash
set -uxo pipefail

ex=../cmake-build-relwithdebinfo/src/bench/crimes_ofs
num_runs=1
bbs=64
n=23
s=$1
lf=$2

hdd_base_path=/data/ubuntu/tests/hdd/
rm -rf ${hdd_base_path}
mkdir -p ${hdd_base_path}
${ex} --print_csv_headers -r ${num_runs} -v ${bbs} -N ${n} -s ${s} -L ${lf} -p ${hdd_base_path} -a -d >>hdd-s${s}-lf${lf}-ofs-res.csv
rm -rf ${hdd_base_path}

