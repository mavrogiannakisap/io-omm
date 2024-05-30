#!/bin/bash
set -uxo pipefail

./nocache-hdd.sh 6 8
./nocache-hdd.sh 8 1
./nocache-hdd.sh 8 2
./nocache-hdd.sh 8 4
./nocache-hdd.sh 12 1
./nocache-hdd.sh 12 2
./nocache-hdd.sh 24 1
