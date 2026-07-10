#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ACTION="${1:-all}"
PORT="${ESPPORT:-/dev/ttyACM0}"
IDF_EXPORT="${IDF_EXPORT:-$HOME/esp/esp-idf/export.sh}"
LOG_DIR="${XADREZ_LOG_DIR:-$PROJECT_ROOT/logs}"
STAMP="$(date +%Y%m%d_%H%M%S)"
LOG_FILE="$LOG_DIR/xadrez_${ACTION}_${STAMP}.log"
LATEST_LOG="$LOG_DIR/latest_chess.log"

mkdir -p "$LOG_DIR"

log_step()
{
    printf '\n[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" | tee -a "$LOG_FILE"
}

run_logged()
{
    log_step "RUN: $*"
    "$@" 2>&1 | tee -a "$LOG_FILE"
}

check_port()
{
    if [ ! -e "$PORT" ]; then
        log_step "ERROR: serial port $PORT was not found"
        find /dev -maxdepth 1 \( -name 'ttyACM*' -o -name 'ttyUSB*' \) -ls 2>/dev/null | tee -a "$LOG_FILE" || true
        log_step "Use ESPPORT=/dev/ttyUSB0 run chess if needed"
        exit 1
    fi
}

if [ ! -f "$IDF_EXPORT" ]; then
    echo "Error: ESP-IDF export script not found: $IDF_EXPORT" | tee -a "$LOG_FILE"
    echo "Install ESP-IDF or run with IDF_EXPORT=/path/to/export.sh run chess" | tee -a "$LOG_FILE"
    exit 1
fi

. "$IDF_EXPORT" >/dev/null
cd "$PROJECT_ROOT"
ln -sfn "$LOG_FILE" "$LATEST_LOG"

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git switch Breno >/dev/null 2>&1 || git checkout Breno >/dev/null 2>&1 || true
fi

log_step "Xadrez command started"
{
    echo "Project: $PROJECT_ROOT"
    echo "Action:  $ACTION"
    echo "Port:    $PORT"
    echo "Log:     $LOG_FILE"
    echo "Branch:  $(git branch --show-current 2>/dev/null || echo unknown)"
    echo "IDF:     ${IDF_PATH:-unknown}"
} | tee -a "$LOG_FILE"

case "$ACTION" in
    build)
        run_logged idf.py set-target esp32s3
        run_logged idf.py fullclean
        run_logged idf.py build
        log_step "Build finished. Log saved at $LOG_FILE"
        ;;
    quick-build)
        run_logged idf.py set-target esp32s3
        run_logged idf.py build
        log_step "Quick build finished. Log saved at $LOG_FILE"
        ;;
    clean-build)
        run_logged idf.py set-target esp32s3
        run_logged idf.py fullclean
        run_logged idf.py build
        log_step "Clean build finished. Log saved at $LOG_FILE"
        ;;
    flash|all)
        check_port
        run_logged idf.py set-target esp32s3
        run_logged idf.py fullclean
        run_logged idf.py build
        run_logged idf.py -p "$PORT" flash
        log_step "Opening monitor. Press Ctrl+] to exit. Full log: $LOG_FILE"
        if command -v script >/dev/null 2>&1; then
            script -q -f -a "$LOG_FILE" -c "idf.py -p '$PORT' monitor"
        else
            idf.py -p "$PORT" monitor 2>&1 | tee -a "$LOG_FILE"
        fi
        ;;
    monitor)
        check_port
        log_step "Opening monitor only. Press Ctrl+] to exit. Full log: $LOG_FILE"
        if command -v script >/dev/null 2>&1; then
            script -q -f -a "$LOG_FILE" -c "idf.py -p '$PORT' monitor"
        else
            idf.py -p "$PORT" monitor 2>&1 | tee -a "$LOG_FILE"
        fi
        ;;
    log)
        echo "$LATEST_LOG"
        ;;
    *)
        echo "Usage: run chess [build|quick-build|clean-build|flash|monitor|log]"
        exit 2
        ;;
esac
