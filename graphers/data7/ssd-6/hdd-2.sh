#!/bin/bash
set -uxo pipefail

./hdd.sh 2 32
./hdd.sh 2 64
./hdd.sh 2 128
./hdd.sh 2 256
./hdd.sh 2 512
./hdd.sh 2 1024
