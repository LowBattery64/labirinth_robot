#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

class VideoSettings
{
public:
    static constexpr int DefaultCameraIndex = 0;
    static constexpr int FrameWidth = 640;
    static constexpr int FrameHeight = 480;
    static constexpr int TargetFps = 15;
    static constexpr int DefaultJpegQuality = 60;
    static constexpr int DefaultSegmentSeconds = 600;
};

class CameraCapture
{
public:
    bool open(int cameraIndex)
    {
        if (!capture.open(cameraIndex, cv::CAP_V4L2))
            return false;

        capture.set(cv::CAP_PROP_FRAME_WIDTH, VideoSettings::FrameWidth);
        capture.set(cv::CAP_PROP_FRAME_HEIGHT, VideoSettings::FrameHeight);
        capture.set(cv::CAP_PROP_FPS, VideoSettings::TargetFps);

        return capture.isOpened();
    }

    bool captureJpeg(std::vector<uchar>& output, int jpegQuality)
    {
        cv::Mat frame;

        if (!capture.read(frame) || frame.empty())
            return false;

        const std::vector<int> parameters{
            cv::IMWRITE_JPEG_QUALITY,
            std::clamp(jpegQuality, 1, 100)
        };

        return cv::imencode(".jpg", frame, output, parameters);
    }

    ~CameraCapture()
    {
        if (capture.isOpened())
            capture.release();
    }

private:
    cv::VideoCapture capture;
};

class VideoRecorder
{
public:
    VideoRecorder(const fs::path& directory, int segmentSeconds)
        : outputDirectory(directory),
          segmentSeconds(segmentSeconds)
    {
        fs::create_directories(outputDirectory);
    }

    void start()
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (recording)
            return;

        recording = true;
        segmentStarted = std::chrono::steady_clock::now();
    }

    void stop()
    {
        std::lock_guard<std::mutex> lock(mutex);
        recording = false;
        releaseWriter();
    }

    bool isRecording() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return recording;
    }

    void write(const std::vector<uchar>& jpegFrame)
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (!recording)
            return;

        cv::Mat frame = cv::imdecode(jpegFrame, cv::IMREAD_COLOR);

        if (frame.empty())
            return;

        const auto now = std::chrono::steady_clock::now();

        if (!writer.isOpened() ||
            frame.size() != frameSize ||
            std::chrono::duration_cast<std::chrono::seconds>(
                now - segmentStarted
            ).count() >= segmentSeconds)
        {
            openWriter(frame.size(), now);
        }

        if (writer.isOpened())
            writer.write(frame);
    }

private:
    void openWriter(
        const cv::Size& size,
        std::chrono::steady_clock::time_point now
    )
    {
        releaseWriter();

        const auto systemNow = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(systemNow);

        std::tm localTime{};
        localtime_r(&time, &localTime);

        char timestamp[32];
        std::strftime(
            timestamp,
            sizeof(timestamp),
            "%Y%m%d_%H%M%S",
            &localTime
        );

        const fs::path outputPath =
            outputDirectory /
            (std::string("omegabot_usb_") + timestamp + ".avi");

        writer.open(
            outputPath.string(),
            cv::VideoWriter::fourcc('M', 'J', 'P', 'G'),
            VideoSettings::TargetFps,
            size
        );

        if (!writer.isOpened())
        {
            std::cerr
                << "Не удалось открыть файл записи: "
                << outputPath
                << std::endl;
            return;
        }

        frameSize = size;
        segmentStarted = now;

        std::cout
            << "Запись: "
            << outputPath
            << std::endl;
    }

    void releaseWriter()
    {
        if (writer.isOpened())
            writer.release();
    }

    fs::path outputDirectory;
    int segmentSeconds;

    mutable std::mutex mutex;
    bool recording = false;

    cv::VideoWriter writer;
    cv::Size frameSize;
    std::chrono::steady_clock::time_point segmentStarted;
};

class VideoProcessor
{
public:
    explicit VideoProcessor(
        const fs::path& recordDirectory,
        int segmentSeconds
    )
        : recorder(recordDirectory, segmentSeconds)
    {
    }

    void setQuality(const std::string& value)
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (value == "HIGH")
            jpegQuality = 85;
        else if (value == "MEDIUM")
            jpegQuality = 70;
        else if (value == "LOW")
            jpegQuality = 55;
        else
            return;

        quality = value;
    }

    std::string getQuality() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return quality;
    }

    void startRecording()
    {
        recorder.start();
    }

    void stopRecording()
    {
        recorder.stop();
    }

    bool isRecording() const
    {
        return recorder.isRecording();
    }

    std::vector<uchar> process(const std::vector<uchar>& source)
    {
        int qualityValue;

        {
            std::lock_guard<std::mutex> lock(mutex);
            qualityValue = jpegQuality;
        }

        std::vector<uchar> result;

        if (qualityValue == 85)
        {
            result = source;
        }
        else
        {
            cv::Mat frame =
                cv::imdecode(source, cv::IMREAD_COLOR);

            if (frame.empty())
                return source;

            if (qualityValue == 70)
            {
                cv::resize(
                    frame,
                    frame,
                    cv::Size(),
                    0.75,
                    0.75,
                    cv::INTER_AREA
                );
            }
            else
            {
                cv::resize(
                    frame,
                    frame,
                    cv::Size(),
                    0.5,
                    0.5,
                    cv::INTER_AREA
                );
            }

            const std::vector<int> parameters{
                cv::IMWRITE_JPEG_QUALITY,
                qualityValue
            };

            if (!cv::imencode(
                    ".jpg",
                    frame,
                    result,
                    parameters
                ))
            {
                result = source;
            }
        }

        recorder.write(result);

        return result;
    }

private:
    mutable std::mutex mutex;

    std::string quality = "HIGH";
    int jpegQuality = 85;

    VideoRecorder recorder;
};

class TcpVideoServer
{
public:
    explicit TcpVideoServer(int port)
        : port(port)
    {
    }

    bool initialize()
    {
        serverSocket =
            socket(AF_INET, SOCK_STREAM, 0);

        if (serverSocket < 0)
            return false;

        int reuse = 1;

        setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)
        );

        sockaddr_in address{};

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(
                serverSocket,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)
            ) < 0)
        {
            return false;
        }

        return listen(serverSocket, 1) >= 0;
    }

    bool waitForClient()
    {
        sockaddr_in clientAddress{};
        socklen_t length = sizeof(clientAddress);

        clientSocket = accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &length
        );

        if (clientSocket < 0)
            return false;

        int noDelay = 1;

        setsockopt(
            clientSocket,
            IPPROTO_TCP,
            TCP_NODELAY,
            &noDelay,
            sizeof(noDelay)
        );

        return true;
    }

    bool sendFrame(const std::vector<uchar>& frame)
    {
        if (clientSocket < 0)
            return false;

        const uint32_t size =
            htonl(static_cast<uint32_t>(frame.size()));

        return sendAll(
                   reinterpret_cast<const char*>(&size),
                   sizeof(size)
               ) &&
               sendAll(
                   reinterpret_cast<const char*>(frame.data()),
                   frame.size()
               );
    }

    void closeClient()
    {
        if (clientSocket >= 0)
        {
            shutdown(clientSocket, SHUT_RDWR);
            close(clientSocket);
            clientSocket = -1;
        }
    }

    ~TcpVideoServer()
    {
        closeClient();

        if (serverSocket >= 0)
            close(serverSocket);
    }

private:
    bool sendAll(const char* data, size_t length)
    {
        size_t sentTotal = 0;

        while (sentTotal < length)
        {
            const ssize_t sent = send(
                clientSocket,
                data + sentTotal,
                length - sentTotal,
                MSG_NOSIGNAL
            );

            if (sent <= 0)
                return false;

            sentTotal += static_cast<size_t>(sent);
        }

        return true;
    }

    int port;

    int serverSocket = -1;
    int clientSocket = -1;
};

class VideoControlServer
{
public:
    VideoControlServer(
        int port,
        VideoProcessor& processor
    )
        : port(port),
          processor(processor)
    {
    }

    bool initialize()
    {
        serverSocket =
            socket(AF_INET, SOCK_STREAM, 0);

        if (serverSocket < 0)
            return false;

        int reuse = 1;

        setsockopt(
            serverSocket,
            SOL_SOCKET,
            SO_REUSEADDR,
            &reuse,
            sizeof(reuse)
        );

        sockaddr_in address{};

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(
                serverSocket,
                reinterpret_cast<sockaddr*>(&address),
                sizeof(address)
            ) < 0)
        {
            return false;
        }

        if (listen(serverSocket, 4) < 0)
            return false;

        std::thread(
            &VideoControlServer::acceptLoop,
            this
        ).detach();

        return true;
    }

    ~VideoControlServer()
    {
        if (serverSocket >= 0)
            close(serverSocket);
    }

private:
    void acceptLoop()
    {
        while (true)
        {
            sockaddr_in clientAddress{};
            socklen_t length = sizeof(clientAddress);

            const int client = accept(
                serverSocket,
                reinterpret_cast<sockaddr*>(&clientAddress),
                &length
            );

            if (client < 0)
                continue;

            std::thread(
                &VideoControlServer::clientLoop,
                this,
                client
            ).detach();
        }
    }

    void clientLoop(int client)
    {
        sendLine(
            client,
            "QUALITY " + processor.getQuality()
        );

        sendLine(
            client,
            std::string("RECORD ") +
            (processor.isRecording() ? "ON" : "OFF")
        );

        char buffer[1024];
        std::string pending;

        while (true)
        {
            const ssize_t received =
                recv(
                    client,
                    buffer,
                    sizeof(buffer),
                    0
                );

            if (received <= 0)
                break;

            pending.append(
                buffer,
                static_cast<size_t>(received)
            );

            size_t newline;

            while (
                (newline = pending.find('\n'))
                != std::string::npos
            )
            {
                std::string command =
                    pending.substr(0, newline);

                pending.erase(
                    0,
                    newline + 1
                );

                while (
                    !command.empty() &&
                    (
                        command.back() == '\r' ||
                        command.back() == ' '
                    )
                )
                {
                    command.pop_back();
                }

                sendLine(
                    client,
                    handleCommand(command)
                );
            }
        }

        close(client);
    }

    std::string handleCommand(
        const std::string& command
    )
    {
        if (command == "STATUS")
        {
            return
                "QUALITY " +
                processor.getQuality() +
                "\nRECORD " +
                (
                    processor.isRecording()
                        ? "ON"
                        : "OFF"
                );
        }

        if (command == "QUALITY HIGH")
        {
            processor.setQuality("HIGH");
            return "QUALITY HIGH";
        }

        if (command == "QUALITY MEDIUM")
        {
            processor.setQuality("MEDIUM");
            return "QUALITY MEDIUM";
        }

        if (command == "QUALITY LOW")
        {
            processor.setQuality("LOW");
            return "QUALITY LOW";
        }

        if (command == "RECORD START")
        {
            processor.startRecording();
            return "RECORD ON";
        }

        if (command == "RECORD STOP")
        {
            processor.stopRecording();
            return "RECORD OFF";
        }

        return "ERROR Неизвестная команда";
    }

    static void sendLine(
        int socket,
        const std::string& text
    )
    {
        const std::string message =
            text + "\n";

        send(
            socket,
            message.data(),
            message.size(),
            MSG_NOSIGNAL
        );
    }

    int port;
    int serverSocket = -1;

    VideoProcessor& processor;
};

class UsbCameraVideoServer
{
public:
    UsbCameraVideoServer(
        int cameraIndex,
        int videoPort,
        int controlPort,
        const fs::path& recordDirectory,
        int segmentSeconds
    )
        : cameraIndex(cameraIndex),
          videoServer(videoPort),
          processor(recordDirectory, segmentSeconds),
          controlServer(controlPort, processor),
          recordDirectory(recordDirectory),
          segmentSeconds(segmentSeconds)
    {
    }

    int run()
    {
        std::cout
            << "OmegaBot USB Camera Video Server"
            << std::endl;

        if (!camera.open(cameraIndex))
        {
            std::cerr
                << "Не удалось открыть USB-камеру, индекс "
                << cameraIndex
                << "."
                << std::endl;

            return 1;
        }

        if (!videoServer.initialize())
        {
            std::cerr
                << "Не удалось запустить TCP-видеосервер."
                << std::endl;

            return 1;
        }

        if (!controlServer.initialize())
        {
            std::cerr
                << "Не удалось запустить канал управления видео."
                << std::endl;

            return 1;
        }

        std::cout
            << "USB-камера открыта."
            << std::endl;

        std::cout
            << "Видео: TCP "
            << 5001
            << std::endl;

        std::cout
            << "Управление видео: TCP "
            << 5002
            << std::endl;

        std::cout
            << "Запись: "
            << recordDirectory
            << std::endl;

        std::vector<uchar> frame;

        while (true)
        {
            if (!videoServer.waitForClient())
                continue;

            while (
                camera.captureJpeg(
                    frame,
                    getJpegQuality()
                )
            )
            {
                const std::vector<uchar> processed =
                    processor.process(frame);

                if (!videoServer.sendFrame(processed))
                    break;
            }

            videoServer.closeClient();
        }

        return 0;
    }

private:
    int getJpegQuality() const
    {
        const std::string quality =
            processor.getQuality();

        if (quality == "HIGH")
            return 85;

        if (quality == "MEDIUM")
            return 70;

        return 55;
    }

    int cameraIndex;

    TcpVideoServer videoServer;

    VideoProcessor processor;

    VideoControlServer controlServer;

    CameraCapture camera;

    fs::path recordDirectory;

    int segmentSeconds;
};

int main(int argc, char* argv[])
{
    int cameraIndex =
        VideoSettings::DefaultCameraIndex;

    int videoPort = 5001;
    int controlPort = 5002;

    int segmentSeconds =
        VideoSettings::DefaultSegmentSeconds;

    fs::path recordDirectory =
        "/media/raspberry/76E8-CACF/omegabot_recordings";

    if (argc > 1)
        cameraIndex = std::stoi(argv[1]);

    if (argc > 2)
        recordDirectory = argv[2];

    if (argc > 3)
        segmentSeconds = std::stoi(argv[3]);

    UsbCameraVideoServer server(
        cameraIndex,
        videoPort,
        controlPort,
        recordDirectory,
        segmentSeconds
    );

    return server.run();
}
