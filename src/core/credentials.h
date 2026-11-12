#pragma once
#include "profile.h"
#include <functional>
#include <memory>
#include <optional>

namespace rs {
struct SecretResult {
    enum Status { Found, Missing, Error } status = Missing;
    QByteArray value;
    QString error;
    static SecretResult failure(QString message) {
        return {Error, {}, std::move(message)};
    }
};
class SecretBackend {
  public:
    virtual ~SecretBackend() = default;
    virtual SecretResult read(const QString &key) = 0;
    virtual QString write(const QString &key, const QByteArray &value) = 0;
    virtual QString remove(const QString &key) = 0;
};
std::shared_ptr<SecretBackend> systemSecretBackend();

// Serializes native calls away from the GUI. A cancelled owner never receives a callback.
class SecretService : public QObject {
  public:
    explicit SecretService(std::shared_ptr<SecretBackend> backend, QObject *parent = nullptr);
    void run(std::function<void(SecretBackend &)> work, QObject *owner, std::function<void()> done);

  private:
    struct Task {
        std::function<void(SecretBackend &)> work;
        QPointer<QObject> owner;
        std::function<void()> done;
    };
    std::shared_ptr<SecretBackend> backend;
    QQueue<Task> tasks;
    bool running = false;
    void next();
};

// All credential mutations pass through this owner. Revisions revoke delayed authentication writes.
class ConnectionRepository : public QObject {
    Q_OBJECT
  public:
    using Completion = std::function<void(bool, const QString &)>;
    explicit ConnectionRepository(QObject *parent = nullptr,
                                  std::shared_ptr<SecretBackend> backend = systemSecretBackend());
    QVector<Profile> profiles;
    Profile *find(const QString &id);
    quint64 revision(const QString &id) const;
    void invalidate(const QString &id) {
        ++revisions[id];
        emit invalidated(id);
    }
    bool pending() const {
        return busy || !requests.isEmpty();
    }
    void read(const Profile &profile, QObject *owner, std::function<void(SecretResult, quint64)> done);
    void update(Profile profile, std::optional<QByteArray> secret, QObject *owner, Completion done);
    void remove(const QString &id, QObject *owner, Completion done);
    void authenticated(const Profile &profile, QByteArray secret, bool remember, quint64 revision,
                       QObject *owner, Completion done);
  signals:
    // Synchronous revocation before any asynchronous credential mutation.
    void invalidated(const QString &id);
    void changed(const rs::Profile &profile, const std::optional<QByteArray> &secret);
    void removed(const QString &id);
    void idle();

  private:
    struct Request {
        Profile profile;
        std::optional<QByteArray> secret;
        bool remove = false, authenticated = false;
        quint64 revision = 0;
        QPointer<QObject> owner;
        Completion done;
    };
    SecretService service;
    QHash<QString, quint64> revisions;
    QQueue<Request> requests;
    bool busy = false;
    void next();
};
} // namespace rs
