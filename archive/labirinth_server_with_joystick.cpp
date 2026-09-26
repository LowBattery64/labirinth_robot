#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <Xinput.h>

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "Xinput.lib")


class NetworkSettings
{
public:
    static constexpr const char* RASPBERRY_IP = "10.109.150.232";
    static constexpr int SERVER_PORT = 5000;
};


class NetworkController
{
private:
    SOCKET connectionSocket;

public:
    NetworkController()
        : connectionSocket(INVALID_SOCKET)
    {
    }

    bool connectToServer()
    {
        connectionSocket = socket(
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP
        );

        if (connectionSocket == INVALID_SOCKET)
        {
            return false;
        }

        sockaddr_in serverAddress{};

        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port =
            htons(NetworkSettings::SERVER_PORT);

        if (
            inet_pton(
                AF_INET,
                NetworkSettings::RASPBERRY_IP,
                &serverAddress.sin_addr
            ) != 1
        )
        {
            closeConnection();
            return false;
        }

        if (
            connect(
                connectionSocket,
                reinterpret_cast<sockaddr*>(&serverAddress),
                sizeof(serverAddress)
            ) == SOCKET_ERROR
        )
        {
            closeConnection();
            return false;
        }

        return true;
    }

    bool sendCommand(char command)
    {
        if (connectionSocket == INVALID_SOCKET)
        {
            return false;
        }

        return send(
            connectionSocket,
            &command,
            sizeof(command),
            0
        ) != SOCKET_ERROR;
    }

    void receiveTelemetry(std::atomic<bool>& running)
    {
        std::string line;
        char receivedCharacter;

        while (running)
        {
            int received = recv(
                connectionSocket,
                &receivedCharacter,
                1,
                0
            );

            if (received <= 0)
            {
                return;
            }

            if (receivedCharacter == '
')
            {
                if (!line.empty())
                {
                    std::cout
                        << "Телеметрия: "
                        << line
                        << std::endl;

                    line.clear();
                }
            }
            else if (receivedCharacter != '')
            {
                line += receivedCharacter;
            }
        }
    }

    void closeConnection()
    {
        if (connectionSocket != INVALID_SOCKET)
        {
            closesocket(connectionSocket);
            connectionSocket = INVALID_SOCKET;
        }
    }

    ~NetworkController()
    {
        closeConnection();
    }
};


class GamepadController
{
private:
    static constexpr int LEFT_STICK_DEAD_ZONE = 8000;

    XINPUT_STATE gamepadState{};

public:
    bool isConnected()
    {
        ZeroMemory(
            &gamepadState,
            sizeof(XINPUT_STATE)
        );

        return XInputGetState(
            0,
            &gamepadState
        ) == ERROR_SUCCESS;
    }

    char getMovementCommand()
    {
        const SHORT horizontalAxis =
            gamepadState.Gamepad.sThumbLX;

        const SHORT verticalAxis =
            gamepadState.Gamepad.sThumbLY;

        if (verticalAxis > LEFT_STICK_DEAD_ZONE)
        {
            return 'F';
        }

        if (verticalAxis < -LEFT_STICK_DEAD_ZONE)
        {
            return 'B';
        }

        if (horizontalAxis < -LEFT_STICK_DEAD_ZONE)
        {
            return 'L';
        }

        if (horizontalAxis > LEFT_STICK_DEAD_ZONE)
        {
            return 'R';
        }

        return 'S';
    }

    bool isStopPressed()
    {
        return (
            gamepadState.Gamepad.wButtons &
            XINPUT_GAMEPAD_A
        ) != 0;
    }

    bool isExitPressed()
    {
        return (
            gamepadState.Gamepad.wButtons &
            XINPUT_GAMEPAD_START
        ) != 0;
    }
};


class RobotController
{
private:
    NetworkController networkController;
    GamepadController gamepadController;

    char lastCommand;
    bool running;

public:
    RobotController()
        : lastCommand('S'),
          running(true)
    {
    }

    int run()
    {
        if (!initializeNetwork())
        {
            return 1;
        }

        if (!waitForGamepad())
        {
            return 1;
        }

        std::atomic<bool> telemetryRunning(true);

        std::thread telemetryThread(
            [this, &telemetryRunning]()
            {
                networkController.receiveTelemetry(
                    telemetryRunning
                );
                telemetryRunning = false;
            }
        );

        controlRobot();

        stopRobot();

        telemetryRunning = false;
        networkController.closeConnection();

        if (telemetryThread.joinable())
        {
            telemetryThread.join();
        }

        return 0;
    }

private:
    bool initializeNetwork()
    {
        WSADATA windowsSocketData{};

        if (
            WSAStartup(
                MAKEWORD(2, 2),
                &windowsSocketData
            ) != 0
        )
        {
            std::cout
                << "Ошибка инициализации Winsock.
";

            return false;
        }

        if (!networkController.connectToServer())
        {
            std::cout
                << "Не удалось подключиться к Raspberry Pi.
";

            WSACleanup();

            return false;
        }

        std::cout
            << "Подключение к Raspberry Pi установлено.
";

        return true;
    }

    bool waitForGamepad()
    {
        std::cout
            << "Ожидание USB-геймпада...
";

        while (running)
        {
            if (gamepadController.isConnected())
            {
                std::cout
                    << "Геймпад подключён.
";

                return true;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(500)
            );
        }

        return false;
    }

    void controlRobot()
    {
        std::cout
            << "
Управление:
"
            << "Левый стик — движение
"
            << "A — стоп
"
            << "Start — выход

";

        while (running)
        {
            if (!gamepadController.isConnected())
            {
                stopRobot();

                std::cout
                    << "Геймпад отключён.
";

                break;
            }

            if (gamepadController.isExitPressed())
            {
                running = false;
                break;
            }

            char currentCommand =
                gamepadController.getMovementCommand();

            if (gamepadController.isStopPressed())
            {
                currentCommand = 'S';
            }

            sendCommand(currentCommand);

            std::this_thread::sleep_for(
                std::chrono::milliseconds(100)
            );
        }
    }

    void sendCommand(char command)
    {
        if (command != lastCommand)
        {
            printCommand(command);
            lastCommand = command;
        }

        networkController.sendCommand(command);
    }

    void stopRobot()
    {
        networkController.sendCommand('S');
        lastCommand = 'S';
    }

    void printCommand(char command)
    {
        switch (command)
        {
            case 'F':
                std::cout << "Вперёд
";
                break;

            case 'B':
                std::cout << "Назад
";
                break;

            case 'L':
                std::cout << "Влево
";
                break;

            case 'R':
                std::cout << "Вправо
";
                break;

            case 'S':
                std::cout << "Стоп
";
                break;
        }
    }
};


int main()
{
    RobotController robotController;

    const int result = robotController.run();

    WSACleanup();

    return result;
}
