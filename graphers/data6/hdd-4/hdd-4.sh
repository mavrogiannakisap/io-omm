#!/bin/bash
set -uxo pipefail

./hdd.sh 3 32
./hdd.sh 3 64
./hdd.sh 3 128
./hdd.sh 4 1
./hdd.sh 4 2
./hdd.sh 4 4
