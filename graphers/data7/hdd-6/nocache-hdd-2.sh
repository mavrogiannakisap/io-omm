#!/bin/bash
set -uxo pipefail

./nocache-hdd.sh 2 32
./nocache-hdd.sh 2 64
./nocache-hdd.sh 2 128
./nocache-hdd.sh 2 256
./nocache-hdd.sh 2 512
./nocache-hdd.sh 2 1024
