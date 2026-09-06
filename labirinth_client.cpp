// ============================================================
// OmegaBot — Raspberry Pi сервер управления
// ============================================================

#include <iostream>
#include <string>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <termios.h>


class SerialController
{
private:
    const std::string serialDevice;
    const speed_t baudRate;

    int serialPort;

public:
    SerialController(
        const std::string& device,
        speed_t baud
    )
        : serialDevice(device),
          baudRate(baud),
          serialPort(-1)
    {
    }

    bool initialize()
    {
        serialPort = open(
            serialDevice.c_str(),
            O_RDWR | O_NOCTTY
        );

        if (serialPort < 0)
        {
            std::cerr
                << "Не удалось открыть Arduino: "
                << serialDevice
                << std::endl;

            return false;
        }

        struct termios serialSettings{};

        if (tcgetattr(serialPort, &serialSettings) != 0)
        {
            std::cerr
                << "Не удалось получить настройки Serial."
                << std::endl;

            close(serialPort);
            serialPort = -1;

            return false;
        }

        cfsetispeed(
            &serialSettings,
            baudRate
        );

        cfsetospeed(
            &serialSettings,
            baudRate
        );

        serialSettings.c_cflag |= (
            CLOCAL | CREAD
        );

        serialSettings.c_cflag &= ~PARENB;
        serialSettings.c_cflag &= ~CSTOPB;
        serialSettings.c_cflag &= ~CSIZE;
        serialSettings.c_cflag |= CS8;

        serialSettings.c_lflag = 0;
        serialSettings.c_oflag = 0;
        serialSettings.c_iflag = 0;

        tcsetattr(
            serialPort,
            TCSANOW,
            &serialSettings
        );

        return true;
    }

    bool sendCommand(char command)
    {
        if (serialPort < 0)
        {
            return false;
        }

        return write(
            serialPort,
            &command,
            1
        ) == 1;
    }

    void closeConnection()
    {
        if (serialPort >= 0)
        {
            close(serialPort);
            serialPort = -1;
        }
    }

    ~SerialController()
    {
        closeConnection();
    }
};


class NetworkController
{
private:
    const int serverPort;

    int serverSocket;
    int clientSocket;

public:
    explicit NetworkController(int port)
        : serverPort(port),
          serverSocket(-1),
          clientSocket(-1)
    {
    }

    bool initialize()
    {
        serverSocket = socket(
            AF_INET,
            SOCK_STREAM,
            0
        );

        if (serverSocket < 0)
        {
            std::cerr
                << "Не удалось создать TCP socket."
                << std::endl;

            return false;
        }

        int reuseAddress = 1;

        setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuseAddress,
            sizeof(reuseAddress)
        );

        sockaddr_in serverAddress{};

        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr.s_addr = INADDR_ANY;
        serverAddress.sin_port = htons(serverPort);

        if (bind(
            serverSocket,
            reinterpret_cast<sockaddr*>(&serverAddress),
            sizeof(serverAddress)
        ) < 0)
        {
            std::cerr
                << "Не удалось привязать порт "
                << serverPort
                << "."
                << std::endl;

            close(serverSocket);
            serverSocket = -1;

            return false;
        }

        if (listen(serverSocket, 1) < 0)
        {
            std::cerr
                << "Не удалось запустить ожидание подключения."
                << std::endl;

            close(serverSocket);
            serverSocket = -1;

            return false;
        }

        std::cout
            << "Сервер запущен. Порт: "
            << serverPort
            << std::endl;

        return true;
    }

    bool waitForClient()
    {
        sockaddr_in clientAddress{};
        socklen_t clientAddressLength =
            sizeof(clientAddress);

        std::cout
            << "Ожидание подключения PC..."
            << std::endl;

        clientSocket = accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientAddressLength
        );

        if (clientSocket < 0)
        {
            std::cerr
                << "Ошибка подключения клиента."
                << std::endl;

            return false;
        }

        std::cout
            << "PC подключён."
            << std::endl;

        return true;
    }

    int receiveCommand(char& command)
    {
        if (clientSocket < 0)
        {
            return -1;
        }

        return recv(
            clientSocket,
            &command,
            1,
            0
        );
    }

    void closeClient()
    {
        if (clientSocket >= 0)
        {
            close(clientSocket);
            clientSocket = -1;
        }
    }

    void closeServer()
    {
        closeClient();

        if (serverSocket >= 0)
        {
            close(serverSocket);
            serverSocket = -1;
        }
    }

    ~NetworkController()
    {
        closeServer();
    }
};


class CommandController
{
public:
    bool isMovementCommand(char command)
    {
        return
            command == 'F' ||
            command == 'B' ||
            command == 'L' ||
            command == 'R' ||
            command == 'S';
    }
};


class RobotServer
{
private:
    static const int SERVER_PORT = 5000;

    // Путь может отличаться: /dev/ttyUSB0 или /dev/ttyACM0.
    static constexpr const char* ARDUINO_DEVICE =
        "/dev/ttyUSB0";

    static const speed_t SERIAL_BAUD_RATE = B115200;

    NetworkController networkController;

    SerialController serialController;

    CommandController commandController;

public:
    RobotServer()
        : networkController(SERVER_PORT),
          serialController(
              ARDUINO_DEVICE,
              SERIAL_BAUD_RATE
          )
    {
    }

    bool initialize()
    {
        if (!serialController.initialize())
        {
            return false;
        }

        if (!networkController.initialize())
        {
            return false;
        }

        return true;
    }

    void run()
    {
        if (!networkController.waitForClient())
        {
            return;
        }

        processCommands();

        serialController.sendCommand('S');

        networkController.closeClient();

        std::cout
            << "Соединение завершено."
            << std::endl;
    }

private:
    void processCommands()
    {
        char receivedCommand;

        while (true)
        {
            int bytesReceived =
                networkController.receiveCommand(
                    receivedCommand
                );

            if (bytesReceived <= 0)
            {
                // При потере TCP-соединения робот должен остановиться.
                serialController.sendCommand('S');

                break;
            }

            if (
                commandController.isMovementCommand(
                    receivedCommand
                )
            )
            {
                serialController.sendCommand(
                    receivedCommand
                );

                std::cout
                    << "Команда: "
                    << receivedCommand
                    << std::endl;
            }
        }
    }
};


int main()
{
    std::cout
        << "OmegaBot Raspberry Pi Server\n"
        << std::endl;

    RobotServer robotServer;

    if (!robotServer.initialize())
    {
        return 1;
    }

    robotServer.run();

    return 0;
}// ============================================================
// OmegaBot — PC operator client
// ============================================================

#include <iostream>
#include <string>
#include <cstring>

#include <winsock2.h>
#include <conio.h>

#pragma comment(lib, "ws2_32.lib")


// ------------------------------------------------------------
// Настройки
// ------------------------------------------------------------

// IP Raspberry Pi
// ЗАМЕНИТЬ на IP вашего Raspberry Pi
#define RASPBERRY_IP "192.168.1.100"

#define SERVER_PORT 5000


// ------------------------------------------------------------
// Отправка команды
// ------------------------------------------------------------

void sendCommand(SOCKET socket, char command)
{
    send(
        socket,
        &command,
        1,
        0
    );
}


// ------------------------------------------------------------
// Main
// ------------------------------------------------------------

int main()
{

    std::cout << " OmegaBot PC Controller\n";



    std::cout << "Управление:\n";
    std::cout << "W - вперёд\n";
    std::cout << "S - назад\n";
    std::cout << "A - влево\n";
    std::cout << "D - вправо\n";
    std::cout << "SPACE - стоп\n";
    std::cout << "ESC - выход\n\n";


    // --------------------------------------------------------
    // Инициализация WinSock
    // --------------------------------------------------------

    WSADATA wsaData{};

    if (WSAStartup(
        MAKEWORD(2, 2),
        &wsaData
    ) != 0)
    {
        std::cerr
            << "Ошибка WSAStartup"
            << std::endl;

        return 1;
    }


    // --------------------------------------------------------
    // Создаём сокет
    // --------------------------------------------------------

    SOCKET socketClient = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );


    if (socketClient == INVALID_SOCKET)
    {
        std::cerr
            << "Не удалось создать socket"
            << std::endl;

        WSACleanup();

        return 1;
    }


    // --------------------------------------------------------
    // Адрес Raspberry Pi
    // --------------------------------------------------------

    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);

    inet_pton(
        AF_INET,
        RASPBERRY_IP,
        &serverAddress.sin_addr
    );


    // --------------------------------------------------------
    // Подключение
    // --------------------------------------------------------

    std::cout
        << "Подключение к Raspberry Pi..."
        << std::endl;


    if (connect(
        socketClient,
        (sockaddr*)&serverAddress,
        sizeof(serverAddress)
    ) == SOCKET_ERROR)
    {
        std::cerr
            << "Не удалось подключиться."
            << std::endl;

        closesocket(socketClient);
        WSACleanup();

        return 1;
    }


    std::cout
        << "Подключение установлено!\n"
        << std::endl;


    // --------------------------------------------------------
    // Управление
    // --------------------------------------------------------

    bool running = true;


    while (running)
    {
        if (_kbhit())
        {
            int key = _getch();


            switch (key)
            {
                // W
                case 'w':
                case 'W':
                    sendCommand(
                        socketClient,
                        'F'
                    );

                    std::cout
                        << "ВПЕРЁД"
                        << std::endl;

                    break;


                // S
                case 's':
                case 'S':
                    sendCommand(
                        socketClient,
                        'B'
                    );

                    std::cout
                        << "НАЗАД"
                        << std::endl;

                    break;


                // A
                case 'a':
                case 'A':
                    sendCommand(
                        socketClient,
                        'L'
                    );

                    std::cout
                        << "ВЛЕВО"
                        << std::endl;

                    break;


                // D
                case 'd':
                case 'D':
                    sendCommand(
                        socketClient,
                        'R'
                    );

                    std::cout
                        << "ВПРАВО"
                        << std::endl;

                    break;


                // SPACE
                case ' ':
                    sendCommand(
                        socketClient,
                        'S'
                    );

                    std::cout
                        << "СТОП"
                        << std::endl;

                    break;


                // ESC
                case 27:
                    sendCommand(
                        socketClient,
                        'S'
                    );

                    running = false;

                    break;
            }
        }


        // Небольшая задержка
        Sleep(10);
    }


    // --------------------------------------------------------
    // Завершение
    // --------------------------------------------------------

    closesocket(socketClient);

    WSACleanup();


    std::cout
        << "\nПрограмма завершена."
        << std::endl;


    return 0;
}
