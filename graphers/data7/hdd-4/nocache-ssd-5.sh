#!/bin/bash
set -uxo pipefail

./nocache-ssd.sh 4 8
./nocache-ssd.sh 4 16
./nocache-ssd.sh 4 32
./nocache-ssd.sh 6 1
./nocache-ssd.sh 6 2
./nocache-ssd.sh 6 4
