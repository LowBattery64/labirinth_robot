# Скрипты запуска OmegaBot

Папка содержит скрипты для автоматизированного запуска и остановки текущей системы.

## Состав

### start_robot.sh

Запускается на Raspberry Pi.

Выполняет:
1. проверяет репозиторий;
2. проверяет Arduino в /dev/ttyUSB0;
3. компилирует labirinth_server.cpp;
4. запускает labirinth_server;
5. запускает camera_bridge.py;
6. включает запись видео на USB-накопитель.

Процессы запускаются в фоне.

Логи:
    ~/labirinth_robot/server_console.log
    ~/labirinth_robot/camera_console.log

PID-файлы:
    ~/labirinth_robot/.labirinth_server.pid
    ~/labirinth_robot/.camera_bridge.pid

Запись по умолчанию:
    /media/raspberry/76E8-CACF/omegabot_recordings

Без записи:
    RECORD_VIDEO=0 bash scripts/start_robot.sh

Параметры можно переопределить через CAMERA_IP, CAMERA_PORT, VIDEO_PORT, SERIAL_DEVICE и RECORD_DIR.

### stop_robot.sh

Останавливает camera_bridge.py и labirinth_server на Raspberry Pi.

    bash scripts/stop_robot.sh

### start_omegabot.ps1

Основной скрипт запуска с Windows ПК.

Он:
1. проверяет SSH к Raspberry Pi;
2. запускает scripts/start_robot.sh;
3. ждёт TCP 5000 и 5001;
4. открывает страницу TrackingCam3;
5. при необходимости собирает operator_ui;
6. запускает OmegaBotOperator.exe.

Запуск:
    .\scripts\start_omegabot.ps1

Принудительная пересборка:
    .\scripts\start_omegabot.ps1 -Build

### stop_omegabot.ps1

Останавливает Operator UI и процессы робота.

    .\scripts\stop_omegabot.ps1

# Первый запуск

## SSH

Должна работать команда:

    ssh raspberry@10.109.150.232

Для автоматического запуска нужен SSH-ключ без запроса пароля.

Проверка:
    ssh -o BatchMode=yes raspberry@10.109.150.232 "echo SSH_OK"

## ПК

Нужны:
- OpenSSH Client;
- CMake;
- Visual Studio 2022 с C++;
- Qt 6 MSVC 2022 x64.

## Raspberry Pi

Нужны:
- g++;
- Python 3;
- Python OpenCV;
- подключённая Arduino;
- смонтированный USB-накопитель для записи.

## Запуск

Из корня репозитория:
    .\scripts\start_omegabot.ps1

После запуска Raspberry Pi собирает и запускает сервер, запускается видеомост, браузер открывает страницу камеры, затем запускается Operator UI.

# Ограничения

- IP Raspberry Pi и камеры прописаны в скриптах;
- путь USB-накопителя прописан в start_robot.sh;
- нужен SSH-ключ;
- Arduino должна быть уже прошита;
- камера пока требует открытия веб-страницы для активации публикации видеопотока;
- скрипты не прошивают Arduino;
- Operator UI пересобирается только при отсутствии .exe или с параметром -Build.
