#!/bin/bash
set -uxo pipefail

./nocache-ssd.sh 3 32
./nocache-ssd.sh 3 64
./nocache-ssd.sh 3 128
./nocache-ssd.sh 4 1
./nocache-ssd.sh 4 2
./nocache-ssd.sh 4 4
