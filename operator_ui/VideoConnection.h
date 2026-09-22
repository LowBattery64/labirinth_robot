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
    void errorOccurred(const QString& message);

private slots:
    void onReadyRead();

private:
    QTcpSocket socket;
    QByteArray buffer;
    quint32 expectedFrameSize = 0;
    int frameCounter = 0;
    qint64 statisticsStart = 0;
};
