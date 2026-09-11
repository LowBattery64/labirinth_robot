#  OmegaBot — инструкция по запуску проекта

Полное руководство: от подготовки железа до управления роботом.
Все команды — для **Ubuntu / Raspberry Pi OS** и **Windows (cmd / PowerShell)**, где это применимо.

---

##  0. Архитектура проекта

```
┌──────────┐   USB    ┌──────────────┐   TCP:5000   ┌──────────┐
│ Arduino  │ ───────► │ Raspberry Pi │ ◄─────────── │    ПК    │
│  моторы  │          │   (сервер)   │  управление  │ (клиент) │
└──────────┘          │              │              │          │
                       │              │   TCP:5001   │          │
                       │  USB-камера  │ ───────────► │   видео  │
                       └──────────────┘   Wi-Fi      └──────────┘
 low_level.ino        labirinth_server.cpp          labirinth_client.cpp
                       video_server.cpp              video_client.cpp
```

Канал управления (порт 5000) и канал видео (порт 5001) — два независимых
TCP-соединения и два независимых процесса на каждой стороне. Видео можно
запускать, выключать и перезапускать, не трогая управление, и наоборот.

**Роли файлов:**

| Файл | Где запускается | Что делает |
|---|---|---|
| `low_level.ino` | Arduino | Читает Serial, крутит моторы |
| `labirinth_server.cpp` | Raspberry Pi | Принимает TCP-команды (порт 5000), шлёт их в Arduino |
| `labirinth_client.cpp` | ПК (Windows) | Читает клавиатуру, шлёт команды по TCP |
| `labirinth_server_with_joystick.cpp` | ПК (Windows) | То же самое, но управление с геймпада (XInput) |
| `video_server.cpp` | Raspberry Pi | Захватывает кадры с USB-камеры, шлёт JPEG по TCP (порт 5001) |
| `video_client.cpp` | ПК (Windows) | Принимает JPEG-кадры и показывает окно с видео |

---

## 🛠 1. Подготовка (один раз)

### 1.1. Что должно быть

* Arduino (Uno / Nano / Leonardo) + драйвер моторов + 2 мотора.
* Raspberry Pi с Raspberry Pi OS, подключённая к той же Wi-Fi сети, что и ПК.
* ПК с Windows и компилятором `g++` (MinGW).
* USB-кабель **data** (не «только зарядка»).
* Все устройства в одной подсети (например, `192.168.1.x` или `10.122.144.x`).
* (Для видео) USB-веб-камера, подключённая к Raspberry Pi.
* (Для видео) Установленный OpenCV — на плате и на ПК (см. раздел 7.5).



## 📦 1. Прошиваем Arduino

### 2.1. Подключить Arduino к ПК

Обычным USB-кабелем. В диспетчере устройств появится COM-порт (например, `COM3`).

### 2.2. Загрузить `low_level.ino`

1. Открыть файл в **Arduino IDE**.
2. **Tools → Board → Arduino Uno** (или ваша плата).
3. **Tools → Port → COM…** (тот, что появился).
4. **Upload** (стрелка вправо). Дождаться `Done uploading`.


---

## 🍓 3. Подключаем Arduino к Raspberry Pi

### 3.1. Вставить USB-кабель в плату

Тот же кабель, что был в ПК, — в USB-порт платы.

### 3.2. Подключиться к платы по SSH

С ПК (PowerShell / cmd):

```cmd
ssh raspberry@<IP_ПЛАТЫ>
```

Если IP неизвестен — УЗНАЙ ЧЕРЕЗ УСТРОЙСТВО НА КОТОРОМ РАЗДАЕШЬ СЕТЬ.

### 3.3. Проверить, что плата видит Arduino

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Возможные результаты:

| Вывод | Что значит |
|---|---|
| `/dev/ttyACM0` | Оригинальная Arduino (Uno R3, Leonardo) |
| `/dev/ttyUSB0` | Клон на CH340 / CP2102 |
| Пусто | Плата не видна → см. п. 3.4 |


### 3.5. Дать пользователю доступ к Serial

```bash
groups
```

Если в выводе **нет** `dialout`:

```bash
sudo usermod -a -G dialout $USER
```

Затем **перезайдите по SSH** (или перезагрузите плату), чтобы изменения применились.

Проверка:

```bash
groups
# должно быть: raspberry adm dialout ...
```

---

##  4. Готовим рабочую директорию на плате

### 4.1. Скопировать папку проекта

```bash
git clone https://github.com/LowBattery64/labirinth_robot
cd ~/labirinth_robot
```


### 4.2. Проверить, что файл на месте

На малине:

```bash
ls -la ~/labirinth_robot
```

Должен быть `labirinth_server.cpp`.

---

## ⚙️ 5. Собираем сервер на Raspberry Pi

### 5.1. Проверить имя Serial-порта в коде

```bash
nano ~/labirinth_robot/labirinth_server.cpp
```

Найдите строку:

```cpp
static constexpr const char* SERIAL_PORT = "/dev/ttyACM0";
```

Убедитесь, что путь **совпадает** с тем, что вы увидели в п. 3.3. Если у вас `/dev/ttyUSB0` — исправьте.

Сохранить и выйти из nano: `Ctrl+O`, `Enter`, `Ctrl+X`.

### 5.2. Скомпилировать

```bash
cd ~/labirinth_robot
g++ -std=c++17 -Wall -Wextra -o labirinth_server labirinth_server.cpp
```

Ошибок быть не должно. В папке появится бинарник `labirinth_server`.



## 🚀 6. Запускаем сервер на Raspberry Pi

```bash
cd ~/labirinth_robot
./labirinth_server
```

Ожидаемый вывод:

```
OmegaBot Raspberry Pi Server

Serial открыт: /dev/ttyACM0
TCP сервер запущен на порту 5000
Ожидание подключения PC...
```

Сервер **висит** и ждёт клиента. **Не закрывайте это окно.**

Если вместо `Serial открыт` — ошибка:

* `No such file or directory` → порт не найден, вернитесь к п. 3.
* `Permission denied` → пользователь не в группе `dialout`, см. п. 3.5.

---

## 💻 7. Собираем клиент на ПК

### 7.1. Открыть терминал в папке с `labirinth_client.cpp`

```powershell
cd путь\к\папке\с\клиентом
```

### 7.2. Скомпилировать

```powershell
g++ -std=c++17 -Wall -Wextra -o labirinth_client.exe labirinth_client.cpp -lws2_32
```


---

## 📷 7.5. Видео с робота (опционально, но по умолчанию — да)

Видео идёт по **отдельному** каналу (порт 5001) и **отдельными**
программами — `video_server.cpp` / `video_client.cpp`. Управление
(`labirinth_server` / `labirinth_client`) при этом работает точно
так же, как раньше, и его можно использовать без видео вообще.

### 7.5.1. Подключить USB-камеру к Raspberry Pi

Обычная USB-веб-камера. Проверить, что плата её видит:

```bash
ls /dev/video*
```

Если устройство не `/dev/video0` — в `video_server.cpp` в классе
`VideoSettings` поменять `CAMERA_INDEX` на нужный номер.

### 7.5.2. Установить OpenCV на Raspberry Pi

```bash
sudo apt update
sudo apt install -y libopencv-dev pkg-config
```

### 7.5.3. Собрать видео-сервер на Raspberry Pi

```bash
cd ~/labirinth_robot
g++ -std=c++17 -Wall -Wextra -o video_server video_server.cpp \
    $(pkg-config --cflags --libs opencv4)
```

> Если `pkg-config` не находит `opencv4` (бывает на некоторых
> версиях Raspberry Pi OS), собрать так:
> ```bash
> g++ -std=c++17 -Wall -Wextra -I/usr/include/opencv4 -o video_server \
>     video_server.cpp -lopencv_core -lopencv_imgcodecs -lopencv_videoio
> ```

### 7.5.4. Установить OpenCV на ПК (Windows)

Проще всего через [vcpkg](https://github.com/microsoft/vcpkg):

```powershell
git clone https://github.com/microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg install opencv:x64-windows
```

### 7.5.5. Собрать видео-клиент на ПК

```powershell
g++ -std=c++17 -Wall -Wextra -o video_client.exe video_client.cpp -lws2_32 ^
    -I <путь_к_vcpkg>\installed\x64-windows\include ^
    -L <путь_к_vcpkg>\installed\x64-windows\lib ^
    -lopencv_core4 -lopencv_imgcodecs4 -lopencv_highgui4
```

(точные имена `-lopencv_coreXXX` зависят от версии OpenCV — посмотреть
в `<vcpkg>\installed\x64-windows\lib`).

### 7.5.6. Запуск

На Raspberry Pi (в отдельном окне/сессии SSH от `labirinth_server`):

```bash
cd ~/labirinth_robot
./video_server
```

На ПК (в отдельном окне от `labirinth_client.exe`):

```cmd
video_client.exe IP_ПЛАТЫ
```

Откроется окно с видео. `Q` или `Esc` — закрыть. Если видео пропало,
а управление работает — проблема только в камере/видеоканале,
на управление это не влияет.

---

## 🎮 8. Запускаем клиент и управляем

### 8.1. Запустить клиент с IP платы

```cmd
labirinth_client.exe IP
```

(подставьте ваш IP из п. 5.3)

Ожидаемый вывод:

```
Подключено к 192.168.1.55:5000

Управление:
  W или F — вперёд
  S или B — назад
  A или L — влево
  D или R — вправо
  Пробел  — стоп
  Q       — выход
```

В окне сервера на плате появится:

```
PC подключён: 192.168.1.100
```

— цепочка собрана.

### 8.2. Управление

| Клавиша | Команда | Действие |
|---|---|---|
| `W` / `F` | `F` | Вперёд |
| `S` / `B` | `B` | Назад |
| `A` / `L` | `L` | Влево |
| `D` / `R` | `R` | Вправо |
| Пробел | `S` | Стоп |
| `Q` | — | Выход из клиента |


### 8.3. Что видно в окне сервера

При каждом нажатии:

```
Команда: F
Команда: L
Команда: S
```

Если команды появляются, а моторы не крутятся → проблема на стороне Arduino (пины, драйвер, питание моторов).

---

## 🛑 9. Остановка всего

### 9.1. Остановить клиент

В окне клиента нажать `Q` — отправит `S` и завершит программу.

Либо `Ctrl+C` в терминале клиента — принудительно.

### 9.2. Остановить сервер на плате

`Ctrl+C` в окне, где запущен `./labirinth_server`.

Если сервер завис в фоне:

```bash
pgrep labirinth_server
kill <PID>
```

или

```bash
pkill labirinth_server
```

---



## 📋 11. Справочник команд Ubuntu / Raspberry Pi OS

### Навигация по директориям

```bash
pwd                  # текущая директория
ls                   # содержимое
ls -la               # содержимое + скрытые файлы
cd ..                # на уровень выше
cd ~                 # в домашнюю директорию
cd ~/labirinth_robot # в папку проекта
mkdir -p a/b/c       # создать папку с родителями
```

### Работа с файлами

```bash
nano имя_файла       # редактировать файл
cat имя_файла        # вывести содержимое
cp откуда куда       # копировать
mv откуда куда       # переместить/переименовать
rm имя_файла         # удалить файл
rm -r папка          # удалить папку
```

### Права на выполнение

```bash
chmod +x labirinth_server   # сделать исполняемым
./labirinth_server          # запустить
```

### Процессы

```bash
ps aux | grep labirinth_server   # найти процессы сервера
pgrep labirinth_server           # PID сервера
kill <PID>                       # убить по PID
pkill labirinth_server           # убить по имени
```

### Сеть

```bash
hostname -I          # IP платы
ip addr              # все интерфейсы
ss -ltn              # слушающие TCP-порты
ss -ltn | grep 5000  # слушает ли кто-то 5000
nc -zv <IP> 5000     # проверить доступность порта
```

### Serial-устройства

```bash
ls /dev/ttyACM*      # Arduino-порты
ls /dev/ttyUSB*      # USB-Serial-порты
ls -l /dev/ttyACM0   # права на порт
groups               # группы текущего пользователя
sudo usermod -a -G dialout $USER   # добавить в dialout
```

---

## 🪟 12. Справочник команд Windows (cmd / PowerShell)

### Навигация

```cmd
cd путь\к\папке     # перейти в папку
dir                  # содержимое
cd ..                # на уровень выше
```






---

## ⚠️ 14. Важные замечания

1. **Таймаут безопасности 500 мс.** Прошивка Arduino останавливает моторы, если команды не приходят дольше 0.5 с. Текущий клиент шлёт команду **только в момент нажатия**. Значит, робот дёрнется и встанет. Чтобы робот ехал, пока клавиша зажата, клиент нужно доработать — слать команду циклически каждые ~100 мс.

2. **Имена файлов.** `labirinth_server.cpp` — сервер для платы, `labirinth_client.cpp` — клиент для ПК. Не перепутайте при копировании и компиляции.

3. **Один клиент — один сервер.** Сервер обслуживает одно TCP-соединение за раз. Второй клиент не подключится, пока первый не отключится.

4. **Wi-Fi.** Если плата и ПК в разных подсетях (например, ПК в `192.168.1.x`, малина в `10.0.0.x`) — TCP-соединение не установится. Проверяйте `ping`.

5. **Питание моторов.** Драйвер моторов должен иметь **отдельное питание** — не от USB платы. Иначе моторы не потянут или плата уйдёт в перезагрузку.

6. **Видео — независимый канал.** `video_server.cpp` / `video_client.cpp` не знают о `labirinth_server.cpp` / `labirinth_client.cpp` и наоборот. Можно запускать видео без управления, управление без видео, перезапускать один канал, пока работает другой.

7. **Качество JPEG.** В `video_server.cpp`, класс `VideoSettings`, есть `JPEG_QUALITY` (сейчас 60). Это первый шаг к будущему требованию "управление загрузкой радиоканала" — понижая это число при слабом канале, можно уменьшить размер кадров ценой качества картинки. Сейчас значение статическое, менять нужно вручную и пересобирать.

8
---

