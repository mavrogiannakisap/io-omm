#!/bin/bash
set -uxo pipefail

./nocache-hdd.sh 3 32
./nocache-hdd.sh 3 64
./nocache-hdd.sh 3 128
./nocache-hdd.sh 4 1
./nocache-hdd.sh 4 2
./nocache-hdd.sh 4 4
