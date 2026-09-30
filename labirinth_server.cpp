// ============================================================
// Raspberry Pi control server (плата ARP-DEK-STR-02)
// ------------------------------------------------------------
//
// Протокол управления (ПК -> плата): однобайтовые команды
// F/B/L/R/S.
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
// Открывает файлы логов и создаёт заголовок CSV
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

// Записывает событие в лог и возвращает его время
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

// Записывает телеметрию в CSV и возвращает её время
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
// Возвращает текущее время с миллисекундами
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

// Открывает Serial-порт и настраивает соединение с платой
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

// Возвращает файловый дескриптор Serial-порта
    int getFileDescriptor() const
    {
        return serialFileDescriptor;
    }

// Отправляет одну команду на плату робота
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

// Читает данные от платы и передаёт готовые строки обработчику
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

// Закрывает Serial-соединение
    void closeConnection()
    {
        if (serialFileDescriptor >= 0)
        {
            close(serialFileDescriptor);
            serialFileDescriptor = -1;
        }
    }

// Закрывает Serial-соединение при уничтожении объекта
    ~SerialController()
    {
        closeConnection();
    }

private:
// Настраивает скорость передачи данных Serial-порта
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

// Настраивает основные параметры Serial-соединения
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

// Отключает стандартную обработку ввода и вывода Serial.
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

// Запускает TCP-сервер и начинает принимать подключения.
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

// Возвращает дескриптор серверного сокета
    int getServerSocket() const
    {
        return serverSocket;
    }

// Возвращает дескриптор подключения оператора
    int getClientSocket() const
    {
        return clientSocket;
    }

// Проверяет, подключён ли оператор
    bool hasClient() const
    {
        return clientSocket >= 0;
    }

// Принимает новое подключение на уже готовом слушающем сокете. Если в этот момент уже был подключён предыдущий клиент, он сначала отключается
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

// Получает данные от оператора
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

    // Отправка телеметрии от платы робота обратно оператору на ПК
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

// Закрывает подключение оператора
    void closeClient()
    {
        if (clientSocket >= 0)
        {
            close(clientSocket);
            clientSocket = -1;
        }
    }

// Закрывает TCP-сервер и подключение оператора
    void closeServer()
    {
        closeClient();

        if (serverSocket >= 0)
        {
            close(serverSocket);
            serverSocket = -1;
        }
    }

// Закрывает сетевые сокеты при уничтожении объекта
    ~NetworkController()
    {
        closeServer();
    }

private:
// Разрешает повторное использование адреса TCP-сервера
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

// Привязывает серверный сокет к указанному порту
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
// Переводит серверный сокет в режим ожидания подключений
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
// Проверяет, является ли команда допустимой
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

// Запускает сервер робота и его основной цикл.
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
// Инициализирует Serial и сетевое соединение
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

    // Единый цикл на всё время жизни процесса: обрабатывает подключения, команды и телеметрию.
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
                    // Это обрыв связи с самой платой робота
                    std::cerr
                        << "Связь с платой робота потеряна (Serial)."
                        << std::endl;

                    publishEvent("Связь с платой робота потеряна (Serial).");

                    break;
                }
            }
        }
    }

// Принимает новое подключение и передаёт накопленные данные
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

// Передаёт оператору накопленные события
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

// Записывает событие и отправляет его оператору
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
// Передаёт оператору накопленную телеметрию
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

// Проверяет полученные команды и передаёт их роботу
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
// Отправляет команду управления на плату
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

    // ПК отключился. Останавливаем
    // робота на всякий случай и просто возвращаемся в режим ожидания нового подключения.
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
