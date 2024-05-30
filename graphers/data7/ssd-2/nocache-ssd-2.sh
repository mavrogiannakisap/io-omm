#!/bin/bash
set -uxo pipefail

./nocache-ssd.sh 2 32
./nocache-ssd.sh 2 64
./nocache-ssd.sh 2 128
./nocache-ssd.sh 2 256
./nocache-ssd.sh 2 512
./nocache-ssd.sh 2 1024
