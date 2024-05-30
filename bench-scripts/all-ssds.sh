#!/bin/bash
set -uxo pipefail

. ./env.sh

mkdir -p ${ssd_base_path}
for v in ${vs[@]}; do
    ./ssd-osm.sh
    ./ssd.sh 1 1
    ./ssd.sh 2 1
    ./ssd.sh 4 1
done