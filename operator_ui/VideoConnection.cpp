#include "VideoConnection.h"
#include <QDataStream>
#include <QtEndian>
#include <QDateTime>

VideoConnection::VideoConnection(QObject* parent) : QObject(parent)
{
    connect(&socket, &QTcpSocket::readyRead, this, &VideoConnection::onReadyRead);
    connect(&socket, &QTcpSocket::connected, this, [this] {
        reconnectTimer.stop();
        buffer.clear();
        expectedFrameSize = 0;
        frameCounter = 0;
        statisticsStart = QDateTime::currentMSecsSinceEpoch();
        emit connectionChanged(true);
    });
    connect(&socket, &QTcpSocket::disconnected, this, [this] {
        emit connectionChanged(false);

        if (reconnectEnabled)
            reconnectTimer.start();
    });
    connect(&socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit errorOccurred(socket.errorString());
    });

    reconnectTimer.setInterval(1000);
    connect(&reconnectTimer, &QTimer::timeout, this, [this] {
        if (reconnectEnabled && socket.state() == QAbstractSocket::UnconnectedState)
            socket.connectToHost(cameraAddress, cameraPort);
    });
}

void VideoConnection::connectToCameraBridge(const QString& address, quint16 port)
{
    cameraAddress = address;
    cameraPort = port;
    reconnectEnabled = true;

    socket.abort();
    socket.connectToHost(cameraAddress, cameraPort);
}

void VideoConnection::disconnectFromCameraBridge()
{
    reconnectEnabled = false;
    reconnectTimer.stop();
    socket.disconnectFromHost();
}

void VideoConnection::onReadyRead()
{
    buffer += socket.readAll();

    while (true) {
        if (expectedFrameSize == 0) {
            if (buffer.size() < 4)
                return;

            expectedFrameSize = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar*>(buffer.constData()));
            buffer.remove(0, 4);

            if (expectedFrameSize == 0 || expectedFrameSize > 10 * 1024 * 1024) {
                socket.abort();
                emit errorOccurred("Некорректный размер видеокадра");
                return;
            }
        }

        if (buffer.size() < static_cast<int>(expectedFrameSize))
            return;

        const QByteArray jpeg = buffer.left(expectedFrameSize);
        buffer.remove(0, expectedFrameSize);
        expectedFrameSize = 0;

        const QImage frame = QImage::fromData(jpeg, "JPG");
        if (frame.isNull()) {
            emit errorOccurred("Не удалось декодировать JPEG");
            continue;
        }

        emit frameReady(frame);
        ++frameCounter;

        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - statisticsStart;
        if (elapsed >= 1000) {
            emit statisticsUpdated(frameCounter * 1000.0 / elapsed, jpeg.size());
            frameCounter = 0;
            statisticsStart = QDateTime::currentMSecsSinceEpoch();
        }
    }
}
