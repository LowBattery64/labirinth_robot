#pragma once
#include <QString>
#include <QStringList>

struct Telemetry
{
    bool valid = false;
    bool irBlocked = false;
    int usCenter = -1;
    int usLeft = -1;
    int usRight = -1;
    bool safeBlocked = false;
    QChar command = 'S';
    bool watchdog = false;
};

inline Telemetry parseTelemetry(const QString& line)
{
    Telemetry result;
    const QStringList p = line.trimmed().split(',');

    if (p.size() < 16 || p[0] != "T")
        return result;

    auto value = [&](const QString& key, int offset) {
        const int i = p.indexOf(key);
        return (i >= 0 && i + offset < p.size()) ? p[i + offset] : QString();
    };

    bool ok = false;
    result.irBlocked = value("IR", 1).toInt(&ok) != 0;
    if (!ok)
        return result;

    result.usCenter = value("US", 1).toInt();
    result.usLeft = value("US", 2).toInt();
    result.usRight = value("US", 3).toInt();
    result.safeBlocked = value("SAFE", 1).toInt() != 0;

    const QString command = value("CMD", 1);
    if (!command.isEmpty())
        result.command = command.front();

    result.watchdog = value("WD", 1).toInt() != 0;
    result.valid = true;
    return result;
}
