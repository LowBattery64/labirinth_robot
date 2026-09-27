#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

#include "Telemetry.h"

class RobotConnection : public QObject
{
    Q_OBJECT

public:
    explicit RobotConnection(QObject* parent = nullptr);

    void connectToRobot(const QString& address, quint16 port = 5000);
    void disconnectFromRobot();
    void setCommand(QChar command);
    void stop();

signals:
    void connectionChanged(bool connected);
    void telemetryUpdated(const Telemetry& telemetry);
    void serverEventReceived(
        const QString& serverTimestamp,
        const QString& message
    );
    void errorOccurred(const QString& message);

private slots:
    void onReadyRead();
    void sendCurrentCommand();

private:
    QTcpSocket socket;
    QTimer commandTimer;
    QTimer reconnectTimer;
    QString robotAddress;
    quint16 robotPort = 5000;
    bool reconnectEnabled = false;
    QByteArray receiveBuffer;
    QChar currentCommand = 'S';
};