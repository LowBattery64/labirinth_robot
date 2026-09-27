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

CAMERA_IP="${CAMERA_IP:-10.109.150.34}"
CAMERA_PORT="${CAMERA_PORT:-5557}"
VIDEO_PORT="${VIDEO_PORT:-5001}"
VIDEO_CONTROL_PORT="${VIDEO_CONTROL_PORT:-5002}"

SERIAL_DEVICE="${SERIAL_DEVICE:-/dev/ttyUSB0}"

RECORD_DIR="${RECORD_DIR:-/media/raspberry/76E8-CACF/omegabot_recordings}"


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
    echo "        Проверь USB-подключение Arduino к Raspberry Pi."
    exit 1
fi

echo "       Arduino: $SERIAL_DEVICE"


echo "[3/5] Подготовка видеоканала..."

echo "       Запись запускается кнопкой оператора."
echo "       Каталог записи: $RECORD_DIR"


echo "[4/5] Компиляция и запуск labirinth_server..."

g++ -std=c++17 "$SERVER_SOURCE" -o "$SERVER_BINARY"

if is_running "$SERVER_PID_FILE"; then
    echo "       labirinth_server уже запущен."
else
    nohup "$SERVER_BINARY" \
        </dev/null \
        >"$SERVER_LOG" 2>&1 &

    echo $! >"$SERVER_PID_FILE"

    sleep 1

    if ! is_running "$SERVER_PID_FILE"; then
        echo "[ERROR] labirinth_server завершился сразу после запуска."
        echo "        Лог:"
        tail -30 "$SERVER_LOG"
        exit 1
    fi

    echo "       labirinth_server запущен."
fi


echo "[5/5] Запуск camera_bridge..."

CAMERA_ARGS=(
    --camera-ip "$CAMERA_IP"
    --camera-port "$CAMERA_PORT"
    --tcp-port "$VIDEO_PORT"
    --control-port "$VIDEO_CONTROL_PORT"
    --record-to "$RECORD_DIR"
)


if is_running "$CAMERA_PID_FILE"; then
    echo "       camera_bridge уже запущен."
else
    nohup python3 "$CAMERA_SCRIPT" \
        "${CAMERA_ARGS[@]}" \
        </dev/null \
        >"$CAMERA_LOG" 2>&1 &

    echo $! >"$CAMERA_PID_FILE"

    sleep 2

    if ! is_running "$CAMERA_PID_FILE"; then
        echo "[ERROR] camera_bridge завершился сразу после запуска."
        echo "        Лог:"
        tail -30 "$CAMERA_LOG"
        exit 1
    fi

    echo "       camera_bridge запущен."
fi


echo
echo "========================================"
echo " OmegaBot запущен"
echo "========================================"
echo " Raspberry: 10.109.150.232"
echo " Control:   TCP 5000"
echo " Video:     TCP $VIDEO_PORT"
echo " Video ctl: TCP $VIDEO_CONTROL_PORT"
echo " Camera:    $CAMERA_IP:$CAMERA_PORT"
echo " Serial:    $SERIAL_DEVICE"
echo " Record:    по кнопке оператора -> $RECORD_DIR"

echo "========================================"
