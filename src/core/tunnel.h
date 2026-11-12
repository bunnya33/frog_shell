#pragma once

#include "profile.h"
#include <QObject>
#include <QUuid>
#include <QVector>
#include <memory>

namespace rs {

struct TunnelRule {
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString profileId;
    QString localAddress = QStringLiteral("127.0.0.1");
    QString remoteAddress = QStringLiteral("127.0.0.1");
    QString name;
    quint16 localPort = 0;
    quint16 remotePort = 0;
    bool autoStart = false;
};

QVector<TunnelRule> loadTunnelRules(const QString& profileId, QString* error = nullptr);
bool saveTunnelRules(const QString& profileId, const QVector<TunnelRule>& rules,
                     QString* error = nullptr);

// GUI-thread facade. Commands are asynchronous. idle() is emitted only after the
// worker thread has finished and all its SSH, listener and stream resources are gone.
class TunnelService final : public QObject {
    Q_OBJECT
public:
    explicit TunnelService(Profile profile, QByteArray secret, QObject* parent = nullptr);
    ~TunnelService() override;
    void startRule(const TunnelRule& rule);
    void stopRule(const QString& id);
    void stopAll();
    void trust(bool accepted);
    int activeCount() const;

signals:
    void ruleStateChanged(QString id, QString state, QString reason);
    // Payload byte totals are scoped to this rule activation and never persisted.
    void ruleTraffic(QString id, quint64 uploadBytes, quint64 downloadBytes,
                     quint64 uploadPerSecond, quint64 downloadPerSecond);
    void hostKey(QString fingerprint, bool changed);
    void authenticated();
    void passwordRejected();
    void keyAuthenticationNeedsInput();
    void idle();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace rs

Q_DECLARE_METATYPE(rs::TunnelRule)
