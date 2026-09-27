// ============================================================
// OmegaBot — Raspberry Pi control server (плата ARP-DEK-STR-02)
// ------------------------------------------------------------
// В отличие от старой схемы (отдельный Arduino только слушал
// команды), здесь плата робота сама шлёт назад строки телеметрии
// (ИК/УЗ/энкодеры/объект камеры — см. str02/str02_controller.ino).
// Поэтому обмен по Serial теперь двунаправленный: команды ПК -> плата
// и телеметрия плата -> ПК, одновременно, без блокировки друг друга
// (см. select() в RobotServer::mainLoop).
//
// Протокол управления (ПК -> плата) не менялся: однобайтовые команды
// F/B/L/R/S, как и раньше.
//
// ------------------------------------------------------------
// Живучесть при потере связи с ПК.
// ------------------------------------------------------------
// Раньше сервер принимал ОДНОГО клиента (waitForClient() блокирующим
// accept()), а при разрыве этого TCP-соединения processCommands()
// выходил из цикла, run() возвращал управление и процесс завершался -
// для следующего подключения его нужно было перезапускать вручную по
// SSH. Кроме того, пока сервер блокировался в accept() в ожидании
// клиента, Serial вообще не читался - телеметрия от платы терялась
// и не попадала в telemetry.csv.
//
// Сейчас mainLoop() держит слушающий сокет, Serial и (если есть)
// текущего клиента одновременно в select():
//  - сервер никогда не завершает работу сам по себе при отключении
//    ПК - он просто возвращается в состояние ожидания нового
//    подключения;
//  - Serial продолжает читаться и телеметрия продолжает писаться в
//    лог непрерывно, даже когда оператор временно не подключён -
//    это и есть часть требования "собирать данные датчиков при
//    отсутствии связи": плата с её собственной автономной логикой
//    (см. AutonomousReturn в прошивке) работает независимо от того,
//    подключён ли сейчас ПК, а Pi продолжает всё это протоколировать.
//
// ВНИМАНИЕ: путь /dev/ttyUSB0 и скорость 115200 — то, что обычно
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
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <deque>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>


class RobotLogger
{
private:
    std::ofstream eventLog;
    std::ofstream telemetryLog;

public:
    bool initialize()
    {
        eventLog.open("robot.log", std::ios::app);
        telemetryLog.open("telemetry.csv", std::ios::app);

        if (!eventLog.is_open() || !telemetryLog.is_open())
        {
            return false;
        }

        if (telemetryLog.tellp() == std::streampos(0))
        {
            telemetryLog
                << "timestamp,telemetry"
                << std::endl;
        }

        return true;
    }

    std::string writeEvent(const std::string& message)
    {
        const std::string eventTimestamp = timestamp();

        if (eventLog.is_open())
        {
            eventLog
                << eventTimestamp
                << " | "
                << message
                << std::endl;
        }

        return eventTimestamp;
    }

    std::string writeTelemetry(const std::string& telemetry)
    {
        const std::string telemetryTimestamp = timestamp();

        if (telemetryLog.is_open())
        {
            telemetryLog
                << telemetryTimestamp
                << ",\""
                << telemetry
                << "\""
                << std::endl;
        }

        return telemetryTimestamp;
    }

private:
    std::string timestamp() const
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t currentTime =
            std::chrono::system_clock::to_time_t(now);

        std::tm timeInfo{};

#ifdef _WIN32
        localtime_s(&timeInfo, &currentTime);
#else
        localtime_r(&currentTime, &timeInfo);
#endif

        std::ostringstream output;
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()
            ) % 1000;

        output
            << std::put_time(&timeInfo, "%Y-%m-%d %H:%M:%S")
            << '.'
            << std::setfill('0')
            << std::setw(3)
            << milliseconds.count();

        return output.str();
    }
};


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
    // до следующего вызова. Возвращает false при ошибке/обрыве связи
    // с платой (это отдельная, более серьёзная ситуация, чем обрыв
    // связи с ПК-оператором, и обрабатывается отдельно в mainLoop).
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

    int getServerSocket() const
    {
        return serverSocket;
    }

    int getClientSocket() const
    {
        return clientSocket;
    }

    bool hasClient() const
    {
        return clientSocket >= 0;
    }

    // Принимает новое подключение на уже готовом (select() это
    // подтвердил) слушающем сокете. Если в этот момент уже был
    // подключён предыдущий клиент, он сначала отключается - сервер
    // одновременно обслуживает только одного оператора, как и раньше.
    bool acceptClient(std::string& acceptedFromAddress)
    {
        sockaddr_in clientAddress{};
        socklen_t clientAddressLength = sizeof(clientAddress);

        int newClientSocket = accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &clientAddressLength
        );

        if (newClientSocket < 0)
        {
            perror("accept");
            return false;
        }

        closeClient();

        clientSocket = newClientSocket;
        acceptedFromAddress = inet_ntoa(clientAddress.sin_addr);

        return true;
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
    // Если клиент сейчас не подключён, это не ошибка - телеметрия в
    // это время всё равно продолжает писаться в лог вызывающей
    // стороной (см. RobotServer::relayTelemetry), просто не уходит
    // по сети, потому что уходить некуда.
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
        "/dev/ttyUSB0";

    static constexpr speed_t SERIAL_BAUD =
        B115200;

    static constexpr size_t COMMAND_BUFFER_SIZE =
        256;

    NetworkController networkController;

    SerialController serialController;

    CommandController commandController;
    RobotLogger logger;

    static constexpr size_t TELEMETRY_HISTORY_SIZE = 1000;
    std::deque<std::string> telemetryHistory;
    static constexpr size_t EVENT_HISTORY_SIZE = 500;
    std::deque<std::string> eventHistory;

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

        if (!logger.initialize())
        {
            std::cerr
                << "Не удалось открыть файлы журналов."
                << std::endl;
            return 1;
        }

        publishEvent("Сервер запущен.");
        mainLoop();

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

    // Единый цикл на всё время жизни процесса: слушающий сокет,
    // Serial и (если подключён) сокет текущего оператора - все
    // одновременно в select(). Сервер сюда заходит один раз и
    // остаётся здесь, переживая любое количество подключений и
    // отключений ПК, пока не оборвётся сама связь с платой (это уже
    // считается фатальной ошибкой - без платы серверу нечего делать).
    void mainLoop()
    {
        char commandBuffer[COMMAND_BUFFER_SIZE];

        int serverFd = networkController.getServerSocket();
        int serialFd = serialController.getFileDescriptor();

        std::cout
            << "Ожидание подключения ПК (датчики платы уже опрашиваются)..."
            << std::endl;

        while (true)
        {
            int clientFd = networkController.getClientSocket();
            int maxFd = std::max({serverFd, serialFd, clientFd});

            fd_set readFds;
            FD_ZERO(&readFds);
            FD_SET(serverFd, &readFds);
            FD_SET(serialFd, &readFds);

            if (clientFd >= 0)
            {
                FD_SET(clientFd, &readFds);
            }

            int ready = select(maxFd + 1, &readFds, nullptr, nullptr, nullptr);

            if (ready < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }

                perror("select");
                break;
            }

            if (FD_ISSET(serverFd, &readFds))
            {
                acceptNewClient();
            }

            if (clientFd >= 0 && FD_ISSET(clientFd, &readFds))
            {
                if (!handleClientData(commandBuffer))
                {
                    handleClientDisconnect();
                }
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
                    // Это обрыв связи с самой платой робота (например,
                    // отвалился USB) - в отличие от отключения ПК, это
                    // фатально для сервера, продолжать нечем.
                    std::cerr
                        << "Связь с платой робота потеряна (Serial)."
                        << std::endl;

                    publishEvent("Связь с платой робота потеряна (Serial).");

                    break;
                }
            }
        }
    }

    void acceptNewClient()
    {
        std::string fromAddress;

        if (!networkController.acceptClient(fromAddress))
        {
            return;
        }

        std::cout
            << "PC подключён: "
            << fromAddress
            << std::endl;

        replayEventHistory();
        replayTelemetryHistory();
        publishEvent("PC подключён: " + fromAddress);
    }

    void replayEventHistory()
    {
        if (!networkController.hasClient() || eventHistory.empty())
            return;

        for (const std::string& eventLine : eventHistory)
        {
            const std::string withNewline = eventLine + "\n";

            if (!networkController.sendData(
                    withNewline.c_str(),
                    withNewline.size()))
            {
                networkController.closeClient();
                return;
            }
        }
    }

    void publishEvent(const std::string& message)
    {
        const std::string eventTimestamp = logger.writeEvent(message);
        const std::string eventLine =
            "E," + eventTimestamp + "," + message + "\n";

        eventHistory.push_back(eventLine.substr(0, eventLine.size() - 1));
        if (eventHistory.size() > EVENT_HISTORY_SIZE)
            eventHistory.pop_front();

        if (networkController.hasClient())
        {
            networkController.sendData(
                eventLine.c_str(),
                eventLine.size()
            );
        }
    }

    void replayTelemetryHistory()
    {
        if (!networkController.hasClient() || telemetryHistory.empty())
            return;

        std::cout
            << "Передача накопленной телеметрии: "
            << telemetryHistory.size()
            << " строк."
            << std::endl;

        for (const std::string& line : telemetryHistory)
        {
            std::string withNewline = line + "\n";

            if (!networkController.sendData(
                    withNewline.c_str(),
                    withNewline.size()))
            {
                networkController.closeClient();
                return;
            }
        }
    }

    // Возвращает false, если клиент отключился (0 байт/ошибка) -
    // тогда вызывающая сторона обрабатывает отключение отдельно.
    bool handleClientData(char* commandBuffer)
    {
        ssize_t bytesReceived = networkController.receiveData(
            commandBuffer,
            COMMAND_BUFFER_SIZE
        );

        if (bytesReceived <= 0)
        {
            return false;
        }

        processReceivedCommands(
            commandBuffer,
            bytesReceived
        );

        return true;
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

            publishEvent(
                std::string("Команда: ") + command
            );
        }
        else
        {
            std::cerr
                << "Не удалось отправить команду плате робота."
                << std::endl;

            publishEvent(
                std::string("Ошибка отправки команды: ") + command
            );
        }
    }

    // Строка телеметрии (ИК/УЗ/энкодеры/камера/режим) от платы
    // робота - логируем на Pi всегда, а оператору на ПК пересылаем,
    // только если он сейчас подключён (sendData сама на это
    // проверяется и просто ничего не делает, если клиента нет).
    void relayTelemetry(const std::string& line)
    {
        std::cout << "Телеметрия: " << line << std::endl;

        const std::string telemetryTimestamp = logger.writeTelemetry(line);

        std::string operatorTelemetry =
            "T," + telemetryTimestamp;

        if (line.size() > 2 && line[0] == 'T' && line[1] == ',')
            operatorTelemetry += "," + line.substr(2);
        else
            operatorTelemetry += "," + line;

        telemetryHistory.push_back(operatorTelemetry);
        if (telemetryHistory.size() > TELEMETRY_HISTORY_SIZE)
            telemetryHistory.pop_front();

        operatorTelemetry += "\n";

        networkController.sendData(
            operatorTelemetry.c_str(),
            operatorTelemetry.size()
        );
    }

    // ПК отключился - это НЕ повод завершать сервер. Останавливаем
    // робота на всякий случай (хотя плата и сама остановится по
    // своему watchdog'у, а затем начнёт автономный возврат) и просто
    // возвращаемся в режим ожидания нового подключения.
    void handleClientDisconnect()
    {
        std::cout
            << "PC отключён."
            << std::endl;

        publishEvent("PC отключён.");

        if (serialController.sendCommand('S'))
        {
            publishEvent("Безопасная остановка после отключения ПК.");
        }

        networkController.closeClient();

        std::cout
            << "Ожидание нового подключения ПК..."
            << std::endl;
    }
};


int main()
{
    RobotServer robotServer;

    return robotServer.run();
}
