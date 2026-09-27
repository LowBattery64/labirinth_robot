#pragma once
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QImage>

class VideoConnection : public QObject
{
    Q_OBJECT

public:
    explicit VideoConnection(QObject* parent = nullptr);
    void connectToCameraBridge(const QString& address, quint16 port = 5001);
    void disconnectFromCameraBridge();

signals:
    void connectionChanged(bool connected);
    void frameReady(const QImage& frame);
    void statisticsUpdated(double fps, int jpegBytes);
    void qualityChanged(const QString& quality);
    void recordingChanged(bool recording);
    void controlError(const QString& message);
    void errorOccurred(const QString& message);

private slots:
    void onReadyRead();

private:
    QTcpSocket socket;
    QTcpSocket controlSocket;
    QTimer reconnectTimer;
    QTimer controlReconnectTimer;
    QString cameraAddress;
    quint16 cameraPort = 5001;
    bool reconnectEnabled = false;
    QString selectedQuality = "HIGH";
    bool recording = false;
    QByteArray controlBuffer;
    QByteArray buffer;
    quint32 expectedFrameSize = 0;
    int frameCounter = 0;
    qint64 statisticsStart = 0;

    void connectControl();
    void sendControlCommand(const QString& command);
    void onControlReadyRead();
};
