// ============================================================
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