#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$HOME/labirinth_robot"

VIDEO_PID_FILE="$REPO_DIR/.usb_camera_server.pid"
SERVER_PID_FILE="$REPO_DIR/.labirinth_server.pid"

stop_process() {
    local name="$1"
    local pid_file="$2"

    if [[ ! -f "$pid_file" ]]; then
        echo "$name: не запущен."
        return
    fi

    local pid
    pid="$(cat "$pid_file")"

    if kill -0 "$pid" 2>/dev/null; then
        kill "$pid"

        for _ in {1..10}; do
            if ! kill -0 "$pid" 2>/dev/null; then
                break
            fi
            sleep 0.5
        done

        if kill -0 "$pid" 2>/dev/null; then
            echo "$name: принудительное завершение."
            kill -9 "$pid"
            sleep 0.5
        fi

        echo "$name: остановлен."
    else
        echo "$name: процесс уже завершён."
    fi

    rm -f "$pid_file"
}

stop_process "usb_camera_server" "$VIDEO_PID_FILE"
stop_process "labirinth_server" "$SERVER_PID_FILE"

echo "OmegaBot USB camera fallback остановлен."
