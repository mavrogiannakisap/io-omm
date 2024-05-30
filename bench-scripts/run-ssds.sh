#!/bin/bash

set -uxo pipefail

./ssd-append.sh
./ssd-omap.sh
./ssd-osm.sh
./ssd.sh 24 1
./ssd.sh 3 128
./ssd.sh 3 16
./ssd.sh 3 2
./ssd.sh 2 1
