#include "RobotConnection.h"

RobotConnection::RobotConnection(QObject* parent) : QObject(parent)
{
    connect(&socket, &QTcpSocket::readyRead, this, &RobotConnection::onReadyRead);
    connect(&socket, &QTcpSocket::connected, this, [this] {
        reconnectTimer.stop();
        currentCommand = 'S';
        sendCurrentCommand();
        emit connectionChanged(true);
    });
    connect(&socket, &QTcpSocket::disconnected, this, [this] {
        currentCommand = 'S';
        emit connectionChanged(false);

        if (reconnectEnabled)
            reconnectTimer.start();
    });
    connect(&socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        emit errorOccurred(socket.errorString());
    });

    commandTimer.setInterval(100);
    connect(&commandTimer, &QTimer::timeout, this, &RobotConnection::sendCurrentCommand);

    reconnectTimer.setInterval(1000);
    connect(&reconnectTimer, &QTimer::timeout, this, [this] {
        if (reconnectEnabled && socket.state() == QAbstractSocket::UnconnectedState)
            socket.connectToHost(robotAddress, robotPort);
    });
}

void RobotConnection::connectToRobot(const QString& address, quint16 port)
{
    robotAddress = address;
    robotPort = port;
    reconnectEnabled = true;

    socket.abort();
    socket.connectToHost(robotAddress, robotPort);
    commandTimer.start();
}

void RobotConnection::disconnectFromRobot()
{
    reconnectEnabled = false;
    reconnectTimer.stop();
    stop();
    commandTimer.stop();
    socket.disconnectFromHost();
}

void RobotConnection::setCommand(QChar command)
{
    currentCommand = command;
    sendCurrentCommand();
}

void RobotConnection::stop()
{
    currentCommand = 'S';
    sendCurrentCommand();
}

void RobotConnection::sendCurrentCommand()
{
    if (socket.state() != QAbstractSocket::ConnectedState)
        return;

    socket.write(QByteArray(1, currentCommand.toLatin1()));
    socket.write("\n");
}

void RobotConnection::onReadyRead()
{
    receiveBuffer += socket.readAll();

    while (true) {
        const int newline = receiveBuffer.indexOf('\n');
        if (newline < 0)
            break;

        const QByteArray line = receiveBuffer.left(newline).trimmed();
        receiveBuffer.remove(0, newline + 1);

        const Telemetry telemetry = parseTelemetry(QString::fromLatin1(line));
        if (telemetry.valid)
            emit telemetryUpdated(telemetry);
    }
}
