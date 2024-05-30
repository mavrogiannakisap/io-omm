#!/bin/bash
set -uxo pipefail

./hdd.sh 1 1
./hdd.sh 2 1
./hdd.sh 2 2
./hdd.sh 2 4
./hdd.sh 2 8
./hdd.sh 2 16
