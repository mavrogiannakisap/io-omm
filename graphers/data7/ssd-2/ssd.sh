#!/bin/bash
set -uxo pipefail

ex=../cmake-build-relwithdebinfo/src/bench/crimes_ofs
num_runs=2
bbs=64
n=23
s=$1
lf=$2

ssd_base_path=/home/ubuntu/data/tests/ssd/
rm -rf ${ssd_base_path}
mkdir -p ${ssd_base_path}
${ex} --print_csv_headers -r ${num_runs} -v ${bbs} -N ${n} -s ${s} -L ${lf} -p ${ssd_base_path} -a -d >>ssd-s${s}-lf${lf}-ofs-res.csv
rm -rf ${ssd_base_path}

