#pragma once
#include <QtCore>
#include <functional>
namespace rs {
class SessionLockEvents final:public QObject {
    Q_OBJECT
  public:
    explicit SessionLockEvents(std::function<void()> callback);
    ~SessionLockEvents() override;
    QString support()const;
  private slots:
    void screenSaver(bool active);
    void lockSignal();
  private:
    friend class AppLockTests;
    quintptr nativeWindow()const;
    std::function<void()> callback;
    class Impl;
    std::unique_ptr<Impl> impl;
};
}
