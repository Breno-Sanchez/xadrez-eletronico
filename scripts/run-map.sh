#!/usr/bin/env bash
set -eu

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PORT="${ESPPORT:-/dev/ttyACM0}"
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf/export.sh}"
LOG_FILE="/tmp/xadrez_led_map_$(date +%Y%m%d_%H%M%S).log"

if [ ! -f "$IDF_EXPORT" ]; then
    echo "Error: ESP-IDF export script not found: $IDF_EXPORT"
    echo "Use: IDF_EXPORT=/path/to/export.sh run map"
    exit 1
fi

if [ ! -e "$PORT" ]; then
    echo "Error: serial port $PORT was not found"
    find /dev -maxdepth 1 \( -name 'ttyACM*' -o -name 'ttyUSB*' \) -ls 2>/dev/null || true
    echo "Use: ESPPORT=/dev/ttyUSB0 run map"
    exit 1
fi

. "$IDF_EXPORT" >/dev/null

cd "$PROJECT_ROOT"

git switch Breno >/dev/null 2>&1 || git checkout Breno >/dev/null 2>&1 || true

XADREZ_MAP_MODE=1 idf.py set-target esp32s3
XADREZ_MAP_MODE=1 idf.py fullclean
XADREZ_MAP_MODE=1 idf.py build
XADREZ_MAP_MODE=1 idf.py -p "$PORT" flash

echo
echo "LED mapper monitor will open now."
echo "Use a/r/b/q inside the monitor."
echo "After q or after finishing all squares, press Ctrl+] to close the monitor."
echo "The monitor log will be saved at: $LOG_FILE"
echo

script -q -f -c "idf.py -p $PORT monitor" "$LOG_FILE"

mkdir -p "$PROJECT_ROOT/build/tools"
cc -std=c17 -Wall -Wextra -Werror -O2 \
    -o "$PROJECT_ROOT/build/tools/parse_led_map" \
    "$PROJECT_ROOT/tools/parse_led_map.c"
"$PROJECT_ROOT/build/tools/parse_led_map" "$LOG_FILE"
