#!/bin/bash
set -uxo pipefail

./nocache-ssd.sh 1 1
./nocache-ssd.sh 2 1
./nocache-ssd.sh 2 2
./nocache-ssd.sh 2 4
./nocache-ssd.sh 2 8
./nocache-ssd.sh 2 16
