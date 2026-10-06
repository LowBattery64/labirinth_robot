#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$HOME/labirinth_robot"

SERVER_SOURCE="$REPO_DIR/labirinth_server.cpp"
SERVER_BINARY="$REPO_DIR/labirinth_server"
SERVER_LOG="$REPO_DIR/server_console.log"
SERVER_PID_FILE="$REPO_DIR/.labirinth_server.pid"

VIDEO_SOURCE="$REPO_DIR/usb_camera_server.cpp"
VIDEO_BINARY="$REPO_DIR/usb_camera_server"
VIDEO_LOG="$REPO_DIR/usb_camera_console.log"
VIDEO_PID_FILE="$REPO_DIR/.usb_camera_server.pid"

USB_CAMERA_INDEX="${USB_CAMERA_INDEX:-0}"
VIDEO_PORT="${VIDEO_PORT:-5001}"
VIDEO_CONTROL_PORT="${VIDEO_CONTROL_PORT:-5002}"
RECORD_DIR="${RECORD_DIR:-/media/raspberry/76E8-CACF/omegabot_recordings}"
RECORD_SEGMENT_SECONDS="${RECORD_SEGMENT_SECONDS:-600}"
SERIAL_DEVICE="${SERIAL_DEVICE:-/dev/ttyUSB0}"

is_running() {
    local pid_file="$1"

    if [[ ! -f "$pid_file" ]]; then
        return 1
    fi

    local pid
    pid="$(cat "$pid_file")"

    if kill -0 "$pid" 2>/dev/null; then
        return 0
    fi

    rm -f "$pid_file"
    return 1
}

echo "[1/5] Проверка репозитория..."

if [[ ! -d "$REPO_DIR" ]]; then
    echo "[ERROR] Репозиторий не найден: $REPO_DIR"
    exit 1
fi

echo "[2/5] Проверка Serial..."

if [[ ! -e "$SERIAL_DEVICE" ]]; then
    echo "[ERROR] Arduino не найдена: $SERIAL_DEVICE"
    exit 1
fi

echo "       Arduino: $SERIAL_DEVICE"

echo "[3/5] Проверка USB-камеры..."

USB_DEVICE="/dev/video$USB_CAMERA_INDEX"

if [[ ! -e "$USB_DEVICE" ]]; then
    echo "[ERROR] USB-камера не найдена: $USB_DEVICE"
    echo "        Проверь подключение камеры и доступные /dev/video*."
    exit 1
fi

echo "       Камера: $USB_DEVICE"

echo "[4/5] Компиляция и запуск labirinth_server..."

g++ -std=c++17 "$SERVER_SOURCE" -o "$SERVER_BINARY"

if is_running "$SERVER_PID_FILE"; then
    echo "       labirinth_server уже запущен."
else
    nohup "$SERVER_BINARY" </dev/null >"$SERVER_LOG" 2>&1 &
    echo $! >"$SERVER_PID_FILE"
    sleep 1

    if ! is_running "$SERVER_PID_FILE"; then
        echo "[ERROR] labirinth_server завершился."
        tail -30 "$SERVER_LOG"
        exit 1
    fi
fi

echo "[5/5] Компиляция и запуск USB camera server..."

g++ -std=c++17 "$VIDEO_SOURCE" -o "$VIDEO_BINARY" \
    $(pkg-config --cflags --libs opencv4)

if is_running "$VIDEO_PID_FILE"; then
    echo "       usb_camera_server уже запущен."
else
    mkdir -p "$RECORD_DIR"

    nohup "$VIDEO_BINARY" \
        "$USB_CAMERA_INDEX" \
        "$RECORD_DIR" \
        "$RECORD_SEGMENT_SECONDS" \
        </dev/null \
        >"$VIDEO_LOG" 2>&1 &

    echo $! >"$VIDEO_PID_FILE"
    sleep 2

    if ! is_running "$VIDEO_PID_FILE"; then
        echo "[ERROR] usb_camera_server завершился."
        tail -30 "$VIDEO_LOG"
        exit 1
    fi
fi

echo
echo "========================================"
echo " OmegaBot — USB camera fallback"
echo "========================================"
echo " Raspberry: 10.109.150.232"
echo " Control:   TCP 5000"
echo " Video:     TCP $VIDEO_PORT"
echo " Video ctl: TCP $VIDEO_CONTROL_PORT"
echo " USB cam:   $USB_DEVICE"
echo " Record:    $RECORD_DIR"
echo "========================================"
