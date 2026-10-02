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

        if (reconnectEnabled && socket.state() == QAbstractSocket::UnconnectedState)
            reconnectTimer.start();
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

void RobotConnection::armAutoReturn()
{
    sendControlByte('A');
}

void RobotConnection::disarmAutoReturn()
{
    sendControlByte('a');
}

void RobotConnection::setRammingEnabled(bool enabled)
{
    sendControlByte(enabled ? 'X' : 'x');
}

void RobotConnection::sendControlByte(char byte)
{
    if (socket.state() != QAbstractSocket::ConnectedState)
        return;

    socket.write(QByteArray(1, byte));
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

        const QByteArray rawLine = receiveBuffer.left(newline);
        receiveBuffer.remove(0, newline + 1);

        const QString line = QString::fromUtf8(rawLine).trimmed();

        if (line.startsWith("E,")) {
            const int separator = line.indexOf(',', 2);

            if (separator > 2) {
                emit serverEventReceived(
                    line.mid(2, separator - 2),
                    line.mid(separator + 1)
                );
            }

            continue;
        }

        const Telemetry telemetry = parseTelemetry(line);

        if (telemetry.valid)
            emit telemetryUpdated(telemetry);
    }
}
