#!/bin/bash
set -uxo pipefail

./nocache-hdd.sh 1 1
./nocache-hdd.sh 2 1
./nocache-hdd.sh 2 2
./nocache-hdd.sh 2 4
./nocache-hdd.sh 2 8
./nocache-hdd.sh 2 16
