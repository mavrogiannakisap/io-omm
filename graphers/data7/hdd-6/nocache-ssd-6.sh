#!/bin/bash
set -uxo pipefail

./nocache-ssd.sh 6 8
./nocache-ssd.sh 8 1
./nocache-ssd.sh 8 2
./nocache-ssd.sh 8 4
./nocache-ssd.sh 12 1
./nocache-ssd.sh 12 2
./nocache-ssd.sh 24 1
