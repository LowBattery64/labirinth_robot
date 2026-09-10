// ============================================================
// OmegaBot — Raspberry Pi control server
// ============================================================

#include <iostream>
#include <string>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


class SerialController
{
private:
    const std::string serialPortPath;
    const speed_t baudRate;

    int serialFileDescriptor;

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
            << "OmegaBot Raspberry Pi Server\n"
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

    void processCommands()
    {
        char commandBuffer[
            COMMAND_BUFFER_SIZE
        ];

        while (true)
        {
            ssize_t bytesReceived =
                networkController.receiveData(
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
                << "Не удалось отправить команду Arduino."
                << std::endl;
        }
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
}// ============================================================
// OmegaBot — Raspberry Pi control server
// ============================================================

#include <iostream>
#include <string>
#include <cstring>

#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


class SerialController
{
private:
    const std::string serialPortPath;
    const speed_t baudRate;

    int serialFileDescriptor;

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
            << "OmegaBot Raspberry Pi Server\n"
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

    void processCommands()
    {
        char commandBuffer[
            COMMAND_BUFFER_SIZE
        ];

        while (true)
        {
            ssize_t bytesReceived =
                networkController.receiveData(
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
                << "Не удалось отправить команду Arduino."
                << std::endl;
        }
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
