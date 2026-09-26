#!/usr/bin/env bash
set -euo pipefail
REPO_DIR="$HOME/labirinth_robot"
SERVER_SOURCE="$REPO_DIR/labirinth_server.cpp"
SERVER_BINARY="$REPO_DIR/labirinth_server"
SERVER_LOG="$REPO_DIR/server_console.log"
SERVER_PID_FILE="$REPO_DIR/.labirinth_server.pid"
CAMERA_SCRIPT="$REPO_DIR/camera_bridge.py"
CAMERA_LOG="$REPO_DIR/camera_console.log"
CAMERA_PID_FILE="$REPO_DIR/.camera_bridge.pid"
CAMERA_IP="\${CAMERA_IP:-10.109.150.34}"
CAMERA_PORT="\${CAMERA_PORT:-5557}"
VIDEO_PORT="\${VIDEO_PORT:-5001}"
SERIAL_DEVICE="\${SERIAL_DEVICE:-/dev/ttyUSB0}"
RECORD_VIDEO="\${RECORD_VIDEO:-1}"
RECORD_DIR="\${RECORD_DIR:-/media/raspberry/76E8-CACF/omegabot_recordings}"

is_running() {
    local pid_file="$1"
    if [[ ! -f "$pid_file" ]]; then return 1; fi
    local pid
    pid="$(cat "$pid_file")"
    if kill -0 "$pid" 2>/dev/null; then return 0; fi
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
    echo "        Проверь USB-подключение Arduino к Raspberry Pi."
    exit 1
fi

echo "[3/5] Компиляция labirinth_server..."
g++ -std=c++17 "$SERVER_SOURCE" -o "$SERVER_BINARY"

if is_running "$SERVER_PID_FILE"; then
    echo "       Сервер уже запущен."
else
    echo "[4/5] Запуск labirinth_server..."
    nohup "$SERVER_BINARY" </dev/null >"$SERVER_LOG" 2>&1 &
    echo $! >"$SERVER_PID_FILE"
fi

echo "[5/5] Запуск camera_bridge..."
if [[ "$RECORD_VIDEO" == "1" ]]; then
    if ! mountpoint -q "$(dirname "$RECORD_DIR")"; then
        echo "[ERROR] USB-накопитель не смонтирован: $(dirname "$RECORD_DIR")"
        echo "        Для запуска без записи установи RECORD_VIDEO=0."
        exit 1
    fi
    mkdir -p "$RECORD_DIR"
    CAMERA_ARGS=(--camera-ip "$CAMERA_IP" --camera-port "$CAMERA_PORT" --tcp-port "$VIDEO_PORT" --record-to "$RECORD_DIR")
else
    CAMERA_ARGS=(--camera-ip "$CAMERA_IP" --camera-port "$CAMERA_PORT" --tcp-port "$VIDEO_PORT")
fi

if is_running "$CAMERA_PID_FILE"; then
    echo "       camera_bridge уже запущен."
else
    nohup python3 "$CAMERA_SCRIPT" "\${CAMERA_ARGS[@]}" </dev/null >"$CAMERA_LOG" 2>&1 &
    echo $! >"$CAMERA_PID_FILE"
fi

echo
echo "OmegaBot на Raspberry Pi запущен."
echo "Control: TCP 5000"
echo "Video:   TCP $VIDEO_PORT"
echo "Camera:  $CAMERA_IP:$CAMERA_PORT"
if [[ "$RECORD_VIDEO" == "1" ]]; then echo "Record:  $RECORD_DIR"; else echo "Record:  отключена"; fi
