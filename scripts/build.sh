#!/usr/bin/env bash
set -e

cd "$(dirname "$0")/.."

source ~/esp/esp-idf/export.sh
idf.py set-target esp32
idf.py build
