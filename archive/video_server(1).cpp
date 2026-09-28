// ============================================================
// OmegaBot — сервер видеопотока (Raspberry Pi)
// ------------------------------------------------------------
// Отдельный процесс от labirinth_server.cpp: работает на своём
// порту (5001) и не затрагивает канал управления (порт 5000).
// Захватывает кадры с USB-камеры, кодирует их в JPEG и шлёт
// по TCP: [4 байта размер][JPEG-данные][4 байта размер][...]
// ============================================================

#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>


// ============================================================
// Настройки видео
// ============================================================

class VideoSettings
{
public:
    static constexpr int CAMERA_INDEX = 0;

    static constexpr int FRAME_WIDTH = 640;
    static constexpr int FRAME_HEIGHT = 480;
    static constexpr int TARGET_FPS = 15;

    // Качество JPEG (0-100). Это и есть тот параметр, которым
    // в будущем можно управлять загрузкой радиоканала —
    // см. requirement "Управление загрузкой радиоканала".
    static constexpr int JPEG_QUALITY = 60;
};


// ============================================================
// Захват и кодирование кадров с камеры
// ============================================================

class CameraCapture
{
private:
    cv::VideoCapture capture;
    std::vector<int> jpegParams;

public:
    CameraCapture()
        : jpegParams{cv::IMWRITE_JPEG_QUALITY, VideoSettings::JPEG_QUALITY}
    {
    }

    bool open()
    {
        capture.open(VideoSettings::CAMERA_INDEX);

        if (!capture.isOpened())
        {
            return false;
        }

        capture.set(cv::CAP_PROP_FRAME_WIDTH, VideoSettings::FRAME_WIDTH);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, VideoSettings::FRAME_HEIGHT);
        capture.set(cv::CAP_PROP_FPS, VideoSettings::TARGET_FPS);

        return true;
    }

    // Возвращает false, если кадр не удалось получить
    // (камера отключилась / потеряна — не должно "ронять" сервер).
    bool captureFrameAsJpeg(std::vector<uchar>& outJpegBuffer)
    {
        cv::Mat frame;

        if (!capture.read(frame) || frame.empty())
        {
            return false;
        }

        return cv::imencode(".jpg", frame, outJpegBuffer, jpegParams);
    }

    void setJpegQuality(int quality)
    {
        jpegParams[1] = quality;
    }

    bool isOpened() const
    {
        return capture.isOpened();
    }

    ~CameraCapture()
    {
        if (capture.isOpened())
        {
            capture.release();
        }
    }
};


// ============================================================
// TCP-сервер видеоканала
// ============================================================

class VideoNetworkController
{
private:
    const int serverPort;

    int serverSocket;
    int clientSocket;

public:
    explicit VideoNetworkController(int port)
        : serverPort(port),
          serverSocket(-1),
          clientSocket(-1)
    {
    }

    bool initialize()
    {
        serverSocket = socket(AF_INET, SOCK_STREAM, 0);

        if (serverSocket < 0)
        {
            perror("video: socket");
            return false;
        }

        int enableReuse = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &enableReuse, sizeof(enableReuse));

        sockaddr_in serverAddress{};
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_addr.s_addr = INADDR_ANY;
        serverAddress.sin_port = htons(serverPort);

        if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0)
        {
            perror("video: bind");
            return false;
        }

        if (listen(serverSocket, 1) < 0)
        {
            perror("video: listen");
            return false;
        }

        std::cout << "Видео-сервер запущен на порту " << serverPort << std::endl;

        return true;
    }

    bool waitForClient()
    {
        sockaddr_in clientAddress{};
        socklen_t clientAddressLength = sizeof(clientAddress);

        std::cout << "Ожидание подключения ПК (видео)..." << std::endl;

        clientSocket = accept(serverSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientAddressLength);

        if (clientSocket < 0)
        {
            perror("video: accept");
            return false;
        }

        // Отключаем алгоритм Нейгла — каждый кадр должен уходить сразу,
        // без накопления мелких пакетов, иначе видео будет "тормозить".
        int noDelay = 1;
        setsockopt(clientSocket, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay));

        std::cout << "ПК подключён (видео): " << inet_ntoa(clientAddress.sin_addr) << std::endl;

        return true;
    }

    bool isClientConnected() const
    {
        return clientSocket >= 0;
    }

    // Шлёт один JPEG-кадр в формате [uint32 размер][данные].
    // Возвращает false при разрыве соединения.
    bool sendFrame(const std::vector<uchar>& jpegBuffer)
    {
        if (clientSocket < 0)
        {
            return false;
        }

        uint32_t frameSize = static_cast<uint32_t>(jpegBuffer.size());
        uint32_t frameSizeNetworkOrder = htonl(frameSize);

        if (!sendAll(reinterpret_cast<const char*>(&frameSizeNetworkOrder), sizeof(frameSizeNetworkOrder)))
        {
            return false;
        }

        return sendAll(reinterpret_cast<const char*>(jpegBuffer.data()), jpegBuffer.size());
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

    ~VideoNetworkController()
    {
        closeServer();
    }

private:
    bool sendAll(const char* data, size_t length)
    {
        size_t totalSent = 0;

        while (totalSent < length)
        {
            ssize_t sent = send(clientSocket, data + totalSent, length - totalSent, MSG_NOSIGNAL);

            if (sent <= 0)
            {
                return false;
            }

            totalSent += static_cast<size_t>(sent);
        }

        return true;
    }
};


// ============================================================
// Основной цикл видеосервера
// ============================================================

class VideoServer
{
private:
    static constexpr int SERVER_PORT = 5001;

    CameraCapture cameraCapture;
    VideoNetworkController networkController;

public:
    VideoServer()
        : networkController(SERVER_PORT)
    {
    }

    int run()
    {
        std::cout << "OmegaBot Video Server\n" << std::endl;

        if (!initialize())
        {
            return 1;
        }

        // Сервер переживает разрыв связи с ПК и повторные подключения —
        // это важно для требования "внезапная потеря связи -> продолжение работы".
        while (true)
        {
            if (!networkController.waitForClient())
            {
                return 1;
            }

            streamFramesUntilDisconnected();

            networkController.closeClient();
        }
    }

private:
    bool initialize()
    {
        if (!cameraCapture.open())
        {
            std::cerr << "Не удалось открыть камеру (индекс "
                      << VideoSettings::CAMERA_INDEX << ")." << std::endl;
            return false;
        }

        std::cout << "Камера открыта." << std::endl;

        return networkController.initialize();
    }

    void streamFramesUntilDisconnected()
    {
        std::vector<uchar> jpegBuffer;

        while (networkController.isClientConnected())
        {
            if (!cameraCapture.captureFrameAsJpeg(jpegBuffer))
            {
                std::cerr << "Кадр с камеры не получен." << std::endl;
                continue;
            }

            if (!networkController.sendFrame(jpegBuffer))
            {
                std::cout << "ПК отключён (видео)." << std::endl;
                break;
            }
        }
    }
};


int main()
{
    VideoServer videoServer;

    return videoServer.run();
}
