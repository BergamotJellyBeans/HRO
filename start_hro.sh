#!/bin/bash

set -e

HRO_ROOT="/mnt/hro/development/HRO"

cd "$HRO_ROOT"

echo "Starting Pi5-HRO..."

echo "Starting hro-web..."
./build/app/pi/hro-web/hro-web &
WEB_PID=$!

sleep 1

echo "Starting hro-png..."
./build/app/pi/hro-png/hro-png &
PNG_PID=$!

sleep 1

echo "Starting hro-engine..."
./build/app/pi/hro-engine/hro-engine &
ENGINE_PID=$!

echo
echo "Pi5-HRO started"
echo "  hro-web    PID=$WEB_PID"
echo "  hro-png    PID=$PNG_PID"
echo "  hro-engine PID=$ENGINE_PID"
echo
echo "Press Ctrl+C to stop all processes."

cleanup()
{
    echo
    echo "Stopping Pi5-HRO..."

    kill "$ENGINE_PID" 2>/dev/null || true
    kill "$PNG_PID"    2>/dev/null || true
    kill "$WEB_PID"    2>/dev/null || true

    wait "$ENGINE_PID" 2>/dev/null || true
    wait "$PNG_PID"    2>/dev/null || true
    wait "$WEB_PID"    2>/dev/null || true

    echo "Pi5-HRO stopped."
}

trap cleanup INT TERM EXIT

wait
