#!/bin/bash
set -uxo pipefail

./nocache-hdd.sh 4 8
./nocache-hdd.sh 4 16
./nocache-hdd.sh 4 32
./nocache-hdd.sh 6 1
./nocache-hdd.sh 6 2
./nocache-hdd.sh 6 4
