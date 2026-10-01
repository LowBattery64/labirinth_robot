#include "MainWindow.h"
#include <QApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QPushButton>
#include <QPixmap>
#include <QStyle>

namespace {
QLabel* statusCard(const QString& title, QLabel*& value)
{
    auto* card = new QFrame;
    card->setObjectName("statusCard");
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->addWidget(new QLabel(title));
    value = new QLabel("—");
    value->setObjectName("cardValue");
    layout->addWidget(value);
    return value;
}

QPushButton* controlButton(const QString& text, QChar command)
{
    auto* button = new QPushButton(text);
    button->setProperty("command", command);
    button->setMinimumSize(76, 54);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setupUi();
    setupConnections();
    robot.connectToRobot("10.109.150.232");
    video.connectToCameraBridge("10.109.150.232");
}

MainWindow::~MainWindow()
{
    robot.stop();
    robot.disconnectFromRobot();
    video.disconnectFromCameraBridge();
}

void MainWindow::setupUi()
{
    setWindowTitle("Гослингмобиль — Drive");
    resize(1280, 800);
    setMinimumSize(1000, 650);

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel("Гослингмобиль");
    title->setObjectName("title");
    header->addWidget(title);
    header->addStretch();

    robotStatus = new QLabel("● ROBOT OFFLINE");
    robotStatus->setObjectName("offline");
    videoStatus = new QLabel("● VIDEO OFFLINE");
    videoStatus->setObjectName("offline");
    header->addWidget(robotStatus);
    header->addWidget(videoStatus);
    root->addLayout(header);

    auto* content = new QHBoxLayout;
    content->setSpacing(12);

    videoLabel = new QLabel("Ожидание видеопотока…");
    videoLabel->setAlignment(Qt::AlignCenter);
    videoLabel->setMinimumSize(700, 480);
    videoLabel->setObjectName("video");
    content->addWidget(videoLabel, 1);

    auto* controlFrame = new QFrame;
    controlFrame->setObjectName("panel");
    auto* controlLayout = new QVBoxLayout(controlFrame);

    auto* controlTitle = new QLabel("Drive");
    controlTitle->setObjectName("panelTitle");
    controlLayout->addWidget(controlTitle, 0, Qt::AlignCenter);

    auto* grid = new QGridLayout;
    auto* forward = controlButton("▲\nW / ↑", 'F');
    auto* left = controlButton("◀\nA / ←", 'L');
    auto* stop = controlButton("■\nSTOP", 'S');
    auto* right = controlButton("▶\nD / →", 'R');
    auto* backward = controlButton("▼\nS / ↓", 'B');
    grid->addWidget(forward, 0, 1);
    grid->addWidget(left, 1, 0);
    grid->addWidget(stop, 1, 1);
    grid->addWidget(right, 1, 2);
    grid->addWidget(backward, 2, 1);
    controlLayout->addLayout(grid);

    auto* help = new QLabel(
        "Команда сохраняется после нажатия.\n"
        "Остановка: S / Space / STOP.\n"
        "УЗ-защита блокирует движение вперёд.");
    help->setWordWrap(true);
    help->setObjectName("hint");
    controlLayout->addWidget(help);

    rammingButton = new QPushButton("ТАРАН: ВЫКЛ");
    rammingButton->setObjectName("ramButton");
    rammingButton->setCheckable(true);
    rammingButton->setMinimumHeight(44);
    controlLayout->addWidget(rammingButton);

    armButton = new QPushButton("ВКЛЮЧИТЬ АВТО-ВОЗВРАТ");
    armButton->setObjectName("armButton");
    armButton->setMinimumHeight(44);
    controlLayout->addWidget(armButton);

    auto* scoutHint = new QLabel(
        "Таран: отключает переднюю УЗ-защиту - робот поедет\n"
        "сквозь препятствия. Используйте осторожно.\n"
        "Авто-возврат: нажмите ПЕРЕД заездом в зону без связи.\n"
        "При потере связи робот сам проедет немного вперёд,\n"
        "развернётся и вернётся обратно в зону связи.");
    scoutHint->setWordWrap(true);
    scoutHint->setObjectName("hint");
    controlLayout->addWidget(scoutHint);

    auto* emergency = new QPushButton("АВАРИЙНАЯ ОСТАНОВКА");
    emergency->setObjectName("emergency");
    emergency->setMinimumHeight(52);
    controlLayout->addWidget(emergency);

    content->addWidget(controlFrame);
    root->addLayout(content, 1);

    auto* statusGrid = new QGridLayout;
    statusGrid->addWidget(statusCard("ДВИЖЕНИЕ", motionStatus), 0, 0);
    statusGrid->addWidget(statusCard("БЕЗОПАСНОСТЬ", safetyStatus), 0, 1);
    statusGrid->addWidget(statusCard("ПЕРЕД РОБОТОМ", distanceStatus), 0, 2);
    statusGrid->addWidget(statusCard("ВИДЕО", videoStats), 0, 3);
    statusGrid->addWidget(statusCard("АВТО-ВОЗВРАТ", autoReturnStatus), 0, 4);
    root->addLayout(statusGrid);

    auto* logButton = new QPushButton("Показать журнал");
    logButton->setCheckable(true);
    logView = new QTextEdit;
    logView->setReadOnly(true);
    logView->setMaximumHeight(130);
    logView->hide();

    auto* logHeader = new QHBoxLayout;
    logHeader->addWidget(new QLabel("ЖУРНАЛ ОПЕРАТОРА"));
    logHeader->addStretch();
    logHeader->addWidget(logButton);
    root->addLayout(logHeader);
    root->addWidget(logView);
    connect(logButton, &QPushButton::toggled, logView, &QWidget::setVisible);

    setCentralWidget(central);

    setStyleSheet(R"(
        QMainWindow, QWidget { background:#101419; color:#e8edf2; font-family:"Segoe UI"; font-size:14px; }
        #title { font-size:25px; font-weight:700; }
        #video { background:#050708; border:1px solid #303943; border-radius:8px; }
        #panel, #statusCard { background:#171d23; border:1px solid #303943; border-radius:8px; }
        #panelTitle { font-size:17px; font-weight:700; }
        #cardValue { font-size:17px; font-weight:700; }
        #hint { color:#8e9aa7; padding:8px; }
        QPushButton { background:#202831; border:1px solid #3b4652; border-radius:7px; padding:8px; }
        QPushButton:hover { background:#29333e; }
        QPushButton:pressed { background:#354250; }
        #emergency { background:#5a2525; border:1px solid #9e4444; font-weight:700; }
        #ramButton:checked { background:#5a2525; border:1px solid #9e4444; font-weight:700; }
        #armButton:disabled { color:#6b7580; }
        #offline { color:#e3a3a3; font-weight:600; }
        #online { color:#8bd69a; font-weight:600; }
        #warning { color:#f1c36d; font-weight:700; }
        #safe { color:#8bd69a; font-weight:700; }
    )");

    connect(emergency, &QPushButton::clicked, this, [this] { setCommand('S'); });

    connect(rammingButton, &QPushButton::toggled, this, [this](bool checked) {
        robot.setRammingEnabled(checked);
        rammingButton->setText(checked ? "ТАРАН: ВКЛ" : "ТАРАН: ВЫКЛ");
        appendLog(checked
            ? "Таран включён - передняя УЗ-защита отключена!"
            : "Таран выключен - защита снова активна");
    });

    connect(armButton, &QPushButton::clicked, this, [this] {
        robot.armAutoReturn();
        appendLog("Авто-возврат включён: при потере связи робот сам вернётся в зону связи");
    });

    for (auto* button : controlFrame->findChildren<QPushButton*>()) {
        if (!button->property("command").isValid())
            continue;
        connect(button, &QPushButton::clicked, this, [this, button] {
            setCommand(button->property("command").toChar());
        });
    }
}

void MainWindow::setupConnections()
{
    connect(&robot, &RobotConnection::connectionChanged, this, [this](bool connected) {
        robotStatus->setText(connected ? "● ROBOT ONLINE" : "● ROBOT OFFLINE");
        robotStatus->setObjectName(connected ? "online" : "offline");
        robotStatus->style()->unpolish(robotStatus);
        robotStatus->style()->polish(robotStatus);
        appendLog(connected ? "Связь с роботом установлена" : "Связь с роботом потеряна");
    });

    connect(&robot, &RobotConnection::telemetryUpdated, this, &MainWindow::updateTelemetry);
    connect(&robot, &RobotConnection::errorOccurred, this, [this](const QString& message) {
        appendLog("Ошибка робота: " + message);
    });

    connect(&video, &VideoConnection::connectionChanged, this, [this](bool connected) {
        videoStatus->setText(connected ? "● VIDEO LIVE" : "● VIDEO OFFLINE");
        videoStatus->setObjectName(connected ? "online" : "offline");
        videoStatus->style()->unpolish(videoStatus);
        videoStatus->style()->polish(videoStatus);
        appendLog(connected ? "Видеоканал подключён" : "Видеоканал потерян");
    });

    connect(&video, &VideoConnection::frameReady, this, [this](const QImage& frame) {
        videoLabel->setPixmap(QPixmap::fromImage(frame).scaled(
            videoLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    });

    connect(&video, &VideoConnection::statisticsUpdated, this, [this](double fps, int bytes) {
        videoStats->setText(QString("%1 FPS · %2 KB")
            .arg(fps, 0, 'f', 1).arg(bytes / 1024.0, 0, 'f', 1));
    });

    connect(&video, &VideoConnection::errorOccurred, this, [this](const QString& message) {
        appendLog("Ошибка видео: " + message);
    });
}

void MainWindow::setCommand(QChar command)
{
    robot.setCommand(command);
    const QString names = "FBLRS";
    const QString display[] = {"ВПЕРЁД", "НАЗАД", "ВЛЕВО", "ВПРАВО", "СТОП"};
    const int index = names.indexOf(command);
    if (index >= 0)
        motionStatus->setText(display[index]);
    appendLog("Команда: " + QString(command));
}

void MainWindow::updateTelemetry(const Telemetry& telemetry)
{
    if (!telemetry.valid)
        return;

    const bool obstacleFlag = telemetry.safeBlocked || telemetry.irBlocked;

    if (telemetry.rammingEnabled && obstacleFlag) {
        safetyStatus->setText("⚠ ТАРАН (защита откл.)");
        safetyStatus->setObjectName("warning");
    } else if (obstacleFlag) {
        safetyStatus->setText("⚠ ПРЕПЯТСТВИЕ");
        safetyStatus->setObjectName("warning");
    } else {
        safetyStatus->setText("● БЕЗОПАСНО");
        safetyStatus->setObjectName("safe");
    }
    safetyStatus->style()->unpolish(safetyStatus);
    safetyStatus->style()->polish(safetyStatus);

    distanceStatus->setText(telemetry.usCenter >= 0
        ? QString("%1 см").arg(telemetry.usCenter) : "—");

    // Режим авто-возврата с самой платы - источник истины на случай
    // переподключения или если кнопки на ПК и реальное состояние
    // робота почему-то разошлись.
    const bool idleArmed = telemetry.mode == "TELEOP" && telemetry.armedForReturn;
    const QString modeText =
        telemetry.mode == "TELEOP"
            ? (idleArmed ? "ВООРУЖЕН, ЖДЁТ" : "—")
            : telemetry.mode;
    autoReturnStatus->setText(modeText);
    autoReturnStatus->setObjectName(
        telemetry.mode == "TELEOP" ? (idleArmed ? "warning" : "safe") : "warning");
    autoReturnStatus->style()->unpolish(autoReturnStatus);
    autoReturnStatus->style()->polish(autoReturnStatus);

    rammingButton->blockSignals(true);
    rammingButton->setChecked(telemetry.rammingEnabled);
    rammingButton->blockSignals(false);
    rammingButton->setText(telemetry.rammingEnabled ? "ТАРАН: ВКЛ" : "ТАРАН: ВЫКЛ");

    // Пока авто-возврат уже активирован, повторное включение не требуется.
    armButton->setEnabled(telemetry.mode == "TELEOP" && !telemetry.armedForReturn);

    if (telemetry.watchdog)
        appendLog("Сработал watchdog: команда остановлена");
}

void MainWindow::appendLog(const QString& message)
{
    logView->append(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + message);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat())
        return;

    switch (event->key()) {
    case Qt::Key_W:
    case Qt::Key_Up: setCommand('F'); break;
    case Qt::Key_S:
    case Qt::Key_Down: setCommand('B'); break;
    case Qt::Key_A:
    case Qt::Key_Left: setCommand('L'); break;
    case Qt::Key_D:
    case Qt::Key_Right: setCommand('R'); break;
    case Qt::Key_Space: setCommand('S'); break;
    default: QMainWindow::keyPressEvent(event); break;
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    Q_UNUSED(event);
}
