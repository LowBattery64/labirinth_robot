// ============================================================
// OmegaBot — клиент видеопотока (ПК)
// ------------------------------------------------------------
// Отдельная программа от labirinth_client.cpp: подключается к
// Raspberry Pi на порт 5001 и просто показывает видео.
// Управление роботом (labirinth_client.exe) запускается
// параллельно, отдельным окном, как и раньше.
// ============================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    using socklen_t = int;
    #define CLOSE_SOCKET closesocket
#else
    #include <arpa/inet.h>
    #include <sys/socket.h>
    #include <unistd.h>
    #define CLOSE_SOCKET close
#endif

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>


// ============================================================
// TCP-соединение с видео-сервером на Raspberry Pi
// ============================================================

class VideoTcpClient
{
private:
    std::string serverIp;
    int serverPort;
    int socketFd;

public:
    VideoTcpClient(const std::string& ip, int port)
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
            disconnect();
            return false;
        }

        if (connect(socketFd, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
        {
            std::cerr << "Не удалось подключиться к " << serverIp << ":" << serverPort << std::endl;
            disconnect();
            return false;
        }

        std::cout << "Видео: подключено к " << serverIp << ":" << serverPort << std::endl;

        return true;
    }

    // Читает ровно length байт в buffer. false — соединение разорвано.
    bool receiveExact(char* buffer, size_t length)
    {
        size_t totalReceived = 0;

        while (totalReceived < length)
        {
            int received = recv(socketFd, buffer + totalReceived, static_cast<int>(length - totalReceived), 0);

            if (received <= 0)
            {
                return false;
            }

            totalReceived += static_cast<size_t>(received);
        }

        return true;
    }

    // Принимает один кадр формата [uint32 размер][JPEG-данные].
    bool receiveFrame(std::vector<uchar>& outJpegBuffer)
    {
        uint32_t frameSizeNetworkOrder = 0;

        if (!receiveExact(reinterpret_cast<char*>(&frameSizeNetworkOrder), sizeof(frameSizeNetworkOrder)))
        {
            return false;
        }

        uint32_t frameSize = ntohl(frameSizeNetworkOrder);

        // Защита от повреждённых/некорректных данных — не выделяем
        // безумные объёмы памяти по битому заголовку.
        constexpr uint32_t MAX_REASONABLE_FRAME_SIZE = 10 * 1024 * 1024;

        if (frameSize == 0 || frameSize > MAX_REASONABLE_FRAME_SIZE)
        {
            return false;
        }

        outJpegBuffer.resize(frameSize);

        return receiveExact(reinterpret_cast<char*>(outJpegBuffer.data()), frameSize);
    }

    void disconnect()
    {
        if (socketFd >= 0)
        {
            CLOSE_SOCKET(socketFd);
            socketFd = -1;
        }
    }

    ~VideoTcpClient()
    {
        disconnect();
    }
};


// ============================================================
// Отображение видео
// ============================================================

class VideoDisplay
{
private:
    static constexpr const char* WINDOW_NAME = "OmegaBot — видео с робота";

public:
    void showFrame(const std::vector<uchar>& jpegBuffer)
    {
        cv::Mat frame = cv::imdecode(jpegBuffer, cv::IMREAD_COLOR);

        if (frame.empty())
        {
            return;
        }

        cv::imshow(WINDOW_NAME, frame);
    }

    // Возвращает false, если пользователь запросил выход (Q / Esc).
    bool pollExitRequested()
    {
        int key = cv::waitKey(1);
        return key == 'q' || key == 'Q' || key == 27; // 27 = Esc
    }
};


int main(int argc, char* argv[])
{
    // IP Raspberry Pi можно передать аргументом:
    //   video_client.exe 10.122.144.232
    std::string serverIp = "10.122.144.232";

    if (argc > 1)
    {
        serverIp = argv[1];
    }

    const int SERVER_PORT = 5001;

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }
#endif

    VideoTcpClient client(serverIp, SERVER_PORT);

    if (!client.connectToServer())
    {
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "\nОкно видео открыто. Q или Esc — выход.\n" << std::endl;

    VideoDisplay display;
    std::vector<uchar> jpegBuffer;
    bool running = true;

    while (running)
    {
        if (!client.receiveFrame(jpegBuffer))
        {
            std::cerr << "Соединение с видео-сервером потеряно." << std::endl;
            break;
        }

        display.showFrame(jpegBuffer);

        if (display.pollExitRequested())
        {
            running = false;
        }
    }

    client.disconnect();

#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "Клиент видео завершён." << std::endl;
    return 0;
}
