#!/bin/bash
set -uxo pipefail

./ssd.sh 3 32
./ssd.sh 3 64
./ssd.sh 3 128
./ssd.sh 4 1
./ssd.sh 4 2
./ssd.sh 4 4
