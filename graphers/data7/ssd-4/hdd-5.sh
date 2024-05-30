#!/bin/bash
set -uxo pipefail

./hdd.sh 4 8
./hdd.sh 4 16
./hdd.sh 4 32
./hdd.sh 6 1
./hdd.sh 6 2
./hdd.sh 6 4
