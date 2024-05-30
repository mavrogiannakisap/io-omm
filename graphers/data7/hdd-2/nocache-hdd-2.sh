#!/bin/bash
set -uxo pipefail

./nocache-hdd.sh 2 256
./nocache-hdd.sh 2 512
./nocache-hdd.sh 2 1024
