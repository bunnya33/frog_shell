#include "operationgate.h"
#include "credentials.h"
#if defined(Q_OS_WIN)
#define WIN32_LEAN_AND_MEAN
// clang-format off
#include <windows.h>
#include <wincred.h>
// clang-format on
#elif defined(Q_OS_MACOS)
#include <Security/Security.h>
#elif defined(RABBITSHELL_LIBSECRET)
#pragma push_macro("signals")
#undef signals
#include <libsecret/secret.h>
#pragma pop_macro("signals")
#include <condition_variable>
#include <thread>
#endif

namespace rs {
namespace {
bool validKey(const QString &key) {
    static const QRegularExpression pattern("^io\\.rabbitshell\\.desktop/ssh/[0-9a-f]{64}$");
    return pattern.match(key).hasMatch();
}
#if defined(Q_OS_WIN)
QString nativeError(DWORD code) {
    return QString("Windows 凭据管理器操作失败（%1）。请检查当前用户的凭据存储是否可用。").arg(code);
}
class NativeBackend final : public SecretBackend {
  public:
    SecretResult read(const QString &key) override {
        if (!validKey(key))
            return SecretResult::failure("无效的应用凭据标识");
        PCREDENTIALW entry = nullptr;
        if (!CredReadW(reinterpret_cast<LPCWSTR>(key.utf16()), CRED_TYPE_GENERIC, 0, &entry)) {
            const auto code = GetLastError();
            return code == ERROR_NOT_FOUND ? SecretResult{} : SecretResult::failure(nativeError(code));
        }
        QByteArray value(reinterpret_cast<const char *>(entry->CredentialBlob), entry->CredentialBlobSize);
        CredFree(entry);
        return {SecretResult::Found, value, {}};
    }
    QString write(const QString &key, const QByteArray &value) override {
        if (!validKey(key))
            return "无效的应用凭据标识";
        if (value.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
            return "密码超过系统凭据库的大小限制";
        CREDENTIALW entry{};
        entry.Type = CRED_TYPE_GENERIC;
        entry.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(key.utf16()));
        entry.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(value.constData()));
        entry.CredentialBlobSize = DWORD(value.size());
        entry.Persist = CRED_PERSIST_LOCAL_MACHINE;
        entry.UserName = const_cast<LPWSTR>(L"RabbitShell SSH");
        return CredWriteW(&entry, 0) ? QString() : nativeError(GetLastError());
    }
    QString remove(const QString &key) override {
        if (!validKey(key))
            return "无效的应用凭据标识";
        if (CredDeleteW(reinterpret_cast<LPCWSTR>(key.utf16()), CRED_TYPE_GENERIC, 0))
            return {};
        const auto code = GetLastError();
        return code == ERROR_NOT_FOUND ? QString() : nativeError(code);
    }
};
#elif defined(Q_OS_MACOS)
class NativeBackend final : public SecretBackend {
    static CFMutableDictionaryRef query(const QString &key) {
        auto q = CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                           &kCFTypeDictionaryValueCallBacks);
        auto account =
            CFStringCreateWithCharacters(nullptr, reinterpret_cast<const UniChar *>(key.utf16()), key.size());
        CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(q, kSecAttrService, CFSTR("io.rabbitshell.desktop.ssh"));
        CFDictionarySetValue(q, kSecAttrAccount, account);
        CFRelease(account);
        return q;
    }
    static QString message(OSStatus status) {
        return QString("macOS 钥匙串操作失败（%1）。请检查钥匙串访问权限。").arg(status);
    }

  public:
    SecretResult read(const QString &key) override {
        if (!validKey(key))
            return SecretResult::failure("无效的应用凭据标识");
        auto q = query(key);
        CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
        CFTypeRef result = nullptr;
        const auto status = SecItemCopyMatching(q, &result);
        CFRelease(q);
        if (status == errSecItemNotFound)
            return {};
        if (status != errSecSuccess)
            return SecretResult::failure(message(status));
        auto data = static_cast<CFDataRef>(result);
        QByteArray value(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), CFDataGetLength(data));
        CFRelease(result);
        return {SecretResult::Found, value, {}};
    }
    QString write(const QString &key, const QByteArray &value) override {
        if (!validKey(key))
            return "无效的应用凭据标识";
        auto q = query(key);
        auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(value.constData()), value.size());
        const void *keys[] = {kSecValueData};
        const void *values[] = {data};
        auto attrs = CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                        &kCFTypeDictionaryValueCallBacks);
        auto status = SecItemUpdate(q, attrs);
        if (status == errSecItemNotFound) {
            CFDictionarySetValue(q, kSecValueData, data);
            status = SecItemAdd(q, nullptr);
        }
        CFRelease(attrs);
        CFRelease(data);
        CFRelease(q);
        return status == errSecSuccess ? QString() : message(status);
    }
    QString remove(const QString &key) override {
        if (!validKey(key))
            return "无效的应用凭据标识";
        auto q = query(key);
        const auto status = SecItemDelete(q);
        CFRelease(q);
        return status == errSecSuccess || status == errSecItemNotFound ? QString() : message(status);
    }
};
#elif defined(RABBITSHELL_LIBSECRET)
// Only native Secret Service storage is allowed. Cancellation bounds a locked/unavailable service.
struct Deadline {
    GCancellable *cancel = g_cancellable_new();
    std::mutex mutex;
    std::condition_variable ready;
    bool finished = false;
    std::thread timer{[this] {
        std::unique_lock lock(mutex);
        if (!ready.wait_for(lock, std::chrono::seconds(15), [this] { return finished; }))
            g_cancellable_cancel(cancel);
    }};
    ~Deadline() {
        {
            std::lock_guard lock(mutex);
            finished = true;
        }
        ready.notify_one();
        timer.join();
        g_object_unref(cancel);
    }
};
const SecretSchema schema = {
    "io.rabbitshell.desktop.ssh", SECRET_SCHEMA_NONE, {{"key", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr}}};
QString secretError(GError *error) {
    if (!error)
        return {};
    // Native messages may contain service details; no credential values are included in our diagnostics.
    const QString text = QString("Linux 系统凭据库不可用或已锁定（%1:%2），请解锁密钥环或取消保存。")
                             .arg(error->domain)
                             .arg(error->code);
    g_error_free(error);
    return text;
}
class NativeBackend final : public SecretBackend {
  public:
    SecretResult read(const QString &key) override {
        if (!validKey(key))
            return SecretResult::failure("无效的应用凭据标识");
        Deadline limit;
        GError *error = nullptr;
        auto value = secret_password_lookup_sync(&schema, limit.cancel, &error, "key",
                                                 key.toUtf8().constData(), nullptr);
        if (error)
            return SecretResult::failure(secretError(error));
        if (!value)
            return {};
        QByteArray bytes(value);
        secret_password_free(value);
        // Secret Service uses text passwords; our own entries store base64 to preserve empty/binary values.
        auto decoded = QByteArray::fromBase64Encoding(bytes, QByteArray::AbortOnBase64DecodingErrors);
        return decoded ? SecretResult{SecretResult::Found, decoded.decoded, {}}
                       : SecretResult::failure("已保存的应用凭据格式无效，请重新输入");
    }
    QString write(const QString &key, const QByteArray &value) override {
        if (!validKey(key))
            return "无效的应用凭据标识";
        Deadline limit;
        GError *error = nullptr;
        const bool ok = secret_password_store_sync(&schema, SECRET_COLLECTION_DEFAULT, "RabbitShell SSH",
                                                   value.toBase64().constData(), limit.cancel, &error, "key",
                                                   key.toUtf8().constData(), nullptr);
        auto message = secretError(error);
        return ok ? QString() : message.isEmpty() ? QString("Linux 系统凭据库拒绝保存") : message;
    }
    QString remove(const QString &key) override {
        if (!validKey(key))
            return "无效的应用凭据标识";
        Deadline limit;
        GError *error = nullptr;
        secret_password_clear_sync(&schema, limit.cancel, &error, "key", key.toUtf8().constData(), nullptr);
        return secretError(error);
    }
};
#else
class NativeBackend final : public SecretBackend {
    QString unavailable =
        "当前构建未启用系统安全凭据库。Linux 请安装 libsecret-1 开发包后重新构建；可取消保存进行本次登录。";

  public:
    SecretResult read(const QString &) override {
        return SecretResult::failure(unavailable);
    }
    QString write(const QString &, const QByteArray &) override {
        return unavailable;
    }
    QString remove(const QString &) override {
        return unavailable;
    }
};
#endif

struct Patch {
    QString key;
    SecretResult before;
};
struct Mutation {
    QString error;
    QVector<Patch> applied;
    QString rollback(SecretBackend &backend) {
        QStringList errors;
        for (auto it = applied.crbegin(); it != applied.crend(); ++it) {
            const auto e = it->before.status == SecretResult::Found ? backend.write(it->key, it->before.value)
                                                                    : backend.remove(it->key);
            if (!e.isEmpty())
                errors << e;
        }
        applied.clear();
        return errors.join('\n');
    }
    void apply(SecretBackend &backend, const std::optional<Profile> &old, const Profile &next,
               const std::optional<QByteArray> &secret, bool deleting) {
        const bool same = old && old->credentialKey() == next.credentialKey();
        SecretResult previous;
        if (old && old->rememberSecret) {
            previous = backend.read(old->credentialKey());
            if (previous.status == SecretResult::Error) {
                error = previous.error;
                return;
            }
        }
        auto change = [&](QString key, SecretResult before, std::optional<QByteArray> after) {
            auto e = after ? backend.write(key, *after) : backend.remove(key);
            if (e.isEmpty())
                applied << Patch{key, before};
            else
                error = e;
        };
        if (!deleting && next.rememberSecret && next.authentication != "agent") {
            if (secret) {
                auto before = same ? previous : backend.read(next.credentialKey());
                if (before.status == SecretResult::Error) {
                    error = before.error;
                    return;
                }
                change(next.credentialKey(), before, secret);
            } else if (!same || previous.status != SecretResult::Found) {
                error = "此认证身份没有已保存的密码，请输入密码或取消保存。";
                return;
            }
        }
        if (error.isEmpty() && old && old->rememberSecret &&
            (deleting || !same || !next.rememberSecret || next.authentication == "agent"))
            change(old->credentialKey(), previous, std::nullopt);
        if (!error.isEmpty()) {
            auto e = rollback(backend);
            if (!e.isEmpty())
                error += "\n系统凭据回滚失败，请在系统凭据管理器检查 RabbitShell 条目：" + e;
        }
    }
};
} // namespace

std::shared_ptr<SecretBackend> systemSecretBackend() {
    return std::make_shared<NativeBackend>();
}
SecretService::SecretService(std::shared_ptr<SecretBackend> b, QObject *parent)
    : QObject(parent), backend(std::move(b)) {}
void SecretService::run(std::function<void(SecretBackend &)> work, QObject *owner,
                        std::function<void()> done) {
    tasks.enqueue({std::move(work), owner, std::move(done)});
    next();
}
void SecretService::next() {
    if (running || tasks.isEmpty())
        return;
    running = true;
    auto task = tasks.dequeue();
    auto thread = QThread::create([b = backend, work = std::move(task.work)] { work(*b); });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, task = std::move(task)] {
        if (task.owner && task.done)
            task.done();
        running = false;
        next();
    });
    thread->start();
}
ConnectionRepository::ConnectionRepository(QObject *parent, std::shared_ptr<SecretBackend> backend)
    : QObject(parent), profiles(loadProfiles()), service(std::move(backend), this) {}
Profile *ConnectionRepository::find(const QString &id) {
    for (auto &p : profiles)
        if (p.id == id)
            return &p;
    return nullptr;
}
quint64 ConnectionRepository::revision(const QString &id) const {
    return revisions.value(id);
}
void ConnectionRepository::read(const Profile &p, QObject *owner,
                                std::function<void(SecretResult, quint64)> done) {
    const auto epoch=OperationGate::token();
    if(!OperationGate::accepts(epoch)){done(SecretResult::failure("应用已锁定。"),revision(p.id));return;}
    const auto token = revision(p.id);
    auto result = std::make_shared<SecretResult>();
    service.run([p, result,epoch](SecretBackend &b) { if(OperationGate::accepts(epoch))*result = b.read(p.credentialKey()); }, owner,
                [result, done = std::move(done), token,epoch] { if(!OperationGate::accepts(epoch)){result->value.fill(0);*result=SecretResult::failure("锁定期间读取已取消。");}done(*result, token); });
}
void ConnectionRepository::update(Profile p, std::optional<QByteArray> secret, QObject *owner,
                                  Completion done) {
    p.rememberSecret &= p.authentication != "agent";
    invalidate(p.id);
    const auto token = revision(p.id);
    requests.enqueue({std::move(p), std::move(secret), false, false, token, owner, std::move(done)});
    next();
}
void ConnectionRepository::remove(const QString &id, QObject *owner, Completion done) {
    if (auto p = find(id)) {
        invalidate(id);
        const auto token = revision(id);
        requests.enqueue({*p, {}, true, false, token, owner, std::move(done)});
        next();
    } else if (done)
        done(true, {});
}
void ConnectionRepository::authenticated(const Profile &p, QByteArray secret, bool remember, quint64 token,
                                         QObject *owner, Completion done) {
    Profile candidate = p;
    candidate.rememberSecret = remember && p.authentication != "agent";
    requests.enqueue({candidate, std::move(secret), false, true, token, owner, std::move(done)});
    next();
}
void ConnectionRepository::next() {
    if (busy || requests.isEmpty())
        return;
    auto request = requests.dequeue();
    auto current = find(request.profile.id);
    if (request.revision != revision(request.profile.id) ||
        (request.authenticated &&
         (!current || current->credentialKey() != request.profile.credentialKey()))) {
        if (request.owner && request.done)
            request.done(false, "保存请求已失效，使用最新连接配置。");
        next();
        return;
    }
    if (request.authenticated) {
        const bool remember = request.profile.rememberSecret;
        request.profile = *current;
        request.profile.rememberSecret = remember;
    }
    busy = true;
    std::optional<Profile> old;
    if (current)
        old = *current;
    auto mutation = std::make_shared<Mutation>();
    service.run(
        [mutation, old, request](SecretBackend &b) {
            mutation->apply(b, old, request.profile, request.secret, request.remove);
        },
        this,
        [this, mutation, request] {
            QString error = mutation->error;
            if (error.isEmpty() && request.revision != revision(request.profile.id))
                error = "保存请求已被较新的修改替代。";
            Profile committed = request.profile;
            if (request.authenticated)
                if (auto latest = find(request.profile.id)) {
                    committed = *latest;
                    committed.rememberSecret = request.profile.rememberSecret;
                }
            auto nextProfiles = profiles;
            if (error.isEmpty()) {
                auto it = std::find_if(nextProfiles.begin(), nextProfiles.end(),
                                       [&](const Profile &p) { return p.id == request.profile.id; });
                if (request.remove) {
                    if (it != nextProfiles.end())
                        nextProfiles.erase(it);
                } else if (it != nextProfiles.end())
                    *it = committed;
                else
                    nextProfiles << committed;
                if (!saveProfiles(nextProfiles, &error))
                    error = "配置保存失败：" + error;
            }
            auto finish = [this, request, committed, nextProfiles](const QString &problem) {
                if (problem.isEmpty()) {
                    profiles = nextProfiles;
                    if (request.remove)
                        emit removed(request.profile.id);
                    else
                        emit changed(committed, request.secret);
                }
                if (request.owner && request.done)
                    request.done(problem.isEmpty(), problem);
                busy = false;
                next();
                if (!pending())
                    emit idle();
            };
            if (!error.isEmpty() && !mutation->applied.isEmpty()) {
                auto rollbackError = std::make_shared<QString>();
                service.run(
                    [mutation, rollbackError](SecretBackend &b) { *rollbackError = mutation->rollback(b); },
                    this,
                    [finish, error, rollbackError] {
                        finish(error + (rollbackError->isEmpty() ? QString()
                                                                 : "\n系统凭据回滚失败：" + *rollbackError));
                    });
            } else
                finish(error);
        });
}
} // namespace rs
