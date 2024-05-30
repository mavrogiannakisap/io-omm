#!/bin/bash
set -uxo pipefail

./nocache-hdd.sh 2 2048
./nocache-hdd.sh 3 1
./nocache-hdd.sh 3 2
./nocache-hdd.sh 3 4
./nocache-hdd.sh 3 8
./nocache-hdd.sh 3 16
