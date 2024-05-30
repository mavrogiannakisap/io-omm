#!/bin/bash
set -uxo pipefail

./ssd.sh 2 2048
./ssd.sh 3 1
./ssd.sh 3 2
./ssd.sh 3 4
./ssd.sh 3 8
./ssd.sh 3 16
