#include "operationgate.h"
#include "tunnel.h"
#include "sshtrust.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QSocketNotifier>
#include <QNetworkInterface>
#include <QThread>
#include <QElapsedTimer>
#include <QTimer>
#include <QtEndian>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <cstddef>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <iphlpapi.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace rs {
namespace {
constexpr int Buffer = 64 * 1024, Chunk = 16 * 1024, WireBuffer = 128 * 1024;
constexpr int MaxRules = 64, MaxStoredRules = 1024, MaxConfig = 1024 * 1024;
constexpr size_t SshBudget = 16 * 1024 * 1024;
std::atomic_int globalStreams{0};
QMutex configMutex;

bool textValid(const QString& s, int maximum, bool empty = false) {
    if ((!empty && s.isEmpty()) || s.size() > maximum) return false;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const auto c = s[i];
        if (c.isNull() || c.category() == QChar::Other_Control) return false;
        if (c.isHighSurrogate()) {
            if (++i == s.size() || !s[i].isLowSurrogate()) return false;
        } else if (c.isLowSurrogate()) return false;
    }
    return true;
}
bool remoteValid(const QString& address) {
    if (!textValid(address, 253)) return false;
    QHostAddress ip;
    if (ip.setAddress(address)) return true;
    const auto ace = QUrl::toAce(address);
    if (ace.isEmpty() || ace.size() > 253) return false;
    auto labels = ace.split('.');
    if (labels.last().isEmpty()) labels.removeLast();
    for (const auto& label : labels) {
        if (label.isEmpty() || label.size() > 63 || label.front() == '-' || label.back() == '-') return false;
        for (char c : label) if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                   (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return !labels.isEmpty();
}
bool validRule(const TunnelRule& r) {
    QHostAddress local;
    return textValid(r.id, 128) && textValid(r.profileId, 128) && textValid(r.name, 256, true) &&
           textValid(r.localAddress, 64) && local.setAddress(r.localAddress) &&
           remoteValid(r.remoteAddress) && r.localPort && r.remotePort;
}
QJsonObject json(const TunnelRule& r) {
    return {{"id", r.id}, {"profileId", r.profileId}, {"localAddress", r.localAddress},
            {"remoteAddress", r.remoteAddress}, {"name", r.name}, {"localPort", r.localPort},
            {"remotePort", r.remotePort}, {"autoStart", r.autoStart}};
}
bool readRules(QVector<TunnelRule>& rules) {
    QFile file(dataPath() + "/tunnels.json");
    if (!file.exists()) return true;
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxConfig) return false;
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(file.read(MaxConfig + 1), &error);
    if (error.error || !doc.isObject()) return false;
    const auto root = doc.object();
    if (root.size() != 2 || !root["version"].isDouble() || root["version"].toDouble() != 1 ||
        !root["rules"].isArray()) return false;
    const auto array = root["rules"].toArray();
    if (array.size() > MaxStoredRules) return false;
    QSet<QString> ids;
    QHash<QString, int> counts;
    for (const auto& value : array) {
        if (!value.isObject()) return false;
        const auto o = value.toObject();
        if (o.size() != 8) return false;
        for (const auto& key : {"id", "profileId", "localAddress", "remoteAddress", "name"})
            if (!o[key].isString()) return false;
        for (const auto& key : {"localPort", "remotePort"}) {
            const auto v = o[key];
            if (!v.isDouble() || v.toDouble() < 1 || v.toDouble() > 65535 ||
                v.toDouble() != v.toInt()) return false;
        }
        if (!o["autoStart"].isBool()) return false;
        TunnelRule r;
        r.id = o["id"].toString(); r.profileId = o["profileId"].toString();
        r.localAddress = o["localAddress"].toString(); r.remoteAddress = o["remoteAddress"].toString();
        r.name = o["name"].toString(); r.localPort = quint16(o["localPort"].toInt());
        r.remotePort = quint16(o["remotePort"].toInt()); r.autoStart = o["autoStart"].toBool();
        if (!validRule(r) || ids.contains(r.id) || ++counts[r.profileId] > MaxRules) return false;
        ids.insert(r.id); rules.append(r);
    }
    return true;
}
void nativeClose(qintptr fd) {
    if (fd == -1) return;
#ifdef Q_OS_WIN
    closesocket(SOCKET(fd));
#else
    ::close(int(fd));
#endif
}
bool wouldBlock() {
#ifdef Q_OS_WIN
    return WSAGetLastError() == WSAEWOULDBLOCK;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}
bool nonblocking(qintptr fd) {
#ifdef Q_OS_WIN
    u_long mode = 1;
    return ioctlsocket(SOCKET(fd), FIONBIO, &mode) == 0;
#else
    int flags = fcntl(int(fd), F_GETFL);
    return flags >= 0 && fcntl(int(fd), F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}
bool suppressSigpipe(qintptr fd) {
#ifdef SO_NOSIGPIPE
    const int enabled = 1;
    return setsockopt(int(fd), SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) == 0;
#else
    Q_UNUSED(fd);
    return true;
#endif
}
void shutdownWrite(qintptr fd) {
#ifdef Q_OS_WIN
    ::shutdown(SOCKET(fd), SD_SEND);
#else
    ::shutdown(int(fd), SHUT_WR);
#endif
}
void wipe(QByteArray& bytes) {
    // All callers own a deep copy: no detach on wiping a shared credential.
    volatile char* p = bytes.data();
    for (qsizetype i = 0; i < bytes.size(); ++i) p[i] = 0;
    bytes.clear();
}
#ifdef Q_OS_WIN
// Winsock permits a later exclusive specific-address bind over an earlier
// wildcard/reuse listener. Check that exceptional overlap while retaining our
// already bound exclusive descriptor. No release/rebind or connection probe.
// AF_INET also exposes the IPv4 shadow of a dual-stack IPv6 wildcard listener;
// IPv6-only listeners have no such row and do not block IPv4 of the same port.
bool existingListenerOverlap(const QHostAddress& address, quint16 port, bool& overlap) {
    overlap = false;
    const bool v6 = address.protocol() == QAbstractSocket::IPv6Protocol;
    const ULONG family = v6 ? AF_INET6 : AF_INET;
    DWORD size = 0;
    auto result = GetExtendedTcpTable(nullptr, &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
    if (result != ERROR_INSUFFICIENT_BUFFER && result != NO_ERROR) return false;
    QByteArray buffer;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (size < sizeof(DWORD) || size > 4 * 1024 * 1024) return false;
        buffer.resize(size);
        result = GetExtendedTcpTable(buffer.data(), &size, FALSE, family, TCP_TABLE_OWNER_PID_LISTENER, 0);
        if (result == ERROR_INSUFFICIENT_BUFFER) continue;
        if (result != NO_ERROR) return false;
        if (!v6) {
            const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.constData());
            if (table->dwNumEntries > (buffer.size() - offsetof(MIB_TCPTABLE_OWNER_PID, table)) / sizeof(MIB_TCPROW_OWNER_PID)) return false;
            const auto target = htonl(address.toIPv4Address());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& row = table->table[i];
                if (ntohs(u_short(row.dwLocalPort)) == port && (!target || !row.dwLocalAddr || target == row.dwLocalAddr)) {
                    overlap = true; break;
                }
            }
        } else {
            const auto* table = reinterpret_cast<const MIB_TCP6TABLE_OWNER_PID*>(buffer.constData());
            if (table->dwNumEntries > (buffer.size() - offsetof(MIB_TCP6TABLE_OWNER_PID, table)) / sizeof(MIB_TCP6ROW_OWNER_PID)) return false;
            const auto target = address.toIPv6Address();const bool any = address == QHostAddress::AnyIPv6;
            bool numeric = false;auto scope = address.scopeId().toUInt(&numeric);
            if(!numeric && !address.scopeId().isEmpty())scope=QNetworkInterface::interfaceIndexFromName(address.scopeId());
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                const auto& row = table->table[i];
                const bool wildcard = std::all_of(std::begin(row.ucLocalAddr),std::end(row.ucLocalAddr),[](UCHAR b){return b==0;});
                const bool same = !memcmp(target.c,row.ucLocalAddr,16) && (!scope || scope==row.dwLocalScopeId);
                if (ntohs(u_short(row.dwLocalPort)) == port && (any || wildcard || same)) { overlap = true; break; }
            }
        }
        return true;
    }
    return false;
}
#endif
class Listener final : public QTcpServer {
public:
    using QTcpServer::QTcpServer;
    bool bindExclusive(const QHostAddress& address, quint16 port, QString& reason) {
        const auto endpoint = (address.protocol() == QAbstractSocket::IPv6Protocol ?
                               "[" + address.toString() + "]" : address.toString()) + ":" + QString::number(port);
#ifdef Q_OS_WIN
        // QTcpServer's default Winsock bind may share a wildcard/reuse listener.
        // Set exclusivity before the only bind; hand this exact listener to Qt.
        const bool ipv6 = address.protocol() == QAbstractSocket::IPv6Protocol;
        const SOCKET fd = WSASocketW(ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                     nullptr, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
        auto fail = [&](int code) {
            if (code == WSAEADDRINUSE || code == WSAEACCES)
                reason = QString("本地监听 %1 无法独占绑定：端口已被占用，或被系统保留/限制（Windows %2）。").arg(endpoint).arg(code);
            else if (code == WSAEADDRNOTAVAIL)
                reason = QString("本地监听 %1 启动失败：此地址不属于本机（Windows %2）。").arg(endpoint).arg(code);
            else reason = QString("本地监听 %1 启动失败（Windows %2）。").arg(endpoint).arg(code);
            if (fd != INVALID_SOCKET) closesocket(fd);
            return false;
        };
        if (fd == INVALID_SOCKET) return fail(WSAGetLastError());
        const int enabled = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&enabled), sizeof(enabled)))
            return fail(WSAGetLastError());
        sockaddr_storage storage{}; int length = 0;
        if (ipv6) {
            // Match Qt's explicit IPv6 scope: do not reserve IPv4 of the same port.
            if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&enabled), sizeof(enabled)))
                return fail(WSAGetLastError());
            auto* addr = reinterpret_cast<sockaddr_in6*>(&storage);
            addr->sin6_family = AF_INET6; addr->sin6_port = htons(port);
            const auto bytes = address.toIPv6Address(); memcpy(&addr->sin6_addr, bytes.c, 16);
            bool numeric = false; auto scope = address.scopeId().toUInt(&numeric);
            if (!numeric && !address.scopeId().isEmpty()) scope = QNetworkInterface::interfaceIndexFromName(address.scopeId());
            addr->sin6_scope_id = scope; length = sizeof(*addr);
        } else {
            auto* addr = reinterpret_cast<sockaddr_in*>(&storage);
            addr->sin_family = AF_INET; addr->sin_port = htons(port); addr->sin_addr.s_addr = htonl(address.toIPv4Address());
            length = sizeof(*addr);
        }
        if (::bind(fd, reinterpret_cast<sockaddr*>(&storage), length)) return fail(WSAGetLastError());
        bool overlap = false;
        if (!existingListenerOverlap(address,port,overlap)) {
            reason = QString("本地监听 %1 未启动：无法核实系统监听地址，未冒险共享端口。").arg(endpoint);
            closesocket(fd);return false;
        }
        if (overlap) return fail(WSAEADDRINUSE);
        if (::listen(fd, listenBacklogSize())) return fail(WSAGetLastError());
        // QTcpServer's socket engine takes ownership of this valid listening descriptor.
        if (!setSocketDescriptor(qintptr(fd))) {
            reason = QString("本地监听 %1 无法交给网络事件循环：%2").arg(endpoint, errorString());
            return false;
        }
        return true;
#else
        if (listen(address, port)) return true;
        reason = serverError() == QAbstractSocket::AddressInUseError ?
                     QString("本地监听 %1 已被占用。").arg(endpoint) :
                     QString("本地监听 %1 启动失败：%2").arg(endpoint, errorString());
        return false;
#endif
    }
    std::function<void(qintptr)> accept;
    void incomingConnection(qintptr descriptor) override { accept(descriptor); }
};
} // namespace

QVector<TunnelRule> loadTunnelRules(const QString& profileId, QString* error) {
    if (error) error->clear();
    QMutexLocker lock(&configMutex);
    QVector<TunnelRule> all, result;
    if (!textValid(profileId, 128) || !readRules(all)) {
        if (error) *error = "端口配置格式无效或无法读取";
        return {};
    }
    for (const auto& r : all) if (r.profileId == profileId) result.append(r);
    return result;
}
bool saveTunnelRules(const QString& profileId, const QVector<TunnelRule>& rules, QString* error) {
    if (error) error->clear();
    auto fail = [&](const char* why) { if (error) *error = QString::fromUtf8(why); return false; };
    if (!textValid(profileId, 128) || rules.size() > MaxRules) return fail("端口配置无效或超过规则上限");
    QMutexLocker lock(&configMutex);
    QLockFile processLock(dataPath() + "/tunnels.json.lock");
    if (!processLock.tryLock(0)) return fail("端口配置正被其他进程使用");
    QVector<TunnelRule> all;
    if (!readRules(all)) return fail("原端口配置无法安全读取，未覆盖文件");
    all.removeIf([&](const TunnelRule& r) { return r.profileId == profileId; });
    QSet<QString> ids;
    for (const auto& r : all) ids.insert(r.id);
    for (const auto& r : rules) {
        if (r.profileId != profileId || !validRule(r) || ids.contains(r.id)) return fail("端口规则无效或重复");
        ids.insert(r.id); all.append(r);
    }
    if (all.size() > MaxStoredRules) return fail("端口规则超过总上限");
    QJsonArray array;
    for (const auto& r : all) array.append(json(r));
    auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"rules", array}}).toJson();
    if (bytes.size() > MaxConfig) return fail("端口配置超过大小上限");
    QSaveFile file(dataPath() + "/tunnels.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return fail("无法保存端口配置");
    return true;
}

class TunnelWorker;
struct TunnelMailbox {
    QMutex mutex;
    TunnelWorker* worker = nullptr;
    bool retiring = false;
};
class TunnelWorker final : public QObject {
    Q_OBJECT
public:
    TunnelWorker(Profile p, QByteArray credential, std::shared_ptr<TunnelMailbox> m)
        : profile(std::move(p)), secret(std::move(credential)), mailbox(std::move(m)) {}
    ~TunnelWorker() override {
        QMutexLocker lock(&mailbox->mutex);
        mailbox->worker = nullptr;
        Q_ASSERT(!ssh && streams.empty()); wipe(secret);
    }
    void add(const TunnelRule& rule);
    void rejectStart(const QString& id) {
        emit state(id,"failed","锁定期间启动请求已取消。");
        if(rules.empty())finish();
    }
    void stop(const QString& id);
    void stopAll();
    void trust(bool accepted);
signals:
    void state(QString id, QString state, QString reason);
    void traffic(QString id, quint64 up, quint64 down, quint64 upRate, quint64 downRate);
    void hostKey(QString fingerprint, bool changed);
    void authenticated();
    void passwordRejected();
    void keyAuthenticationNeedsInput();
    void done();
private:
    const quint64 startEpoch=OperationGate::token();
    enum Phase { Dormant, Connecting, Handshake, Trust, Auth, Ready, Dead } phase = Dormant;
    enum Op { None, Open, Read, Write, Eof, Free };
    struct Rule { TunnelRule value; Listener* listener = nullptr; bool stopping = false;
        quint64 up = 0, down = 0, sampledUp = 0, sampledDown = 0; qint64 sampledAt = 0; };
    struct Stream {
        QString rule;
        qintptr fd = -1;
        QSocketNotifier* read = nullptr;
        QSocketNotifier* write = nullptr;
        LIBSSH2_CHANNEL* channel = nullptr;
        Op op = None;
        QByteArray request, toSsh, toLocal;
        char readBuffer[Chunk];
        size_t readLength = 0;
        QDeadlineTimer deadline;
        bool localEof = false, remoteEof = false, sentEof = false, shut = false, cancel = false;
    };
    Profile profile;
    QByteArray secret, user, keyPath;
    std::shared_ptr<TunnelMailbox> mailbox;
    QTcpSocket* socket = nullptr;
    QTimer* deadlineTimer = nullptr;
    QElapsedTimer trafficClock;
    LIBSSH2_SESSION* ssh = nullptr;
    LIBSSH2_AGENT* agent = nullptr;
    libssh2_agent_publickey* identity = nullptr;
    int agentPhase = 0;
    std::map<QString, Rule> rules;
    std::vector<std::unique_ptr<Stream>> streams;
    Stream* opening = nullptr;
    Stream* pinned = nullptr;
    QDeadlineTimer phaseDeadline;
    bool queued = false, pumping = false;
    int readBudget = 0;
    size_t allocated = 0;
    struct alignas(std::max_align_t) Allocation { size_t bytes; };
    static LIBSSH2_ALLOC_FUNC(allocate) {
        auto* self = static_cast<TunnelWorker*>(*abstract);
        if (count > SshBudget - self->allocated) return nullptr;
        auto* block = static_cast<Allocation*>(std::malloc(sizeof(Allocation) + count));
        if (!block) return nullptr;
        block->bytes = count; self->allocated += count; return block + 1;
    }
    static LIBSSH2_FREE_FUNC(release) {
        if (!ptr) return;
        auto* block = static_cast<Allocation*>(ptr) - 1;
        static_cast<TunnelWorker*>(*abstract)->allocated -= block->bytes;
        std::free(block);
    }
    static LIBSSH2_REALLOC_FUNC(resize) {
        if (!ptr) return allocate(count, abstract);
        auto* self = static_cast<TunnelWorker*>(*abstract);
        auto* block = static_cast<Allocation*>(ptr) - 1;
        const size_t old = block->bytes;
        if (count > SshBudget - self->allocated + old) return nullptr;
        auto* replacement = static_cast<Allocation*>(std::realloc(block, sizeof(Allocation) + count));
        if (!replacement) return nullptr;
        replacement->bytes = count; self->allocated = self->allocated - old + count;
        return replacement + 1;
    }
    static LIBSSH2_SEND_FUNC(sendCallback) {
        Q_UNUSED(socket); Q_UNUSED(flags);
        auto* self = static_cast<TunnelWorker*>(*abstract);
        // On teardown discard a suspended encrypted packet so public libssh2
        // cleanup can complete its exact pending send before freeing channels.
        if (self->phase == Dead) return ssize_t(length);
        if (self->socket->state() != QAbstractSocket::ConnectedState) return -ECONNRESET;
        const qint64 available = WireBuffer - self->socket->bytesToWrite();
        if (available <= 0) return -EAGAIN;
        auto n = self->socket->write(static_cast<const char*>(buffer), qMin(qint64(length), available));
        return n < 0 ? -ECONNRESET : (n ? ssize_t(n) : -EAGAIN);
    }
    static LIBSSH2_RECV_FUNC(recvCallback) {
        Q_UNUSED(socket); Q_UNUSED(flags);
        auto* self = static_cast<TunnelWorker*>(*abstract);
        if (self->phase == Dead) return -ECONNRESET;
        if (self->readBudget <= 0 || !self->socket->bytesAvailable()) return -EAGAIN;
        const auto n = self->socket->read(static_cast<char*>(buffer), qMin(qint64(length), qint64(self->readBudget)));
        if (n < 0) return -ECONNRESET;
        self->readBudget -= int(n);
        return n ? ssize_t(n) : -EAGAIN;
    }
    void begin();
    void schedule();
    void pump();
    bool authenticate();
    bool step(Stream& stream);
    void accept(const QString& id, qintptr fd);
    void localRead(Stream& stream);
    void localWrite(Stream& stream);
    void closeLocal(Stream& stream);
    void cancel(Stream& stream);
    void startListeners();
    void reap();
    void armDeadline();
    void finish(const QString& error = {});
};

void TunnelWorker::add(const TunnelRule& value) {
    if (phase == Dead) {
        emit state(value.id, "failed", "SSH 连接正在释放，请等待停止完成");
        return;
    }
    if (rules.contains(value.id)) return;
    if (!validRule(value) || value.profileId != profile.id || rules.size() >= MaxRules) {
        emit state(value.id, "failed", "端口规则无效或超过上限");
        if (rules.empty()) finish();
        return;
    }
    auto inserted=rules.emplace(value.id, Rule{value});
    inserted.first->second.sampledAt=trafficClock.isValid()?trafficClock.elapsed():0;
    emit state(value.id, phase == Ready ? "starting" : "connecting", {});
    if (phase == Dormant) begin();
    if (phase == Ready) startListeners();
}
void TunnelWorker::begin() {
    if(!OperationGate::accepts(startEpoch)){finish("连接请求已因锁定失效。");return;}
    static const int initialized = libssh2_init(0);
    if (initialized) { finish("SSH 库初始化失败"); return; }
    socket = new QTcpSocket(this);
    trafficClock.start();
    auto trafficTimer = new QTimer(this); trafficTimer->setInterval(1000);
    connect(trafficTimer,&QTimer::timeout,this,[this]{
        const qint64 now=trafficClock.elapsed();
        for(auto &[id,rule]:rules){
            const qint64 elapsed=qMax<qint64>(1,now-rule.sampledAt);
            const auto upRate=quint64((rule.up-rule.sampledUp)*1000.0/elapsed);
            const auto downRate=quint64((rule.down-rule.sampledDown)*1000.0/elapsed);
            rule.sampledAt=now;rule.sampledUp=rule.up;rule.sampledDown=rule.down;
            if(!rule.stopping)emit traffic(id,rule.up,rule.down,upRate,downRate);
        }
    });
    trafficTimer->start();
    socket->setReadBufferSize(WireBuffer);
    deadlineTimer = new QTimer(this); deadlineTimer->setSingleShot(true);
    connect(deadlineTimer, &QTimer::timeout, this, [this] {
        if (phase != Ready && phaseDeadline.hasExpired()) { finish("SSH 连接或认证超时"); return; }
        for (const auto& s : streams) if ((s->op == Open || s->op == Free || s->cancel) && s->deadline.hasExpired()) {
            finish("SSH 通道操作超时"); return;
        }
        armDeadline();
    });
    connect(socket, &QTcpSocket::connected, this, [this] {
        if (phase != Connecting) return;
        socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        socket->setSocketOption(QAbstractSocket::SendBufferSizeSocketOption, Buffer);
        socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, Buffer);
        socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
#ifdef Q_OS_WIN
        tcp_keepalive values{1, 20000, 5000};
        DWORD returned = 0;
        WSAIoctl(SOCKET(socket->socketDescriptor()), SIO_KEEPALIVE_VALS, &values, sizeof(values),
                 nullptr, 0, &returned, nullptr, nullptr);
#else
        const int interval = 5, probes = 3, idle = 20;
#ifdef TCP_KEEPIDLE
        setsockopt(socket->socketDescriptor(), IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#elif defined(TCP_KEEPALIVE)
        setsockopt(socket->socketDescriptor(), IPPROTO_TCP, TCP_KEEPALIVE, &idle, sizeof(idle));
#endif
        setsockopt(socket->socketDescriptor(), IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
        setsockopt(socket->socketDescriptor(), IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof(probes));
#endif
        ssh = libssh2_session_init_ex(allocate, release, resize, this);
        if (!ssh) { finish("无法创建 SSH 会话"); return; }
        libssh2_session_callback_set2(ssh, LIBSSH2_CALLBACK_SEND, reinterpret_cast<libssh2_cb_generic*>(sendCallback));
        libssh2_session_callback_set2(ssh, LIBSSH2_CALLBACK_RECV, reinterpret_cast<libssh2_cb_generic*>(recvCallback));
        libssh2_session_set_blocking(ssh, 0);
        libssh2_session_method_pref(ssh, LIBSSH2_METHOD_KEX,
            "curve25519-sha256,curve25519-sha256@libssh.org,ecdh-sha2-nistp256,"
            "diffie-hellman-group16-sha512,diffie-hellman-group14-sha256");
        phase = Handshake; schedule();
    });
    connect(socket, &QTcpSocket::readyRead, this, &TunnelWorker::schedule);
    connect(socket, &QTcpSocket::bytesWritten, this, &TunnelWorker::schedule);
    connect(socket, &QTcpSocket::disconnected, this, [this] { if (phase != Dead) finish("SSH 连接已断开"); });
    connect(socket, &QTcpSocket::errorOccurred, this, [this] { if (phase != Dead) finish("SSH 网络连接失败"); });
    phase = Connecting;
    phaseDeadline = QDeadlineTimer(qBound(3, profile.timeout, 60) * 1000);
    armDeadline();
    if (!textValid(profile.host, 253) || !textValid(profile.user, 256) ||
        profile.port < 1 || profile.port > 65535 || secret.size() > 64 * 1024 ||
        (profile.authentication != "password" && profile.authentication != "key" && profile.authentication != "agent")) {
        finish("SSH 连接配置无效"); return;
    }
    user = profile.user.toUtf8(); keyPath = QFile::encodeName(profile.keyPath);
    if(!OperationGate::accepts(startEpoch)){finish("应用已锁定，新连接已取消。");return;}
    socket->connectToHost(profile.host, quint16(profile.port));
}
void TunnelWorker::trust(bool accepted) {
    if (phase != Trust) return;
    if (!accepted) { finish("已取消主机指纹确认"); return; }
    auto result = sshtrust::check(ssh, profile, true);
    if (result.status != sshtrust::Result::Trusted) {
        if (result.status == sshtrust::Result::Changed) {
            emit hostKey(result.fingerprint, true);
            finish("主机指纹变化，已停止连接");
        } else finish("主机身份校验失败：" + result.reason);
        return;
    }
    phase = Auth; phaseDeadline = QDeadlineTimer(qBound(3, profile.timeout, 60) * 1000);
    armDeadline(); schedule();
}
bool TunnelWorker::authenticate() {
    int rc = LIBSSH2_ERROR_AUTHENTICATION_FAILED;
    if (profile.authentication == "password")
        rc = libssh2_userauth_password_ex(ssh, user.constData(), unsigned(user.size()), secret.constData(), unsigned(secret.size()), nullptr);
    else if (profile.authentication == "key")
        rc = libssh2_userauth_publickey_fromfile_ex(ssh, user.constData(), unsigned(user.size()), nullptr, keyPath.constData(), secret.constData());
    else {
        // Agent IPC stays on the worker. libssh2's platform agent backend can
        // perform synchronous local IPC; no GUI thread runs any agent operation.
        if (!agent) agent = libssh2_agent_init(ssh);
        if (!agent) { finish("无法连接 SSH Agent"); return false; }
        if (agentPhase == 0) {
            rc = libssh2_agent_connect(agent);
            if (rc == LIBSSH2_ERROR_EAGAIN) return false;
            if (rc) { finish("无法连接 SSH Agent"); return false; }
            agentPhase = 1;
        }
        if (agentPhase == 1) {
            rc = libssh2_agent_list_identities(agent);
            if (rc == LIBSSH2_ERROR_EAGAIN) return false;
            if (rc || libssh2_agent_get_identity(agent, &identity, nullptr)) { finish("SSH Agent 没有可用身份"); return false; }
            agentPhase = 2;
        }
        rc = libssh2_agent_userauth(agent, user.constData(), identity);
        if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN) {
            auto* previous = identity;
            if (!libssh2_agent_get_identity(agent, &identity, previous)) { schedule(); return false; }
        }
    }
    if (rc == LIBSSH2_ERROR_EAGAIN) return false;
    wipe(secret);
    if (rc) {
        if (rc == LIBSSH2_ERROR_AUTHENTICATION_FAILED && profile.authentication == "password") emit passwordRejected();
        if (profile.authentication == "key" && socket->state() == QAbstractSocket::ConnectedState) emit keyAuthenticationNeedsInput();
        finish("SSH 认证失败"); return false;
    }
    if (agent) { libssh2_agent_disconnect(agent); libssh2_agent_free(agent); agent = nullptr; }
    phase = Ready;
    emit authenticated();
    // TCP keepalive avoids libssh2 1.11.1 keepalive_send(), whose public
    // implementation silently swallows EAGAIN and uses a transient packet.
    startListeners(); armDeadline();
    return true;
}
void TunnelWorker::startListeners() {
    QStringList failed;
    for (auto& [id, rule] : rules) {
        if (rule.listener || rule.stopping) continue;
        emit state(id, "starting", {});
        auto* listener = new Listener(this);
        listener->setMaxPendingConnections(32);
        listener->setListenBacklogSize(32);
        listener->accept = [this, id](qintptr fd) { accept(id, fd); };
        QString reason;
        if (!listener->bindExclusive(QHostAddress(rule.value.localAddress), rule.value.localPort, reason)) {
            delete listener; rule.stopping = true;
            failed.append(id);
            emit state(id, "failed", reason);
        } else { rule.listener = listener; emit state(id, "running", {}); }
    }
    for (const auto& id : failed) rules.erase(id);
    if (rules.empty()) finish();
}
void TunnelWorker::accept(const QString& id, qintptr fd) {
    auto rule = rules.find(id);
    if (phase != Ready || rule == rules.end() || rule->second.stopping) { nativeClose(fd); return; }
    int total = globalStreams.load();
    bool reserved = false;
    if (streams.size() < 32) while (total < 64) {
        if (globalStreams.compare_exchange_weak(total, total + 1)) { reserved = true; break; }
    }
    if (!reserved || !nonblocking(fd) || !suppressSigpipe(fd)) {
        if (reserved) --globalStreams;
        nativeClose(fd); emit state(id, "running", "连接流数量已达上限或本地连接不可用"); return;
    }
    auto s = std::make_unique<Stream>(); s->rule = id; s->fd = fd;
    const int kernelBuffer = Buffer;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&kernelBuffer), sizeof(kernelBuffer));
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&kernelBuffer), sizeof(kernelBuffer));
    s->deadline = QDeadlineTimer(10000);
    auto appendInt = [&](quint32 v) { char n[4]; qToBigEndian(v, n); s->request.append(n, 4); };
    auto address = QUrl::toAce(rule->second.value.remoteAddress);
    QHostAddress ip;
    if (ip.setAddress(rule->second.value.remoteAddress)) address = rule->second.value.remoteAddress.toUtf8();
    appendInt(quint32(address.size())); s->request.append(address);
    appendInt(rule->second.value.remotePort);
    sockaddr_storage origin{};
#ifdef Q_OS_WIN
    int originLength = sizeof(origin);
#else
    socklen_t originLength = sizeof(origin);
#endif
    QString originHost = "127.0.0.1"; quint16 originPort = 0;
    if (!getpeername(fd, reinterpret_cast<sockaddr*>(&origin), &originLength)) {
        if (origin.ss_family == AF_INET) {
            auto* a = reinterpret_cast<sockaddr_in*>(&origin);
            originHost = QHostAddress(ntohl(a->sin_addr.s_addr)).toString(); originPort = ntohs(a->sin_port);
        } else if (origin.ss_family == AF_INET6) {
            auto* a = reinterpret_cast<sockaddr_in6*>(&origin);
            originHost = QHostAddress(reinterpret_cast<quint8*>(&a->sin6_addr)).toString(); originPort = ntohs(a->sin6_port);
        }
    }
    auto originBytes = originHost.toUtf8();
    appendInt(quint32(originBytes.size())); s->request.append(originBytes); appendInt(originPort);
    auto* raw = s.get();
    s->read = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    s->write = new QSocketNotifier(fd, QSocketNotifier::Write, this); s->write->setEnabled(false);
    connect(s->read, &QSocketNotifier::activated, this, [this, raw] { localRead(*raw); schedule(); });
    connect(s->write, &QSocketNotifier::activated, this, [this, raw] { localWrite(*raw); schedule(); });
    streams.push_back(std::move(s)); schedule(); armDeadline();
}
void TunnelWorker::localRead(Stream& s) {
    if (s.cancel || s.localEof || s.op == Write) { if (s.read) s.read->setEnabled(false); return; }
    char buffer[Chunk];
    int remaining = Buffer - int(s.toSsh.size());
    if (remaining) {
        auto n = ::recv(s.fd, buffer, qMin(remaining, Chunk), 0);
        if (n > 0) s.toSsh.append(buffer, n);
        else if (!n) s.localEof = true;
        else if (!wouldBlock()) { cancel(s); return; }
    }
    if (s.read) s.read->setEnabled(!s.localEof && s.toSsh.size() < Buffer && !s.cancel);
}
void TunnelWorker::localWrite(Stream& s) {
    if (s.cancel || s.fd == -1) return;
    if (!s.toLocal.isEmpty()) {
#ifdef MSG_NOSIGNAL
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        auto n = ::send(s.fd, s.toLocal.constData(), int(s.toLocal.size()), flags);
        if (n > 0) { if(auto it=rules.find(s.rule);it!=rules.end())it->second.down+=quint64(n); s.toLocal.remove(0, n); }
        else if (n < 0 && !wouldBlock()) { cancel(s); return; }
    }
    if (s.write) s.write->setEnabled(!s.toLocal.isEmpty());
    if (s.remoteEof && s.toLocal.isEmpty() && !s.shut) { shutdownWrite(s.fd); s.shut = true; }
}
void TunnelWorker::closeLocal(Stream& s) {
    delete s.read; s.read = nullptr;
    delete s.write; s.write = nullptr;
    nativeClose(s.fd); s.fd = -1;
}
void TunnelWorker::cancel(Stream& s) {
    if (s.cancel) return;
    s.cancel = true; s.deadline = QDeadlineTimer(5000); closeLocal(s);
    // A pending operation retains its exact buffer and arguments until it completes.
    // In particular an open cannot be abandoned in libssh2's session-level FSM.
    armDeadline();
}
bool TunnelWorker::step(Stream& s) {
    if (s.op == None) {
        if (!s.channel) {
            if (s.cancel || (opening && opening != &s)) return false;
            opening = &s; s.op = Open;
        } else if (s.cancel || (s.sentEof && s.remoteEof && s.toLocal.isEmpty())) {
            s.op = Free; s.deadline = QDeadlineTimer(5000); closeLocal(s); armDeadline();
        } else if (!s.toSsh.isEmpty() && libssh2_channel_window_write_ex(s.channel, nullptr)) {
            s.op = Write;
            if (s.read) s.read->setEnabled(false);
        } else if (s.localEof && s.toSsh.isEmpty() && !s.sentEof) s.op = Eof;
        else if (!s.remoteEof && s.toLocal.size() < Buffer) {
            s.op = Read; s.readLength = size_t(qMin(Chunk, Buffer - int(s.toLocal.size())));
        } else if (socket->bytesAvailable()) {
            // A zero-length public read processes SSH control packets (notably
            // WINDOW_ADJUST after reverse EOF) without draining a paused stream.
            // libssh2 may top up its small window once; it cannot grow it on
            // repeated zero-length reads without application consumption.
            s.op = Read; s.readLength = 0;
        } else return false;
    }
    ssize_t rc = 0;
    const Op operation = s.op;
    switch (operation) {
    case Open:
        s.channel = libssh2_channel_open_ex(ssh, "direct-tcpip", 12, Buffer, Chunk,
                                           s.request.constData(), unsigned(s.request.size()));
        rc = s.channel ? 0 : libssh2_session_last_errno(ssh);
        break;
    case Read: rc = libssh2_channel_read_ex(s.channel, 0, s.readBuffer, s.readLength); break;
    case Write: rc = libssh2_channel_write_ex(s.channel, 0, s.toSsh.constData(), size_t(s.toSsh.size())); break;
    case Eof: rc = libssh2_channel_send_eof(s.channel); break;
    case Free: rc = libssh2_channel_free(s.channel); break;
    default: break;
    }
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        if (libssh2_session_block_directions(ssh) & LIBSSH2_SESSION_BLOCK_OUTBOUND) pinned = &s;
        // Pure inbound reads have no suspended send and no retained user buffer;
        // libssh2 1.11.1 read's only retained state is an outbound window adjust.
        else {
            if (pinned == &s) pinned = nullptr;
            if (operation == Read) s.op = None;
        }
        return false;
    }
    if (pinned == &s) pinned = nullptr;
    s.op = None;
    if (operation == Open) {
        opening = nullptr;
        if (!s.channel) {
            if (!s.cancel && rules.contains(s.rule)) emit state(s.rule, "running", "SSH 服务器拒绝通道或目标连接失败");
            cancel(s); return true;
        }
        // Discard unexpected extended data within this same small channel window.
        libssh2_channel_handle_extended_data2(s.channel, LIBSSH2_CHANNEL_EXTENDED_DATA_MERGE);
        return true;
    }
    if (operation == Free) { s.channel = nullptr; s.cancel = true; return true; }
    if (rc < 0) {
        if (rc == LIBSSH2_ERROR_SOCKET_SEND || rc == LIBSSH2_ERROR_SOCKET_RECV ||
            rc == LIBSSH2_ERROR_SOCKET_DISCONNECT || rc == LIBSSH2_ERROR_ALLOC ||
            rc == LIBSSH2_ERROR_PROTO || rc == LIBSSH2_ERROR_DECRYPT) {
            finish("SSH 传输失败或资源上限已触发"); return false;
        }
        cancel(s); return true;
    }
    if (operation == Write) {
        if(rc>0)if(auto it=rules.find(s.rule);it!=rules.end())it->second.up+=quint64(rc);
        s.toSsh.remove(0, qsizetype(rc));
        if (s.read && !s.localEof && !s.cancel) s.read->setEnabled(s.toSsh.size() < Buffer);
    } else if (operation == Read) {
        if (rc > 0 && !s.cancel) { s.toLocal.append(s.readBuffer, qsizetype(rc)); localWrite(s); }
        if (libssh2_channel_eof(s.channel)) { s.remoteEof = true; localWrite(s); }
    } else if (operation == Eof) s.sentEof = true;
    return rc > 0 || operation == Eof;
}
void TunnelWorker::schedule() {
    if (queued || phase == Dead) return;
    queued = true;
    QMetaObject::invokeMethod(this, [this] { queued = false; pump(); }, Qt::QueuedConnection);
}
void TunnelWorker::pump() {
    if (pumping || phase == Dead || phase == Connecting || phase == Trust) return;
    pumping = true;
    const auto guard = qScopeGuard([&] { pumping = false; });
    readBudget = WireBuffer;
    if (phase == Handshake) {
        const int rc = libssh2_session_handshake(ssh, libssh2_socket_t(socket->socketDescriptor()));
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            if (!readBudget && socket->bytesAvailable()) schedule();
            return;
        }
        if (rc) { finish("SSH 握手失败"); return; }
        const auto result = sshtrust::check(ssh, profile);
        if (result.status == sshtrust::Result::Error) {
            finish("主机身份校验失败：" + result.reason); return;
        }
        if (result.status != sshtrust::Result::Trusted) {
            const bool changed = result.status == sshtrust::Result::Changed;
            phase = Trust; emit hostKey(result.fingerprint, changed);
            if (changed) finish("主机指纹变化，已停止连接");
            else { phaseDeadline = QDeadlineTimer(120000); armDeadline(); }
            return;
        }
        phase = Auth;
    }
    if (phase == Auth && !authenticate()) {
        if (phase != Dead && !readBudget && socket->bytesAvailable()) schedule();
        return;
    }
    if (phase != Ready) return;
    bool progress = false;
    for (int round = 0; round < 8 && phase == Ready; ++round) {
        bool advanced = false;
        if (pinned) {
            advanced = step(*pinned);
            if (pinned) break;
        }
        for (auto& s : streams) {
            advanced |= step(*s);
            if (pinned || phase != Ready) break;
        }
        progress |= advanced;
        reap();
        if (!advanced || pinned) break;
    }
    if (phase == Ready && !pinned &&
        (progress || readBudget < WireBuffer)) schedule();
}
void TunnelWorker::reap() {
    std::erase_if(streams, [&](const auto& s) {
        if (s->cancel && !s->channel && s->op == None) { closeLocal(*s); --globalStreams; return true; }
        return false;
    });
    for (auto it = rules.begin(); it != rules.end();) {
        if (it->second.stopping && std::none_of(streams.begin(), streams.end(), [&](const auto& s) { return s->rule == it->first; })) {
            auto id = it->first; it = rules.erase(it); emit state(id, "stopped", {});
        } else ++it;
    }
    if (rules.empty()) finish();
    else armDeadline();
}
void TunnelWorker::stop(const QString& id) {
    auto it = rules.find(id);
    if (it == rules.end() || it->second.stopping || phase == Dead) return;
    it->second.stopping = true; emit state(id, "stopping", {});
    delete it->second.listener; it->second.listener = nullptr;
    for (auto& s : streams) if (s->rule == id) cancel(*s);
    if (std::all_of(rules.begin(), rules.end(), [](const auto& r) { return r.second.stopping; })) {
        finish(); return;
    }
    reap(); schedule();
}
void TunnelWorker::stopAll() {
    if (phase == Dead) return;
    for (auto& [id, rule] : rules) {
        if (!rule.stopping) emit state(id, "stopping", {});
        rule.stopping = true;
    }
    finish();
}
void TunnelWorker::armDeadline() {
    if (!deadlineTimer || phase == Dead) return;
    qint64 remaining = phase == Ready ? 60000 : qMax(qint64(1), phaseDeadline.remainingTime());
    bool needed = phase != Ready;
    for (const auto& s : streams) if (!s->channel || s->op == Free || s->cancel) {
        remaining = qMin(remaining, qMax(qint64(1), s->deadline.remainingTime())); needed = true;
    }
    if (needed) deadlineTimer->start(int(remaining)); else deadlineTimer->stop();
}
void TunnelWorker::finish(const QString& error) {
    if (phase == Dead) return;
    {
        QMutexLocker lock(&mailbox->mutex);
        mailbox->retiring = true;
    }
    phase = Dead;
    if (deadlineTimer) deadlineTimer->stop();
    for (auto& [id, rule] : rules) { delete rule.listener; rule.listener = nullptr; }
    for (auto& s : streams) { s->cancel = true; closeLocal(*s); }
    if (socket) socket->abort();
    if (agent) { libssh2_agent_disconnect(agent); libssh2_agent_free(agent); agent = nullptr; }
    // Dead callbacks return fatal errors, never EAGAIN. libssh2's public free
    // walks every channel, including any suspended open, without network waits.
    if (ssh) {
        if (pinned) {
            // Finish the exact retained operation against the discard/fatal
            // callbacks; no network IO or waiting occurs during reclamation.
            auto* pending = pinned;
            step(*pending);
            pinned = nullptr;
        }
        libssh2_session_set_blocking(ssh, 0);
        int rc = libssh2_session_free(ssh);
        Q_ASSERT(rc != LIBSSH2_ERROR_EAGAIN);
        Q_UNUSED(rc); ssh = nullptr;
    }
    globalStreams.fetch_sub(int(streams.size())); streams.clear(); opening = pinned = nullptr;
    wipe(secret);
    for (const auto& [id, rule] : rules) emit state(id, error.isEmpty() ? "stopped" : "failed", error);
    rules.clear();
    Q_ASSERT(allocated == 0);
    emit done();
}

struct TunnelService::Private {
    Profile profile;
    QByteArray secret;
    QThread* thread = nullptr;
    std::shared_ptr<TunnelMailbox> mailbox;
    QSet<QString> active, stopping;
    bool closing = false;
    bool post(std::function<void(TunnelWorker*)> command) {
        if (!mailbox) return false;
        QMutexLocker lock(&mailbox->mutex);
        if (!mailbox->retiring) if (auto* worker = mailbox->worker)
            return QMetaObject::invokeMethod(worker, [worker, command = std::move(command)] { command(worker); }, Qt::QueuedConnection);
        return false;
    }
    bool retiring() const {
        if (closing) return true;
        if (!mailbox) return false;
        QMutexLocker lock(&mailbox->mutex);
        return mailbox->retiring || !mailbox->worker;
    }
    ~Private() { wipe(secret); }
};
TunnelService::TunnelService(Profile profile, QByteArray secret, QObject* parent) : QObject(parent), d(std::make_unique<Private>()) {
    d->profile = std::move(profile);
    // Deep copy once; the caller's implicit-sharing credential is never modified.
    d->secret = QByteArray(secret.constData(), secret.size());
}
TunnelService::~TunnelService() {
    if (d->mailbox) {
        QMutexLocker lock(&d->mailbox->mutex);
        if (auto* worker = d->mailbox->worker) {
            disconnect(worker, nullptr, this, nullptr);
            QMetaObject::invokeMethod(worker, &TunnelWorker::stopAll, Qt::QueuedConnection);
        }
    }
    // The unparented QThread owns its asynchronous cleanup connections.
    // No wait(), blocking queued connection or GUI-thread socket operation.
}
void TunnelService::startRule(const TunnelRule& rule) {
    if(OperationGate::locked()){emit ruleStateChanged(rule.id,"failed","应用已锁定。");return;}
    if (d->active.contains(rule.id)) return;
    if (d->retiring()) { emit ruleStateChanged(rule.id, "failed", "正在释放 SSH 连接，请等待停止完成"); return; }
    if (!validRule(rule) || rule.profileId != d->profile.id) {
        emit ruleStateChanged(rule.id, "failed", "端口规则无效"); return;
    }
    if (!d->thread) {
        auto* thread = new QThread;
        d->mailbox = std::make_shared<TunnelMailbox>();
        auto* worker = new TunnelWorker(d->profile, QByteArray(d->secret.constData(), d->secret.size()), d->mailbox);
        d->mailbox->worker = worker;
        d->thread = thread;
        worker->moveToThread(thread);
        connect(worker, &TunnelWorker::state, this, [this](QString id, QString state, QString reason) {
            if (state == "stopped" || state == "failed") {
                d->active.remove(id);
                d->stopping.remove(id);
            }
            if (d->active.isEmpty() || d->active == d->stopping) d->closing = true;
            emit ruleStateChanged(id, state, reason);
        });
        connect(worker,&TunnelWorker::traffic,this,[this](QString id,quint64 up,quint64 down,quint64 upRate,quint64 downRate){
            if(d->active.contains(id))emit ruleTraffic(id,up,down,upRate,downRate);
        });
        connect(worker, &TunnelWorker::hostKey, this, &TunnelService::hostKey);
        connect(worker, &TunnelWorker::authenticated, this, &TunnelService::authenticated);
        connect(worker, &TunnelWorker::passwordRejected, this, &TunnelService::passwordRejected);
        connect(worker, &TunnelWorker::keyAuthenticationNeedsInput, this, &TunnelService::keyAuthenticationNeedsInput);
        // Delete the worker in its live event loop, then quit. finished/idle
        // therefore cannot overtake worker/socket destruction.
        connect(worker, &TunnelWorker::done, worker, &QObject::deleteLater);
        connect(worker, &QObject::destroyed, thread, &QThread::quit, Qt::DirectConnection);
        connect(thread, &QThread::finished, this, [this] {
            // Accepted commands can be overtaken by a spontaneous worker
            // failure. Every outstanding id receives a terminal state, with
            // counts settled first and retirement retained during callbacks.
            d->closing = true;
            const auto unsettled = d->active;
            d->mailbox.reset(); d->thread = nullptr;
            d->active.clear(); d->stopping.clear();
            QPointer<TunnelService> guard(this);
            for (const auto& id : unsettled) {
                emit ruleStateChanged(id, "failed", "SSH 连接已结束，端口规则未能启动");
                if (!guard) return;
            }
            d->closing = false;
            emit idle();
        });
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
    }
    d->active.insert(rule.id);
    const auto operation=OperationGate::token();
    if (!d->post([rule,operation](TunnelWorker* worker) { if(OperationGate::accepts(operation))worker->add(rule);else worker->rejectStart(rule.id); })) {
        d->active.remove(rule.id);
        d->closing = true;
        emit ruleStateChanged(rule.id, "failed", "SSH 连接正在释放，请等待停止完成");
    }
}
void TunnelService::stopRule(const QString& id) {
    if (!d->thread || !d->active.contains(id)) return;
    d->stopping.insert(id);
    if (d->stopping == d->active) d->closing = true;
    d->post([id](TunnelWorker* worker) { worker->stop(id); });
}
void TunnelService::stopAll() {
    if (!d->thread) return;
    d->closing = true;
    d->stopping = d->active;
    d->post([](TunnelWorker* worker) { worker->stopAll(); });
}
void TunnelService::trust(bool accepted) {
    d->post([accepted](TunnelWorker* worker) { worker->trust(accepted); });
}
int TunnelService::activeCount() const { return int(d->active.size()); }

} // namespace rs
#include "tunnel.moc"
