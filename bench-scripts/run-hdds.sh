#!/bin/bash

set -uxo pipefail

./hdd-append.sh
./hdd-omap.sh
./hdd.sh 24 1
./hdd.sh 3 128
./hdd.sh 3 16
./hdd.sh 3 2
./hdd.sh 2 1
