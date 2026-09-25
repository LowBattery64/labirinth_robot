# OmegaBot Operator Console

Qt 6 desktop interface for телеуправление OmegaBot.

## Текущее поведение

- Видео с Raspberry Pi через TCP 5001.
- Управление и телеметрия через TCP 5000.
- Raspberry Pi: 10.109.150.232.
- W/A/S/D и стрелки управляют роботом.
- Отпускание клавиши не останавливает робота.
- S, Space и STOP останавливают робота.
- Показываются состояние соединения, безопасность, центральный УЗ-дальномер и FPS видео.
- Журнал оператора доступен внутри приложения, но пока не сохраняется в файл.

## Сборка Windows

Из корня репозитория:

~~~powershell
cmake -S operator_ui -B operator_ui/build -G "Visual Studio 17 2022" -A x64
cmake --build operator_ui/build --config Release
~~~

Исполняемый файл:

~~~text
operator_ui/build/Release/OmegaBotOperator.exe
~~~

Требуется Qt 6 с компонентами:

- Qt6::Widgets
- Qt6::Network

Если CMake не находит Qt, необходимо указать установленный Qt через CMAKE_PREFIX_PATH.

## Сетевые подключения

Управление и телеметрия:

~~~text
10.109.150.232:5000
~~~

Видео:

~~~text
10.109.150.232:5001
~~~

Приложение подключается к Raspberry Pi автоматически при запуске.
