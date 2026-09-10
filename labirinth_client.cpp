// ============================================================
// OmegaBot — ПК-клиент (Windows / Linux)
// Подключается к Raspberry Pi и отправляет команды.
// ============================================================

#include <iostream>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <conio.h>   // _kbhit, _getch
    #pragma comment(lib, "ws2_32.lib")
    using socklen_t = int;
    #define CLOSE_SOCKET closesocket
#else
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #include <termios.h>
    #define CLOSE_SOCKET close
#endif


class TcpClient
{
private:
    std::string serverIp;
    int serverPort;
    int socketFd;

public:
    TcpClient(const std::string& ip, int port)
        : serverIp(ip),
          serverPort(port),
          socketFd(-1)
    {
    }

    bool connectToServer()
    {
        socketFd = static_cast<int>(socket(AF_INET, SOCK_STREAM, 0));

        if (socketFd < 0)
        {
            std::cerr << "Не удалось создать socket." << std::endl;
            return false;
        }

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(static_cast<u_short>(serverPort));

        if (inet_pton(AF_INET, serverIp.c_str(), &serverAddress.sin_addr) <= 0)
        {
            std::cerr << "Неверный IP-адрес сервера." << std::endl;
            CLOSE_SOCKET(socketFd);
            socketFd = -1;
            return false;
        }

        if (connect(
                socketFd,
                reinterpret_cast<sockaddr*>(&serverAddress),
                sizeof(serverAddress)) < 0)
        {
            std::cerr << "Не удалось подключиться к "
                      << serverIp << ":" << serverPort << std::endl;
            CLOSE_SOCKET(socketFd);
            socketFd = -1;
            return false;
        }

        std::cout << "Подключено к " << serverIp
                  << ":" << serverPort << std::endl;
        return true;
    }

    bool sendCommand(char command)
    {
        if (socketFd < 0) return false;

        int sent = send(socketFd, &command, 1, 0);
        return sent == 1;
    }

    void disconnect()
    {
        if (socketFd >= 0)
        {
            CLOSE_SOCKET(socketFd);
            socketFd = -1;
        }
    }

    ~TcpClient()
    {
        disconnect();
    }
};


// ============================================================
// Управление клавиатурой в реальном времени (без Enter)
// ============================================================
#ifdef _WIN32

char readKey()
{
    if (_kbhit())
    {
        return static_cast<char>(_getch());
    }
    return 0;
}

#else

char readKey()
{
    termios oldt{}, newt{};
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    int ch = getchar();

    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return static_cast<char>(ch);
}

#endif


int main(int argc, char* argv[])
{
    // IP Raspberry Pi можно передать аргументом:
    //   labirinth_client.exe 10.122.144.232
    std::string serverIp = "10.122.144.232";

    if (argc > 1)
    {
        serverIp = argv[1];
    }

    const int SERVER_PORT = 5000;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }
#endif

    TcpClient client(serverIp, SERVER_PORT);

    if (!client.connectToServer())
    {
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "\nУправление:\n"
              << "  W или F — вперёд\n"
              << "  S или B — назад\n"
              << "  A или L — влево\n"
              << "  D или R — вправо\n"
              << "  Пробел  — стоп\n"
              << "  Q       — выход\n"
              << std::endl;

    bool running = true;

    while (running)
    {
        char key = readKey();

        if (key == 0) continue;

        char command = 0;

        switch (key)
        {
            case 'w': case 'W': case 'f': case 'F':
                command = 'F'; break;

            case 's': case 'S': case 'b': case 'B':
                command = 'B'; break;

            case 'a': case 'A': case 'l': case 'L':
                command = 'L'; break;

            case 'd': case 'D': case 'r': case 'R':
                command = 'R'; break;

            case ' ':
                command = 'S'; break;

            case 'q': case 'Q':
                running = false;
                break;

            default:
                break;
        }

        if (command != 0)
        {
            if (client.sendCommand(command))
            {
                std::cout << "Отправлено: " << command << std::endl;
            }
            else
            {
                std::cerr << "Ошибка отправки." << std::endl;
                running = false;
            }
        }
    }

    client.sendCommand('S');
    client.disconnect();

#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "Клиент завершён." << std::endl;
    return 0;
}
