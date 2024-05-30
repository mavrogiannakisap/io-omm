#!/bin/bash

set -eEuxo pipefail


ex=../cmake-build-relwithdebinfo/src/bench/buildfiles
ssd_base_path=/homelocal/amin/ssdmount/scratch/datadir/ssd
hdd_base_path=/mnt/hdd1/tests/hdd

for path in ${ssd_base_path} ${hdd_base_path}; do
  test -d ${path} && ${ex} -p ${path} -N 23 -v 32   || true
  test -d ${path} && ${ex} -p ${path} -N 23 -v 128  || true
  test -d ${path} && ${ex} -p ${path} -N 23 -v 4096 || true
done