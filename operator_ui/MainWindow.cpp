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
#include <QFont>
#include <QGraphicsDropShadowEffect>
#include <QSignalBlocker>

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
    const bool loggingReady = logger.initialize();

    setupUi();

    for (const QString& line : logger.recentEventLines())
        appendLogLine(line);

    setupConnections();
    robot.connectToRobot("10.109.150.232");
    video.connectToCameraBridge("10.109.150.232");

    if (loggingReady) {
        appendLog("Операторское приложение запущено");
        appendLog("Постоянные логи: " + logger.directoryPath());
    } else {
        appendLog("Не удалось открыть постоянные файлы логирования");
    }
}

MainWindow::~MainWindow()
{
    robot.stop();
    robot.disconnectFromRobot();
    video.disconnectFromCameraBridge();
    appendLog("Операторское приложение завершено");
}

void MainWindow::setupUi()
{
    setWindowTitle("Гослингмобиль — Operator Console");
    resize(1280, 800);
    setMinimumSize(1000, 650);

    auto* central = new QWidget;
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* header = new QHBoxLayout;

    auto* driveLogo = new QLabel("Drive");
    driveLogo->setObjectName("driveLogo");
    QFont driveFont("Bahnschrift SemiBold", 32);
    driveFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.5);
    driveLogo->setFont(driveFont);
    auto* driveGlow = new QGraphicsDropShadowEffect(driveLogo);
    driveGlow->setBlurRadius(24);
    driveGlow->setOffset(0, 0);
    driveGlow->setColor(QColor("#00eaff"));
    driveLogo->setGraphicsEffect(driveGlow);
    header->addWidget(driveLogo);

    header->addStretch();

    auto* title = new QLabel("ГОСЛИНГМОБИЛЬ");
    title->setObjectName("title");
    title->setAlignment(Qt::AlignCenter);
    QFont titleFont("Bahnschrift SemiBold", 24);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.5);
    title->setFont(titleFont);
    auto* titleGlow = new QGraphicsDropShadowEffect(title);
    titleGlow->setBlurRadius(20);
    titleGlow->setOffset(0, 0);
    titleGlow->setColor(QColor("#ff36d1"));
    title->setGraphicsEffect(titleGlow);
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
    auto* videoArea = new QVBoxLayout;
    videoArea->setSpacing(8);
    videoArea->addWidget(videoLabel, 1);

    auto* videoToolbar = new QFrame;
    videoToolbar->setObjectName("videoToolbar");
    auto* videoToolbarLayout = new QHBoxLayout(videoToolbar);
    videoToolbarLayout->setContentsMargins(12, 8, 12, 8);

    auto* qualityCaption = new QLabel("КАЧЕСТВО");
    qualityCaption->setObjectName("toolbarCaption");
    videoToolbarLayout->addWidget(qualityCaption);

    qualitySelector = new QComboBox;
    qualitySelector->addItem("ВЫСОКОЕ", "HIGH");
    qualitySelector->addItem("СРЕДНЕЕ", "MEDIUM");
    qualitySelector->addItem("НИЗКОЕ", "LOW");
    qualitySelector->setCurrentIndex(0);
    qualitySelector->setMinimumWidth(140);
    videoToolbarLayout->addWidget(qualitySelector);

    videoToolbarLayout->addSpacing(12);

    recordingStatus = new QLabel("● НЕ ЗАПИСЫВАЕТСЯ");
    recordingStatus->setObjectName("recordOff");
    videoToolbarLayout->addWidget(recordingStatus);

    recordButton = new QPushButton("●  НАЧАТЬ ЗАПИСЬ");
    recordButton->setObjectName("recordButton");
    recordButton->setCheckable(true);
    recordButton->setMinimumHeight(38);
    videoToolbarLayout->addWidget(recordButton);

    videoToolbarLayout->addStretch();
    videoArea->addWidget(videoToolbar);
    content->addLayout(videoArea, 1);

    auto* controlFrame = new QFrame;
    controlFrame->setObjectName("panel");
    auto* controlLayout = new QVBoxLayout(controlFrame);

    auto* controlTitle = new QLabel("УПРАВЛЕНИЕ");
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
    rammingButton->setObjectName("rammingButton");
    rammingButton->setCheckable(true);
    rammingButton->setMinimumHeight(44);
    controlLayout->addWidget(rammingButton);

    auto* rammingHint = new QLabel(
        "Таран отключает УЗ-защиту движения вперёд —\n"
        "робот сможет протаранить препятствие.");
    rammingHint->setWordWrap(true);
    rammingHint->setObjectName("hint");
    controlLayout->addWidget(rammingHint);

    armButton = new QPushButton("ВКЛЮЧИТЬ АВТО-ВОЗВРАТ");
    armButton->setObjectName("armButton");
    armButton->setCheckable(true);
    armButton->setMinimumHeight(44);
    controlLayout->addWidget(armButton);

    auto* armHint = new QLabel(
        "Если связь пропадёт сама по себе, робот и так попробует\n"
        "вернуться назад тем же путём. Эта кнопка - для другого:\n"
        "нажмите её ПЕРЕД заездом в зону без связи, чтобы робот там\n"
        "проехал вперёд, развернулся и сам вернулся обратно.");
    armHint->setWordWrap(true);
    armHint->setObjectName("hint");
    controlLayout->addWidget(armHint);

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
        #title { color:#ff6de2; font-size:25px; font-weight:700; }
        #driveLogo { color:#48efff; }
        #video { background:#050708; border:1px solid #303943; border-radius:8px; }
        #videoToolbar { background:#141b21; border:1px solid #303943; border-radius:8px; }
        #toolbarCaption { color:#8e9aa7; font-size:12px; font-weight:700; }
        QComboBox { background:#202831; border:1px solid #3b4652; border-radius:6px; padding:6px 10px; min-height:24px; }
        QComboBox:hover { border:1px solid #48efff; }
        #recordButton { color:#ff6de2; border:1px solid #8b3d78; font-weight:700; padding:8px 14px; }
        #recordButton:checked { color:#ff8c8c; border:1px solid #ff4f87; background:#38202c; }
        #recordOff { color:#8e9aa7; font-weight:700; }
        #rammingButton { color:#f1c36d; border:1px solid #8a6a2a; font-weight:700; }
        #rammingButton:checked { color:#ff8c8c; border:1px solid #ff4f87; background:#38202c; }
        #armButton { color:#8bd69a; border:1px solid #3d7a4e; font-weight:700; }
        #armButton:checked { color:#48efff; border:1px solid #1c8fa0; background:#123038; }
        #panel, #statusCard { background:#171d23; border:1px solid #303943; border-radius:8px; }
        #panelTitle { font-size:17px; font-weight:700; }
        #cardValue { font-size:17px; font-weight:700; }
        #hint { color:#8e9aa7; padding:8px; }
        QPushButton { background:#202831; border:1px solid #3b4652; border-radius:7px; padding:8px; }
        QPushButton:hover { background:#29333e; }
        QPushButton:pressed { background:#354250; }
        #emergency { background:#5a2525; border:1px solid #9e4444; font-weight:700; }
        #offline { color:#e3a3a3; font-weight:600; }
        #online { color:#8bd69a; font-weight:600; }
        #recordOn { color:#ff6de2; font-weight:700; }
        #recordError { color:#ff8c8c; font-weight:700; }
        #warning { color:#f1c36d; font-weight:700; }
        #safe { color:#8bd69a; font-weight:700; }
    )");

    connect(qualitySelector, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
            video.setQuality(qualitySelector->itemData(index).toString());
    });

    connect(recordButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked)
            video.startRecording();
        else
            video.stopRecording();
    });

    connect(emergency, &QPushButton::clicked, this, [this] { setCommand('S'); });
    for (auto* button : controlFrame->findChildren<QPushButton*>()) {
        if (!button->property("command").isValid())
            continue;
        connect(button, &QPushButton::clicked, this, [this, button] {
            setCommand(button->property("command").toChar());
        });
    }

    connect(rammingButton, &QPushButton::toggled, this, [this](bool checked) {
        robot.setRammingEnabled(checked);
        rammingButton->setText(checked ? "ТАРАН: ВКЛ" : "ТАРАН: ВЫКЛ");
        appendLog(checked ? "Таран включён оператором" : "Таран выключен оператором");
    });

    connect(armButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked)
            robot.armAutoReturn();
        else
            robot.disarmAutoReturn();
        armButton->setText(checked
            ? "АВТО-ВОЗВРАТ ВКЛЮЧЁН"
            : "ВКЛЮЧИТЬ АВТО-ВОЗВРАТ");
        appendLog(checked
            ? "Авто-возврат включён оператором (перед зоной без связи)"
            : "Авто-возврат выключен оператором");
    });
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
    connect(&robot, &RobotConnection::serverEventReceived, this,
        [this](const QString& serverTimestamp, const QString& message) {
            appendServerLog(serverTimestamp, message);
        });
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

    connect(&video, &VideoConnection::qualityChanged, this, [this](const QString& quality) {
        const int index = qualitySelector->findData(quality);
        if (index >= 0 && qualitySelector->currentIndex() != index) {
            QSignalBlocker blocker(qualitySelector);
            qualitySelector->setCurrentIndex(index);
        }
        appendLog("Качество видео: " + quality);
    });

    connect(&video, &VideoConnection::recordingChanged, this, [this](bool recording) {
        QSignalBlocker blocker(recordButton);
        recordButton->setChecked(recording);
        recordButton->setText(recording ? "■  ОСТАНОВИТЬ ЗАПИСЬ" : "●  НАЧАТЬ ЗАПИСЬ");
        recordingStatus->setText(recording ? "● ЗАПИСЬ ИДЁТ" : "● НЕ ЗАПИСЫВАЕТСЯ");
        recordingStatus->setObjectName(recording ? "recordOn" : "recordOff");
        recordingStatus->style()->unpolish(recordingStatus);
        recordingStatus->style()->polish(recordingStatus);
        appendLog(recording ? "Запись видео начата" : "Запись видео остановлена");
    });

    connect(&video, &VideoConnection::controlError, this, [this](const QString& message) {
        QSignalBlocker blocker(recordButton);
        recordButton->setChecked(false);
        recordButton->setText("●  НАЧАТЬ ЗАПИСЬ");
        recordingStatus->setText("● ОШИБКА ЗАПИСИ");
        recordingStatus->setObjectName("recordError");
        recordingStatus->style()->unpolish(recordingStatus);
        recordingStatus->style()->polish(recordingStatus);
        appendLog("Управление видео: " + message);
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

    logger.writeTelemetry(telemetry);

    safetyStatus->setText((telemetry.safeBlocked || telemetry.irBlocked)
        ? "⚠ ПРЕПЯТСТВИЕ" : "● БЕЗОПАСНО");
    safetyStatus->setObjectName((telemetry.safeBlocked || telemetry.irBlocked)
        ? "warning" : "safe");
    safetyStatus->style()->unpolish(safetyStatus);
    safetyStatus->style()->polish(safetyStatus);

    distanceStatus->setText(telemetry.usCenter >= 0
        ? QString("%1 см").arg(telemetry.usCenter) : "—");

    {
        // Синхронизируем кнопки с фактическим состоянием платы, не
        // переотправляя команду (блокируем сигнал на время setChecked).
        QSignalBlocker blocker(rammingButton);
        rammingButton->setChecked(telemetry.rammingEnabled);
        rammingButton->setText(telemetry.rammingEnabled ? "ТАРАН: ВКЛ" : "ТАРАН: ВЫКЛ");
    }

    {
        QSignalBlocker blocker(armButton);
        armButton->setChecked(telemetry.armedForReturn);
        armButton->setText(telemetry.armedForReturn
            ? "АВТО-ВОЗВРАТ ВКЛЮЧЁН"
            : "ВКЛЮЧИТЬ АВТО-ВОЗВРАТ");
    }

    const bool maneuverActive = telemetry.mode != "TELEOP";
    autoReturnStatus->setText(telemetry.mode);
    autoReturnStatus->setObjectName(maneuverActive
        ? "warning"
        : (telemetry.armedForReturn ? "safe" : "offline"));
    autoReturnStatus->style()->unpolish(autoReturnStatus);
    autoReturnStatus->style()->polish(autoReturnStatus);

    if (telemetry.watchdog && !watchdogActive) {
        appendLog("Сработал watchdog: команда остановлена");
        watchdogActive = true;
    } else if (!telemetry.watchdog) {
        watchdogActive = false;
    }
}

void MainWindow::appendLog(const QString& message)
{
    logger.writeLocalEvent(message);
    appendLogLine(
        QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
        + "  LOCAL  " + message
    );
}

void MainWindow::appendServerLog(
    const QString& serverTimestamp,
    const QString& message
)
{
    logger.writeServerEvent(serverTimestamp, message);
    appendLogLine(
        QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
        + "  SERVER " + serverTimestamp + "  " + message
    );
}

void MainWindow::appendLogLine(const QString& line)
{
    if (!logView)
        return;

    logView->append(line);
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
