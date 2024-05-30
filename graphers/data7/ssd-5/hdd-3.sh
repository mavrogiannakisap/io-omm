#!/bin/bash
set -uxo pipefail

./hdd.sh 2 2048
./hdd.sh 3 1
./hdd.sh 3 2
./hdd.sh 3 4
./hdd.sh 3 8
./hdd.sh 3 16
