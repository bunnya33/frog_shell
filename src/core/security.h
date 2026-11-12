#pragma once
#include <QtCore>
namespace rs {
// Password bytes are owned only by a bounded worker and cleared before it completes.
class SecurityStore final : public QObject {
    Q_OBJECT
  public:
    enum Action { Enable, Verify, Change, Disable };
    explicit SecurityStore(const QString &directory,QObject *parent=nullptr);
    ~SecurityStore() override;
    bool enabled() const { return active; }
    bool damaged() const { return !problem.isEmpty(); }
    QString error() const { return problem; }
    bool busy() const { return working; }
    int retryAfterMs() const;
    void request(Action action,QByteArray current,QByteArray replacement={});
    quint64 requestId()const{return serial;}
    void cancel(quint64 id){if(id==serial)++serial;}
    static void wipe(QByteArray &bytes);
  signals:
    void completed(rs::SecurityStore::Action action,bool ok,const QString &error);
    void changed();
  private:
    friend class AppLockTests;
    QString directory,fileName,problem;
    QByteArray salt,verifier;
    bool active=false,working=false,published=false;
    int failures=0,iterations=600000;
    QDeadlineTimer throttle{0};
    QThread *worker=nullptr;
    quint64 serial=0;
    void load();
    bool write(bool enabled,const QByteArray &salt,const QByteArray &verifier,QString &error);
};
}
Q_DECLARE_METATYPE(rs::SecurityStore::Action)
