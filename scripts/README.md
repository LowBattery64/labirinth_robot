# Скрипты запуска Робота

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


# Первый запуск

## SSH

Должна работать команда:

    ssh raspberry@10.109.150.232

Для автоматического запуска нужен SSH-ключ без запроса пароля.

Проверка:
    ssh -o BatchMode=yes raspberry@10.109.150.232 "echo SSH_OK"


## Raspberry Pi

Нужны:
- g++;
- Python 3;
- Python OpenCV;
- подключённая Arduino;
- смонтированный USB-накопитель для записи.


# Ограничения

- IP Raspberry Pi и камеры прописаны в скриптах;
- путь USB-накопителя прописан в start_robot.sh;
- нужен SSH-ключ;
- Arduino должна быть уже прошита;
- камера пока требует открытия веб-страницы для активации публикации видеопотока;
- скрипты не прошивают Arduino;
- Operator UI пересобирается только при отсутствии .exe или с параметром -Build.
