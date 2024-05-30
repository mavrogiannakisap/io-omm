#!/bin/bash
set -uxo pipefail

./ssd.sh 1 1
./ssd.sh 2 1
./ssd.sh 2 2
./ssd.sh 2 4
./ssd.sh 2 8
./ssd.sh 2 16
