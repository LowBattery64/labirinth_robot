#pragma once
#include <QMainWindow>
#include <QLabel>
#include <QTextEdit>
#include <QKeyEvent>
#include "RobotConnection.h"
#include "VideoConnection.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
private:
    void setupUi();
    void setupConnections();
    void setCommand(QChar command);
    void updateTelemetry(const Telemetry& telemetry);
    void appendLog(const QString& message);

    QLabel* videoLabel = nullptr;
    QLabel* robotStatus = nullptr;
    QLabel* videoStatus = nullptr;
    QLabel* motionStatus = nullptr;
    QLabel* safetyStatus = nullptr;
    QLabel* distanceStatus = nullptr;
    QLabel* videoStats = nullptr;
    QTextEdit* logView = nullptr;
    RobotConnection robot;
    VideoConnection video;
};
