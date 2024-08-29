 #!/bin/bash
set -uxo pipefail

. ./env.sh

for n in ${append_ns[@]}; do
  for lvl in ${lvls[@]}; do
    rm -rf ${ssd_base_path}/*
    out="ssd-v${v}-n${n}-i${lvl}-s${append_s}-lf${append_lf}-ofs-append-res.csv"
    test -s ${out} || ${append_ofs_multi_bench_ex} --print_csv_headers -r ${num_runs} -v ${v} -N ${n} -s ${append_s} -L ${append_lf} -i ${lvl} -p ${ssd_base_path} -d >${out}
  done
done
