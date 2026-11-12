#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
class QLabel;
namespace rs {
class StorageUsageController final : public QObject {
public:
    StorageUsageController(QLabel *dataLabel, QLabel *logLabel, QObject *parent);
    ~StorageUsageController() override;
    void refresh(const QString &dataDirectory, const QString &settingsFile, const QString &logDirectory);
    void leave();
private:
    void start();
    QLabel *dataLabel, *logLabel;
    QString dataDirectory, settingsFile, logDirectory;
    std::shared_ptr<std::atomic_uint64_t> generation;
    bool active = false, running = false, pending = false;
};
}
