# Команды проекта OmegaBot

## Перемещение между директориями

```bash
pwd
```
Показывает текущую директорию.

```bash
ls
```
Показывает содержимое текущей директории.

```bash
ls -la
```
Показывает содержимое текущей директории, включая скрытые файлы.

```bash
cd ..
```
Переходит на уровень выше.

```bash
cd ~
```
Переходит в домашнюю директорию пользователя.

```bash
cd /путь/к/директории
```
Переходит в указанную директорию.

```bash
cd ~/OmegaBot
```
Переходит в директорию проекта.

```bash
mkdir -p ~/OmegaBot
```
Создаёт директорию проекта вместе с отсутствующими родительскими директориями.

## Работа с файлами проекта

```bash
ls *.cpp
```
Показывает C++-файлы в текущей директории.

```bash
ls *.ino
```
Показывает Arduino-файлы в текущей директории.

```bash
nano имя_файла
```
Открывает файл для редактирования в Nano.

```bash
cat имя_файла
```
Выводит содержимое файла в терминал.

## Компиляция Raspberry Pi

```bash
g++ -std=c++17 -Wall -Wextra -o raspberry_server raspberry_server.cpp
```
Компилирует сервер Raspberry Pi и создаёт исполняемый файл `raspberry_server`.

```bash
g++ -std=c++17 -Wall -Wextra -o labirinth_client labirinth_client.cpp
```
Компилирует сервер из файла `server.cpp`.

```bash
./raspberry_server
```
Запускает сервер Raspberry Pi.

```bash
chmod +x raspberry_server
```
Добавляет право на выполнение файла.

## Компиляция PC-клиента

```powershell
g++ -std=c++17 -Wall -Wextra -o labirinth_server.exe labirinth_server.cpp -lws2_32
```
Компилирует PC-клиент под Windows с библиотекой WinSock2.

```powershell
.\pc_client.exe
```
Запускает PC-клиент из PowerShell.

```cmd
pc_client.exe
```
Запускает PC-клиент из командной строки Windows.

## Работа с Arduino

```bash
ls /dev/ttyUSB*
```
Показывает подключённые USB Serial-устройства.

```bash
ls /dev/ttyACM*
```
Показывает подключённые Arduino Serial-устройства.

```bash
ls /dev/ttyUSB0
```
Проверяет наличие устройства `/dev/ttyUSB0`.

```bash
ls /dev/ttyACM0
```
Проверяет наличие устройства `/dev/ttyACM0`.

```bash
sudo usermod -a -G dialout $USER
```
Добавляет текущего пользователя в группу `dialout` для доступа к Serial-устройствам.

```bash
groups
```
Показывает группы текущего пользователя.

```bash
sudo chmod 666 /dev/ttyACM0
```
Временно предоставляет чтение и запись для устройства `/dev/ttyACM0`.

```bash
sudo chmod 666 /dev/ttyUSB0
```
Временно предоставляет чтение и запись для устройства `/dev/ttyUSB0`.

## Проверка процесса сервера

```bash
ps aux | grep raspberry_server
```
Показывает запущенные процессы сервера.

```bash
pgrep raspberry_server
```
Показывает идентификатор запущенного процесса сервера.

```bash
kill PID
```
Завершает процесс с указанным идентификатором.

```bash
pkill raspberry_server
```
Завершает процессы с именем `raspberry_server`.

## Проверка сетевого подключения

```bash
hostname -I
```
Показывает IP-адрес Raspberry Pi.

```bash
ip addr
```
Показывает сетевые интерфейсы и их IP-адреса.

```bash
ss -ltn
```
Показывает TCP-порты, находящиеся в состоянии прослушивания.

```bash
ss -ltn | grep 5000
```
Проверяет, слушает ли сервер TCP-порт `5000`.

```bash
ping IP_RASPBERRY_PI
```
Проверяет доступность Raspberry Pi по сети.

```bash
nc -zv IP_RASPBERRY_PI 5000
```
Проверяет доступность TCP-порта `5000` на Raspberry Pi.

## Запуск проекта

```bash
cd ~/OmegaBot
```
Переходит в директорию проекта.

```bash
g++ -std=c++17 -Wall -Wextra -o labirinth_client labirinth_client.cpp
```
Компилирует сервер Raspberry Pi.

```bash
./labirinth_client
```
Запускает сервер и ожидает подключения PC.

```powershell
cd путь\к\проекту
```
Переходит в директорию проекта на Windows.

```powershell
g++ -std=c++17 -Wall -Wextra -o labirinth_server.exe labirinth_server.cpp -lws2_32
```
Компилирует PC-клиент.

```powershell
.\labirinth_server.exe
```
Запускает PC-клиент и подключается к Raspberry Pi.

## Остановка

```text
ESC
```
Отправляет команду остановки роботу и завершает PC-клиент.

```text
Ctrl+C
```
Принудительно останавливает работающую программу в терминале.
