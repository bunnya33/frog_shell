#pragma once
#include "services.h"
#include <QtCore>
#include <atomic>
#include <memory>
namespace rs::serverclock {
using IntentPermit = std::shared_ptr<std::atomic_bool>;
struct Sample {
    qint64 epoch = 0;
    QDateTime local;
    int offsetSeconds = 0;
    QString zone, ntp = "unknown";
};
services::Command sampleCommand();
bool parseSample(const QByteArray &bytes, Sample *sample, QString *error = nullptr);
bool validLocalDateTime(const QString &value);
QString offsetText(int seconds);
QVariantMap run(const QString &action, const QVariantMap &options, const services::Runner &runner,
                const std::function<bool()> &canWrite = {});
}
Q_DECLARE_METATYPE(rs::serverclock::IntentPermit)
