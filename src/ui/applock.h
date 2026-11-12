#pragma once
#include "../core/security.h"
#include "../core/operationgate.h"
#include <QtWidgets>
namespace rs {
class LockSurface;
class SessionLockEvents;
class AppLock final : public QObject {
    Q_OBJECT
  public:
    explicit AppLock(const QString &directory,QObject *parent=nullptr);
    ~AppLock() override;
    static AppLock *instance();
    SecurityStore &security(){return store;}
    bool locked() const{return OperationGate::locked();}
    void lock();
    void unlock(QByteArray password);
    void showStartup();
    void activate();
    void addSettings(QVBoxLayout *layout,QWidget *dialog);
    QString platformSupport()const;
    void simulateSystemLock(); // Shared route with OS notification; tests never lock the workstation.
  signals:
    void lockedChanged(bool locked);
    void unlocked();
  protected:
    bool eventFilter(QObject *,QEvent *) override;
  private:
    friend class AppLockTests;
    friend class LockSurface;
    struct Hidden {QPointer<QWidget> widget;bool hidden,enabled;};
    struct Shield {QPointer<QWidget> window;QPointer<LockSurface> surface;QString title,name,description;bool enabled=true;QList<Hidden> children;};
    SecurityStore store;
    QList<Shield> shields;
    QPointer<QWidget> startup;
    QPointer<QWidget> priorFocus;
    std::unique_ptr<SessionLockEvents> systemEvents;
    bool updating=false,exitAfterUnlock=false;
    quint64 verifyEpoch=0;
    void cover(QWidget *window);
    void concealChildren(Shield &shield);
    void restore();
    void notifySurfaces(const QString &message={});
    bool trusted(QObject *)const;
    void passwordDialog(SecurityStore::Action action,QWidget *parent);
};
}
