#include "OperatorLogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

bool OperatorLogger::initialize()
{
    logDirectory = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation
    );

    if (logDirectory.isEmpty())
        return false;

    logDirectory += "/logs";

    if (!QDir().mkpath(logDirectory))
        return false;

    eventLogPath = logDirectory + "/operator.log";
    telemetryLogPath = logDirectory + "/telemetry.csv";

    QFile telemetryFile(telemetryLogPath);
    const bool needsTelemetryHeader =
        !telemetryFile.exists() || telemetryFile.size() == 0;

    if (!telemetryFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;

    if (needsTelemetryHeader) {
        QTextStream output(&telemetryFile);
        output
            << "local_timestamp,server_timestamp,ir_blocked,"
            << "us_center_cm,us_left_cm,us_right_cm,safe_blocked,"
            << "command,watchdog,raw_telemetry\n";
        output.flush();
    }

    telemetryFile.close();

    QFile eventFile(eventLogPath);
    if (!eventFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;

    eventFile.close();

    ready = true;
    return true;
}

void OperatorLogger::writeLocalEvent(const QString& message)
{
    if (!ready)
        return;

    appendLine(
        eventLogPath,
        QString("%1 | server=— | LOCAL | %2")
            .arg(localTimestamp(), message)
    );
}

void OperatorLogger::writeServerEvent(
    const QString& serverTimestamp,
    const QString& message
)
{
    if (!ready)
        return;

    appendLine(
        eventLogPath,
        QString("%1 | server=%2 | SERVER | %3")
            .arg(localTimestamp(), serverTimestamp, message)
    );
}

void OperatorLogger::writeTelemetry(const Telemetry& telemetry)
{
    if (!ready || !telemetry.valid)
        return;

    appendLine(
        telemetryLogPath,
        QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10")
            .arg(csvEscape(localTimestamp()))
            .arg(csvEscape(telemetry.serverTimestamp))
            .arg(telemetry.irBlocked ? 1 : 0)
            .arg(telemetry.usCenter)
            .arg(telemetry.usLeft)
            .arg(telemetry.usRight)
            .arg(telemetry.safeBlocked ? 1 : 0)
            .arg(csvEscape(QString(telemetry.command)))
            .arg(telemetry.watchdog ? 1 : 0)
            .arg(csvEscape(telemetry.rawLine))
    );
}

QString OperatorLogger::directoryPath() const
{
    return logDirectory;
}

QStringList OperatorLogger::recentEventLines(int maxLines) const
{
    QStringList result;

    if (!ready || maxLines <= 0)
        return result;

    QFile file(eventLogPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    QTextStream input(&file);
    while (!input.atEnd()) {
        const QString line = input.readLine();
        result.append(line);

        if (result.size() > maxLines)
            result.removeFirst();
    }

    return result;
}

bool OperatorLogger::isReady() const
{
    return ready;
}

QString OperatorLogger::localTimestamp() const
{
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
}

void OperatorLogger::appendLine(
    const QString& filePath,
    const QString& line
)
{
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream output(&file);
    output << line << '\n';
    output.flush();
    file.flush();
    file.close();
}

QString OperatorLogger::csvEscape(const QString& value)
{
    QString escaped = value;
    escaped.replace('"', "\"\"");
    return "\"" + escaped + "\"";
}