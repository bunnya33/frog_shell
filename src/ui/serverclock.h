#pragma once
#include "../core/serverclock.h"
#include <QtWidgets>
namespace rs {
class Session;
class ServerClock : public QPushButton {
    Q_OBJECT
public:
    explicit ServerClock(QWidget *parent=nullptr);
    ~ServerClock() override;
    void setSession(Session *session);
    void setConnected(bool connected);
    void setActive(bool active);
private:
    friend class ServerClockTests;
    QPointer<Session> session;
    QList<QMetaObject::Connection> bindings;
    bool connected=false,active=true,valid=false,busy=false,dialogInitialized=false;
    bool refreshAfterBusy=false;
    int pendingId=0;
    quint64 generation=0,pendingGeneration=0;
    QString pendingAction,feedbackText,operationFeedback;
    serverclock::IntentPermit writePermit;
    serverclock::Sample sample;
    QElapsedTimer elapsed,requestAge;
    qint64 wallAtSample=0;
    QTimer timer;
    QPointer<QDialog> dialog;
    QPointer<QLabel> currentLabel,zoneLabel,ntpLabel,statusLabel;
    QPointer<QDateEdit> dateEdit;
    QPointer<QTimeEdit> timeEdit;
    QPointer<QPushButton> applyButton,refreshButton;
    void tick();
    void refresh(bool force=false);
    void submit(const QString &action,QVariantMap options={});
    void completed(Session *origin,const QString &type,int id,const QVariantMap &result);
    void failed(Session *origin,const QString &type,int id,const QString &reason);
    void showPanel();
    void apply();
    void updateView();
    void closePanel();
    void cancelIntent();
    QDateTime displayedTime() const;
};
}
