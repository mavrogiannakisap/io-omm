#!/bin/bash
set -uxo pipefail

./ssd.sh 4 8
./ssd.sh 4 16
./ssd.sh 4 32
./ssd.sh 6 1
./ssd.sh 6 2
./ssd.sh 6 4
