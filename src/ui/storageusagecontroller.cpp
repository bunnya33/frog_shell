#include "storageusagecontroller.h"
#include "../core/storageusage.h"
#include <QCoreApplication>
#include <QLabel>
#include <QLocale>
#include <QPointer>
#include <QRunnable>
#include <QThreadPool>
#include <limits>

namespace rs {
namespace {
QString amountText(const UsageAmount &amount) {
    if (amount.partial && !amount.observed) return QStringLiteral("无法统计");
    const auto bytes = qint64(qMin<quint64>(amount.bytes, quint64((std::numeric_limits<qint64>::max)())));
    return (amount.partial ? QStringLiteral("部分结果 ≥ ") : QString()) + QLocale().formattedDataSize(bytes);
}
}
StorageUsageController::StorageUsageController(QLabel *data, QLabel *logs, QObject *parent)
    : QObject(parent), dataLabel(data), logLabel(logs), generation(std::make_shared<std::atomic_uint64_t>(0)) {}
StorageUsageController::~StorageUsageController() { ++*generation; }
void StorageUsageController::refresh(const QString &data, const QString &settings, const QString &logs) {
    active = true; dataDirectory = data; settingsFile = settings; logDirectory = logs;
    ++*generation;
    dataLabel->setText("配置与本地缓存：统计中…"); logLabel->setText("日志占用：统计中…");
    if (running) pending = true; else start();
}
void StorageUsageController::leave() { active = false; pending = false; ++*generation; }
void StorageUsageController::start() {
    if (!active) return;
    running = true; pending = false;
    const auto token = generation->load();
    const auto serial = generation;
    const auto data = dataDirectory, settings = settingsFile, logs = logDirectory;
    QPointer<StorageUsageController> guard(this);
    QThreadPool::globalInstance()->start(QRunnable::create([guard, serial, token, data, settings, logs] {
        const auto result = scanStorageUsage(data, settings, logs, [serial, token] { return serial->load() != token; });
        QMetaObject::invokeMethod(QCoreApplication::instance(), [guard, serial, token, result] {
            if (!guard) return;
            guard->running = false;
            if (guard->active && serial->load() == token && !result.data.canceled && !result.logs.canceled) {
                guard->dataLabel->setText("配置与本地缓存：" + amountText(result.data));
                guard->logLabel->setText("日志占用：" + amountText(result.logs));
            }
            if (guard->active && guard->pending) guard->start();
        }, Qt::QueuedConnection);
    }));
}
}
