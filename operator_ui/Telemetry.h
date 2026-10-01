#pragma once

#include <QString>
#include <QStringList>

struct Telemetry
{
    bool valid = false;

    QString serverTimestamp;
    QString rawLine;

    bool irBlocked = false;
    int usCenter = -1;
    int usLeft = -1;
    int usRight = -1;
    bool safeBlocked = false;
    bool rammingEnabled = false;
    QChar command = 'S';
    bool watchdog = false;
    bool armedForReturn = false;
    QString mode = "TELEOP";
};

inline Telemetry parseTelemetry(const QString& line)
{
    Telemetry result;
    result.rawLine = line;

    const QStringList p = line.trimmed().split(',');

    if (p.size() < 20 || p[0] != "T")
        return result;

    if (p.size() >= 2)
        result.serverTimestamp = p[1];

    auto value = [&](const QString& key, int offset) {
        const int i = p.indexOf(key);
        return (i >= 0 && i + offset < p.size())
            ? p[i + offset]
            : QString();
    };

    bool ok = false;

    result.irBlocked = value("IR", 1).toInt(&ok) != 0;

    if (!ok)
        return result;

    result.usCenter = value("US", 1).toInt();
    result.usLeft = value("US", 2).toInt();
    result.usRight = value("US", 3).toInt();

    result.safeBlocked = value("SAFE", 1).toInt() != 0;
    result.rammingEnabled = value("RAM", 1).toInt() != 0;

    const QString command = value("CMD", 1);

    if (!command.isEmpty())
        result.command = command.front();

    result.watchdog = value("WD", 1).toInt() != 0;
    result.armedForReturn = value("ARMED", 1).toInt() != 0;

    const QString mode = value("MODE", 1);

    if (!mode.isEmpty())
        result.mode = mode;

    result.valid = true;
    return result;
}
