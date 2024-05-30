#!/bin/bash
set -uxo pipefail

./nocache-ssd.sh 2 2048
./nocache-ssd.sh 3 1
./nocache-ssd.sh 3 2
./nocache-ssd.sh 3 4
./nocache-ssd.sh 3 8
./nocache-ssd.sh 3 16
