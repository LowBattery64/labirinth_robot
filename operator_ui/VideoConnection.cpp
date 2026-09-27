#include "VideoConnection.h"
#include <QDataStream>
#include <QtEndian>
#include <QDateTime>

VideoConnection::VideoConnection(QObject* parent) : QObject(parent)
{
    connect(&socket, &QTcpSocket::readyRead, this, &VideoConnection::onReadyRead);
    connect(&controlSocket, &QTcpSocket::readyRead, this, &VideoConnection::onControlReadyRead);

    connect(&controlSocket, &QTcpSocket::connected, this, [this] {
        controlReconnectTimer.stop();
        sendControlCommand("STATUS");
    });

    connect(&controlSocket, &QTcpSocket::disconnected, this, [this] {
        if (reconnectEnabled)
            controlReconnectTimer.start();
    });

    connect(&controlSocket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit controlError(controlSocket.errorString());
        if (reconnectEnabled)
            controlReconnectTimer.start();
    });
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

    controlReconnectTimer.setInterval(1000);
    connect(&controlReconnectTimer, &QTimer::timeout, this, [this] {
        connectControl();
    });
}

void VideoConnection::connectToCameraBridge(const QString& address, quint16 port)
{
    cameraAddress = address;
    cameraPort = port;
    reconnectEnabled = true;

    socket.abort();
    socket.connectToHost(cameraAddress, cameraPort);
    connectControl();
}

void VideoConnection::disconnectFromCameraBridge()
{
    reconnectEnabled = false;
    reconnectTimer.stop();
    controlReconnectTimer.stop();
    socket.disconnectFromHost();
    controlSocket.disconnectFromHost();
}

void VideoConnection::connectControl()
{
    if (!reconnectEnabled || controlSocket.state() != QAbstractSocket::UnconnectedState)
        return;

    controlSocket.abort();
    controlSocket.connectToHost(cameraAddress, 5002);
}

void VideoConnection::sendControlCommand(const QString& command)
{
    if (controlSocket.state() != QAbstractSocket::ConnectedState)
        return;

    controlSocket.write(command.toUtf8() + "\n");
}

void VideoConnection::setQuality(const QString& quality)
{
    selectedQuality = quality.toUpper();
    sendControlCommand("QUALITY " + selectedQuality);
}

void VideoConnection::startRecording()
{
    sendControlCommand("RECORD START");
}

void VideoConnection::stopRecording()
{
    sendControlCommand("RECORD STOP");
}

void VideoConnection::onControlReadyRead()
{
    controlBuffer += controlSocket.readAll();

    while (true) {
        const int newline = controlBuffer.indexOf('\n');
        if (newline < 0)
            return;

        const QString line = QString::fromUtf8(controlBuffer.left(newline)).trimmed();
        controlBuffer.remove(0, newline + 1);

        if (line.startsWith("QUALITY ")) {
            const QString quality = line.mid(8).trimmed();
            selectedQuality = quality;
            emit qualityChanged(quality);
        } else if (line == "RECORD ON") {
            recording = true;
            emit recordingChanged(true);
        } else if (line == "RECORD OFF") {
            recording = false;
            emit recordingChanged(false);
        } else if (line.startsWith("ERROR ")) {
            emit controlError(line.mid(6));
        }
    }
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
