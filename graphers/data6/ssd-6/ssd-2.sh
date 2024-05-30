#!/bin/bash
set -uxo pipefail

./ssd.sh 2 32
./ssd.sh 2 64
./ssd.sh 2 128
./ssd.sh 2 256
./ssd.sh 2 512
./ssd.sh 2 1024
