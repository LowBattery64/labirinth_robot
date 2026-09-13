// ============================================================
// OmegaBot — Raspberry Pi control server (плата ARP-DEK-STR-02)
// ------------------------------------------------------------
// В отличие от старой схемы (отдельный Arduino только слушал
// команды), здесь плата робота сама шлёт назад строки телеметрии
// (ИК/УЗ/энкодеры/объект камеры — см. str02/str02_controller.ino).
// Поэтому обмен по Serial теперь двунаправленный: команды ПК -> плата
// и телеметрия плата -> ПК, одновременно, без блокировки друг друга
// (см. select() в RobotServer::processCommands).
//
// Протокол управления (ПК -> плата) не менялся: однобайтовые команды
// F/B/L/R/S, как и раньше.
//
// ВНИМАНИЕ: путь /dev/ttyACM0 и скорость 115200 — то, что обычно
// получается при подключении Mega-совместимой платы по USB, но это
// стоит проверить на реальном Raspberry Pi (`ls /dev/tty*` до и после
// подключения платы) и поправить SERIAL_PORT при необходимости.
// ============================================================

#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <algorithm>
#include <functional>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>


class SerialController
{
private:
    const std::string serialPortPath;
    const speed_t baudRate;

    int serialFileDescriptor;
    std::string lineBuffer;

public:
    SerialController(
        const std::string& portPath,
        speed_t serialBaudRate
    )
        : serialPortPath(portPath),
          baudRate(serialBaudRate),
          serialFileDescriptor(-1)
    {
    }

    bool initialize()
    {
        serialFileDescriptor = open(
            serialPortPath.c_str(),
            O_RDWR | O_NOCTTY
        );

        if (serialFileDescriptor < 0)
        {
            perror("Не удалось открыть Serial");
            return false;
        }

        struct termios serialSettings{};

        if (tcgetattr(
            serialFileDescriptor,
            &serialSettings
        ) != 0)
        {
            perror("tcgetattr");

            closeConnection();

            return false;
        }

        configureBaudRate(serialSettings);
        configureCommunicationMode(serialSettings);
        configureInputOutputMode(serialSettings);

        if (tcsetattr(
            serialFileDescriptor,
            TCSANOW,
            &serialSettings
        ) != 0)
        {
            perror("tcsetattr");

            closeConnection();

            return false;
        }

        std::cout
            << "Serial открыт: "
            << serialPortPath
            << std::endl;

        return true;
    }

    int getFileDescriptor() const
    {
        return serialFileDescriptor;
    }

    bool sendCommand(char command)
    {
        if (serialFileDescriptor < 0)
        {
            return false;
        }

        return write(
            serialFileDescriptor,
            &command,
            1
        ) == 1;
    }

    // Вызывается, когда select() сказал, что на serialFileDescriptor
    // есть данные. Читает всё, что накопилось, разбивает на строки
    // (плата шлёт телеметрию, разделённую '\n') и отдаёт каждую
    // полную строку через onLine. Неполный хвост остаётся в буфере
    // до следующего вызова. Возвращает false при ошибке/обрыве связи.
    bool pollLines(const std::function<void(const std::string&)>& onLine)
    {
        char chunk[256];

        ssize_t bytesRead = read(
            serialFileDescriptor,
            chunk,
            sizeof(chunk)
        );

        if (bytesRead < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                return true;
            }

            perror("read (serial)");
            return false;
        }

        if (bytesRead == 0)
        {
            // Плата отключилась (USB отвалился и т.п.)
            return false;
        }

        for (ssize_t i = 0; i < bytesRead; i++)
        {
            char receivedChar = chunk[i];

            if (receivedChar == '\n')
            {
                onLine(lineBuffer);
                lineBuffer.clear();
            }
            else if (receivedChar != '\r')
            {
                lineBuffer += receivedChar;
            }
        }

        return true;
    }

    void closeConnection()
    {
        if (serialFileDescriptor >= 0)
        {
            close(serialFileDescriptor);
            serialFileDescriptor = -1;
        }
    }

    ~SerialController()
    {
        closeConnection();
    }

private:
    void configureBaudRate(
        struct termios& serialSettings
    )
    {
        cfsetispeed(
            &serialSettings,
            baudRate
        );

        cfsetospeed(
            &serialSettings,
            baudRate
        );
    }

    void configureCommunicationMode(
        struct termios& serialSettings
    )
    {
        serialSettings.c_cflag |= (
            CLOCAL | CREAD
        );

        serialSettings.c_cflag &= ~PARENB;
        serialSettings.c_cflag &= ~CSTOPB;
        serialSettings.c_cflag &= ~CSIZE;

        serialSettings.c_cflag |= CS8;
    }

    void configureInputOutputMode(
        struct termios& serialSettings
    )
    {
        serialSettings.c_lflag &= ~(
            ICANON |
            ECHO |
            ECHOE |
            ISIG
        );

        serialSettings.c_iflag &= ~(
            IXON |
            IXOFF |
            IXANY
        );

        serialSettings.c_oflag &= ~OPOST;
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
            perror("socket");
            return false;
        }

        enableAddressReuse();

        if (!bindServerSocket())
        {
            closeServer();
            return false;
        }

        if (!startListening())
        {
            closeServer();
            return false;
        }

        std::cout
            << "TCP сервер запущен на порту "
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
            perror("accept");
            return false;
        }

        std::cout
            << "PC подключён: "
            << inet_ntoa(clientAddress.sin_addr)
            << std::endl;

        return true;
    }

    int getClientSocket() const
    {
        return clientSocket;
    }

    ssize_t receiveData(
        char* buffer,
        size_t bufferSize
    )
    {
        if (clientSocket < 0)
        {
            return -1;
        }

        return recv(
            clientSocket,
            buffer,
            bufferSize,
            0
        );
    }

    // Отправка телеметрии от платы робота обратно оператору на ПК.
    bool sendData(const char* data, size_t length)
    {
        if (clientSocket < 0)
        {
            return false;
        }

        size_t totalSent = 0;

        while (totalSent < length)
        {
            ssize_t sent = send(
                clientSocket,
                data + totalSent,
                length - totalSent,
                MSG_NOSIGNAL
            );

            if (sent <= 0)
            {
                return false;
            }

            totalSent += static_cast<size_t>(sent);
        }

        return true;
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

private:
    void enableAddressReuse()
    {
        int enableReuse = 1;

        setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &enableReuse,
            sizeof(enableReuse)
        );
    }

    bool bindServerSocket()
    {
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
            perror("bind");
            return false;
        }

        return true;
    }

    bool startListening()
    {
        if (listen(
            serverSocket,
            1
        ) < 0)
        {
            perror("listen");
            return false;
        }

        return true;
    }
};


class CommandController
{
public:
    bool isValidCommand(char command) const
    {
        switch (command)
        {
            case 'F':
            case 'B':
            case 'L':
            case 'R':
            case 'S':
                return true;

            default:
                return false;
        }
    }
};


class RobotServer
{
private:
    static constexpr int SERVER_PORT = 5000;

    // См. предупреждение в шапке файла - проверить на реальном Pi.
    static constexpr const char* SERIAL_PORT =
        "/dev/ttyACM0";

    static constexpr speed_t SERIAL_BAUD =
        B115200;

    static constexpr size_t COMMAND_BUFFER_SIZE =
        256;

    NetworkController networkController;

    SerialController serialController;

    CommandController commandController;

public:
    RobotServer()
        : networkController(SERVER_PORT),
          serialController(
              SERIAL_PORT,
              SERIAL_BAUD
          )
    {
    }

    int run()
    {
        std::cout
            << "OmegaBot Raspberry Pi Server (ARP-DEK-STR-02)\n"
            << std::endl;

        if (!initialize())
        {
            return 1;
        }

        if (!connectOperator())
        {
            return 1;
        }

        processCommands();

        stopRobot();

        return 0;
    }

private:
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

    bool connectOperator()
    {
        return networkController.waitForClient();
    }

    // Одновременно слушаем ПК (TCP) и плату робота (Serial) через
    // select() - ни одно направление не блокирует другое.
    void processCommands()
    {
        char commandBuffer[COMMAND_BUFFER_SIZE];

        int clientFd = networkController.getClientSocket();
        int serialFd = serialController.getFileDescriptor();
        int maxFd = std::max(clientFd, serialFd);

        while (true)
        {
            fd_set readFds;
            FD_ZERO(&readFds);
            FD_SET(clientFd, &readFds);
            FD_SET(serialFd, &readFds);

            int ready = select(maxFd + 1, &readFds, nullptr, nullptr, nullptr);

            if (ready < 0)
            {
                perror("select");
                break;
            }

            if (FD_ISSET(clientFd, &readFds))
            {
                ssize_t bytesReceived = networkController.receiveData(
                    commandBuffer,
                    COMMAND_BUFFER_SIZE
                );

                if (bytesReceived <= 0)
                {
                    handleClientDisconnect();
                    break;
                }

                processReceivedCommands(
                    commandBuffer,
                    bytesReceived
                );
            }

            if (FD_ISSET(serialFd, &readFds))
            {
                bool serialOk = serialController.pollLines(
                    [this](const std::string& line)
                    {
                        relayTelemetry(line);
                    }
                );

                if (!serialOk)
                {
                    std::cerr
                        << "Связь с платой робота потеряна (Serial)."
                        << std::endl;
                    break;
                }
            }
        }
    }

    void processReceivedCommands(
        const char* commandBuffer,
        ssize_t bytesReceived
    )
    {
        for (
            ssize_t commandIndex = 0;
            commandIndex < bytesReceived;
            commandIndex++
        )
        {
            char receivedCommand =
                commandBuffer[commandIndex];

            if (
                commandController.isValidCommand(
                    receivedCommand
                )
            )
            {
                sendRobotCommand(
                    receivedCommand
                );
            }
        }
    }

    void sendRobotCommand(char command)
    {
        if (
            serialController.sendCommand(
                command
            )
        )
        {
            std::cout
                << "Команда: "
                << command
                << std::endl;
        }
        else
        {
            std::cerr
                << "Не удалось отправить команду плате робота."
                << std::endl;
        }
    }

    // Строка телеметрии (ИК/УЗ/энкодеры/камера) от платы робота —
    // логируем на Pi и пересылаем оператору на ПК тем же TCP-соединением.
    void relayTelemetry(const std::string& line)
    {
        std::cout << "Телеметрия: " << line << std::endl;

        std::string withNewline = line + "\n";

        networkController.sendData(
            withNewline.c_str(),
            withNewline.size()
        );
    }

    void handleClientDisconnect()
    {
        std::cout
            << "PC отключён."
            << std::endl;
    }

    void stopRobot()
    {
        // Безопасное состояние при любом завершении TCP-сессии.
        serialController.sendCommand('S');
    }
};


int main()
{
    RobotServer robotServer;

    return robotServer.run();
}
