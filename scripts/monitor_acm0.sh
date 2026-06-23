#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

source ~/esp/esp-idf/export.sh
idf.py -p /dev/ttyACM0 monitor
