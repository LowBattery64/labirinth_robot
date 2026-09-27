#pragma once

#include <QString>
#include <QStringList>

#include "Telemetry.h"

class OperatorLogger
{
public:
    bool initialize();

    void writeLocalEvent(const QString& message);
    void writeServerEvent(const QString& serverTimestamp, const QString& message);
    void writeTelemetry(const Telemetry& telemetry);

    QString directoryPath() const;
    QStringList recentEventLines(int maxLines = 200) const;
    bool isReady() const;

private:
    QString logDirectory;
    QString eventLogPath;
    QString telemetryLogPath;
    bool ready = false;

    QString localTimestamp() const;
    void appendLine(const QString& filePath, const QString& line);
    static QString csvEscape(const QString& value);
};