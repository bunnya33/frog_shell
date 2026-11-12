#pragma once
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>
class QWidget;
class QMessageBox;
namespace rs {
class CacheCleanupController final : public QObject {
    Q_OBJECT
  public:
    explicit CacheCleanupController(QWidget *page);
    ~CacheCleanupController() override;
    void start(const QString &dataDirectory, const QString &logDirectory);
    void cancel();
    bool isBusy() const { return busy; }
    // Tests can inject tokens without accessing the system clipboard.
    void setTokenProvider(std::function<QSet<QString>()> provider);
    void setLoggingActiveProvider(std::function<bool()> provider);
  signals:
    void busyChanged(bool busy);
    void finished();
  private:
    void end();
    QSet<QString> protectedTokens() const;
    QPointer<QWidget> page;
    QPointer<QMessageBox> prompt;
    std::function<QSet<QString>()> tokenProvider;
    std::function<bool()> loggingActiveProvider;
    std::shared_ptr<std::atomic_bool> cancellation;
    bool busy=false, workerRunning=false;
};
}
