#!/bin/bash
set -uxo pipefail

. ./env.sh
v=128

run() {
  s=$1
  lf=$2
  mkdir -p ${ssd_base_path}
  rm -fr ${ssd_base_path}/*
  out="ssd-ad-v${v}-s${s}-lf${lf}-ofs-res.csv"
  test -s ${out} || ${ofs_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${s} -L ${lf} -p ${ssd_base_path} -a -d >${out}
}

run 2 1
run 2 2
run 2 4
run 2 8
run 2 16
run 2 32
run 2 64
run 2 128
run 2 256
run 2 512
run 2 1024
run 2 2048
run 3 1
run 3 2
run 3 4
run 3 8
run 3 16
run 3 32
run 3 64
run 3 128
run 4 1
run 4 2
run 4 4
run 4 8
run 4 16
run 4 32
run 24 1
