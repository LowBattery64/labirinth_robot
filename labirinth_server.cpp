// ============================================================
// OmegaBot — Raspberry Pi control server
// ============================================================

#include <iostream>
#include <cstring>
#include <string>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


// ------------------------------------------------------------
// Настройки
// ------------------------------------------------------------

#define SERVER_PORT 5000

// При необходимости изменить:
// /dev/ttyUSB0
// /dev/ttyACM0
#define SERIAL_PORT "/dev/ttyACM0"

#define SERIAL_BAUD B115200


// ------------------------------------------------------------
// Настройка Serial
// ------------------------------------------------------------

int openSerial()
{
    int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY);

    if (fd < 0)
    {
        perror("Не удалось открыть Serial");
        return -1;
    }

    struct termios tty{};

    if (tcgetattr(fd, &tty) != 0)
    {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    cfsetispeed(&tty, SERIAL_BAUD);
    cfsetospeed(&tty, SERIAL_BAUD);

    tty.c_cflag |= (CLOCAL | CREAD);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;

    tty.c_cflag |= CS8;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    tty.c_oflag &= ~OPOST;

    tcsetattr(fd, TCSANOW, &tty);

    std::cout << "Serial открыт: "
              << SERIAL_PORT
              << std::endl;

    return fd;
}


// ------------------------------------------------------------
// Создание TCP-сервера
// ------------------------------------------------------------

int createServer()
{
    int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket < 0)
    {
        perror("socket");
        return -1;
    }

    int opt = 1;

    setsockopt(
        serverSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        &opt,
        sizeof(opt)
    );


    sockaddr_in serverAddress{};

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(SERVER_PORT);


    if (bind(
        serverSocket,
        (sockaddr*)&serverAddress,
        sizeof(serverAddress)
    ) < 0)
    {
        perror("bind");
        close(serverSocket);
        return -1;
    }


    if (listen(serverSocket, 1) < 0)
    {
        perror("listen");
        close(serverSocket);
        return -1;
    }


    std::cout
        << "TCP сервер запущен на порту "
        << SERVER_PORT
        << std::endl;

    return serverSocket;
}


// ------------------------------------------------------------
// Основная программа
// ------------------------------------------------------------

int main()
{

    std::cout << " OmegaBot Raspberry Pi Server\n";



    // --------------------------------------------------------
    // Serial
    // --------------------------------------------------------

    int serialFd = openSerial();

    if (serialFd < 0)
    {
        return 1;
    }


    // --------------------------------------------------------
    // TCP
    // --------------------------------------------------------

    int serverSocket = createServer();

    if (serverSocket < 0)
    {
        close(serialFd);
        return 1;
    }


    // --------------------------------------------------------
    // Ждём ПК
    // --------------------------------------------------------

    std::cout
        << "Ожидание подключения PC..."
        << std::endl;


    sockaddr_in clientAddress{};
    socklen_t clientLength = sizeof(clientAddress);


    int clientSocket = accept(
        serverSocket,
        (sockaddr*)&clientAddress,
        &clientLength
    );


    if (clientSocket < 0)
    {
        perror("accept");

        close(serverSocket);
        close(serialFd);

        return 1;
    }


    std::cout
        << "PC подключён: "
        << inet_ntoa(clientAddress.sin_addr)
        << std::endl;


    // --------------------------------------------------------
    // Принимаем команды
    // --------------------------------------------------------

    char buffer[256];


    while (true)
    {
        memset(buffer, 0, sizeof(buffer));


        ssize_t bytesReceived = recv(
            clientSocket,
            buffer,
            sizeof(buffer) - 1,
            0
        );


        // Клиент отключился
        if (bytesReceived <= 0)
        {
            std::cout
                << "PC отключён."
                << std::endl;

            // На всякий случай стоп
            char stopCommand = 'S';

            write(
                serialFd,
                &stopCommand,
                1
            );

            break;
        }


        // Обрабатываем полученные символы
        for (ssize_t i = 0; i < bytesReceived; i++)
        {
            char command = buffer[i];


            if (
                command == 'F' ||
                command == 'B' ||
                command == 'L' ||
                command == 'R' ||
                command == 'S'
            )
            {
                write(
                    serialFd,
                    &command,
                    1
                );


                std::cout
                    << "Команда: "
                    << command
                    << std::endl;
            }
        }
    }


    close(clientSocket);
    close(serverSocket);
    close(serialFd);

    return 0;
}