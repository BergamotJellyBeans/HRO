#!/bin/bash

set -e

HRO_ROOT="/mnt/hro/development/HRO"
PID_DIR="$HRO_ROOT/run"

WEB_PID_FILE="$PID_DIR/hro-web.pid"
PNG_PID_FILE="$PID_DIR/hro-png.pid"
ENGINE_PID_FILE="$PID_DIR/hro-engine.pid"

cd "$HRO_ROOT"

mkdir -p "$PID_DIR"


is_running()
{
    local pid_file="$1"

    if [ ! -f "$pid_file" ]; then
        return 1
    fi

    local pid
    pid=$(cat "$pid_file")

    if kill -0 "$pid" 2>/dev/null; then
        return 0
    fi

    return 1
}


start_hro()
{
    if is_running "$WEB_PID_FILE" ||
       is_running "$PNG_PID_FILE" ||
       is_running "$ENGINE_PID_FILE"; then
        echo "Pi5-HRO is already running."
        status_hro
        return 1
    fi

    rm -f "$WEB_PID_FILE" \
          "$PNG_PID_FILE" \
          "$ENGINE_PID_FILE"

    echo "Starting Pi5-HRO..."

    echo "Starting hro-web..."
    nohup ./build/app/pi/hro-web/hro-web \
        > "$PID_DIR/hro-web.log" 2>&1 &
    echo $! > "$WEB_PID_FILE"

    sleep 1

    echo "Starting hro-png..."
    nohup ./build/app/pi/hro-png/hro-png \
        > "$PID_DIR/hro-png.log" 2>&1 &
    echo $! > "$PNG_PID_FILE"

    sleep 1

    echo "Starting hro-engine..."
    nohup ./build/app/pi/hro-engine/hro-engine \
        > "$PID_DIR/hro-engine.log" 2>&1 &
    echo $! > "$ENGINE_PID_FILE"

    sleep 1

    echo
    echo "Pi5-HRO started."
    status_hro
}


stop_process()
{
    local name="$1"
    local pid_file="$2"

    if ! is_running "$pid_file"; then
        echo "$name: not running"
        rm -f "$pid_file"
        return
    fi

    local pid
    pid=$(cat "$pid_file")

    echo "Stopping $name (PID=$pid)..."

    kill "$pid" 2>/dev/null || true

    for i in {1..20}; do
        if ! kill -0 "$pid" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done

    if kill -0 "$pid" 2>/dev/null; then
        echo "$name did not stop - sending SIGKILL"
        kill -9 "$pid" 2>/dev/null || true
    fi

    rm -f "$pid_file"
}


stop_hro()
{
    echo "Stopping Pi5-HRO..."

    stop_process "hro-engine" "$ENGINE_PID_FILE"
    stop_process "hro-png"    "$PNG_PID_FILE"
    stop_process "hro-web"    "$WEB_PID_FILE"

    echo "Pi5-HRO stopped."
}


status_process()
{
    local name="$1"
    local pid_file="$2"

    if is_running "$pid_file"; then
        echo "$name: RUNNING (PID=$(cat "$pid_file"))"
    else
        echo "$name: STOPPED"
    fi
}


status_hro()
{
    echo "Pi5-HRO status:"
    status_process "hro-web"    "$WEB_PID_FILE"
    status_process "hro-png"    "$PNG_PID_FILE"
    status_process "hro-engine" "$ENGINE_PID_FILE"
}


case "${1:-}" in

    start)
        start_hro
        ;;

    stop)
        stop_hro
        ;;

    restart)
        stop_hro
        sleep 1
        start_hro
        ;;

    status)
        status_hro
        ;;

    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
        ;;

esac