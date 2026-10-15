# Скрипты запуска Goslingmobile

Скрипты предназначены для запуска и остановки серверных процессов на Raspberry Pi. Рабочий каталог по умолчанию — `~/labirinth_robot`.

## Актуальный запуск: USB-камера

Использовать этот вариант для текущей конфигурации с USB-камерой, подключённой к Raspberry Pi.

```bash
cd ~/labirinth_robot
chmod +x scripts/start_usb_camera_robot.sh scripts/stop_usb_camera_robot.sh
./scripts/start_usb_camera_robot.sh
```

Скрипт проверяет наличие репозитория, Arduino и камеры `/dev/video0` (по умолчанию), компилирует `labirinth_server.cpp` и `usb_camera_server.cpp`, затем запускает оба процесса в фоне. Для сборки видеосервера требуются компилятор `g++`, `pkg-config` и OpenCV для C++:

```bash
sudo apt update
sudo apt install -y g++ pkg-config libopencv-dev
```

Остановка:

```bash
./scripts/stop_usb_camera_robot.sh
```

## Параметры USB-камеры

Параметры можно задать перед запуском:

| Переменная | По умолчанию | Назначение |
|---|---|---|
| `USB_CAMERA_INDEX` | `0` | Индекс камеры: `0` соответствует `/dev/video0` |
| `VIDEO_PORT` | `5001` | TCP-порт видеопотока |
| `VIDEO_CONTROL_PORT` | `5002` | TCP-порт управления качеством/записью |
| `RECORD_DIR` | `/media/raspberry/76E8-CACF/omegabot_recordings` | Каталог видеозаписей |
| `RECORD_SEGMENT_SECONDS` | `600` | Длительность сегмента записи в секундах |
| `SERIAL_DEVICE` | `/dev/ttyUSB0` | Последовательный порт Arduino |

Например, выбрать вторую камеру:

```bash
USB_CAMERA_INDEX=1 ./scripts/start_usb_camera_robot.sh
```

Убедись, что USB-накопитель смонтирован, если записи должны сохраняться на него. Каталог записи скрипт создаёт автоматически, но сам факт монтирования накопителя не проверяет.

## Логи и PID-файлы

Все пути относятся к `~/labirinth_robot`.

| Файл | Назначение |
|---|---|
| `server_console.log` | Вывод сервера управления |
| `usb_camera_console.log` | Вывод USB-видеосервера |
| `.labirinth_server.pid` | PID сервера управления |
| `.usb_camera_server.pid` | PID USB-видеосервера |

При ошибке запуска сначала проверь соответствующий лог:

```bash
tail -n 50 ~/labirinth_robot/server_console.log
tail -n 50 ~/labirinth_robot/usb_camera_console.log
```

## Старый вариант: `start_robot.sh`

`start_robot.sh` и `stop_robot.sh` относятся к прежней схеме с `camera_bridge.py` и внешним сетевым источником камеры. В текущей ветке `Prod` файл `camera_bridge.py` в корне репозитория отсутствует, поэтому `start_robot.sh` не является актуальным способом запуска и может завершиться ошибкой. Для USB-камеры используй `start_usb_camera_robot.sh`.

В старом скрипте были предусмотрены переменные `CAMERA_IP`, `CAMERA_PORT`, `VIDEO_PORT`, `VIDEO_CONTROL_PORT`, `SERIAL_DEVICE` и `RECORD_DIR`; их описание оставлено только для обслуживания прежней конфигурации.

## Сеть

Сервер управления принимает TCP-подключения на порту `5000`. Видеопоток и управление видеозаписью используют порты `5001` и `5002` соответственно. IP Raspberry Pi зависит от настроек сети; адрес, напечатанный скриптом, является настройкой конкретной конфигурации, а не универсальным адресом.

## Ограничения

- Скрипты не прошивают Arduino.
- Arduino должна быть подключена и прошита совместимой версией `str02-controller.ino`.
- Устройство камеры должно быть доступно как `/dev/videoN`.
- Для записи на USB-накопитель он должен быть смонтирован в указанный каталог.
- Скрипты запускают процессы в фоне; для диагностики используй логи из таблицы выше.
