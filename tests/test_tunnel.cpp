#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include "../src/core/tunnel.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
#include <future>
#include <thread>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#pragma comment(lib, "psapi.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace {
bool until(const std::function<bool()>& predicate, int timeout = 10000) {
    QElapsedTimer clock; clock.start();
    do {
        if (predicate()) return true;
        QTest::qWait(5);
    } while (clock.elapsed() < timeout);
    return predicate();
}
quint16 freePort() {
    QTcpServer socket;
    if (!socket.listen(QHostAddress::LocalHost, 0)) return 0;
    return socket.serverPort();
}
struct Native {
    qintptr fd = -1;
    explicit Native(quint16 port) {
        fd = qintptr(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (fd == -1) return;
#ifdef SO_NOSIGPIPE
        const int noSigpipe = 1;
        if (setsockopt(int(fd), SOL_SOCKET, SO_NOSIGPIPE, &noSigpipe, sizeof(noSigpipe))) { close(); return; }
#endif
#ifdef Q_OS_WIN
        DWORD timeout = 8000;
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<char*>(&timeout), sizeof(timeout));
#else
        timeval timeout{8, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))) close();
    }
    ~Native() { close(); }
    void close() {
        if (fd == -1) return;
#ifdef Q_OS_WIN
        closesocket(fd);
#else
        ::close(fd);
#endif
        fd = -1;
    }
    bool sendAll(const QByteArray& data) {
        qsizetype offset = 0;
        while (offset < data.size()) {
#ifdef MSG_NOSIGNAL
            constexpr int flags = MSG_NOSIGNAL;
#else
            constexpr int flags = 0;
#endif
            const auto sent = ::send(fd, data.constData() + offset, int(qMin(qsizetype(65536), data.size() - offset)), flags);
            if (sent <= 0) return false;
            offset += sent;
        }
        return true;
    }
    void eof() {
#ifdef Q_OS_WIN
        ::shutdown(fd, SD_SEND);
#else
        ::shutdown(fd, SHUT_WR);
#endif
    }
    QByteArray receive(qsizetype maximum = 8 * 1024 * 1024) {
        QByteArray result; char buffer[16384];
        while (result.size() < maximum) {
            const auto n = ::recv(fd, buffer, int(qMin(qsizetype(sizeof(buffer)), maximum - result.size())), 0);
            if (n <= 0) break;
            result.append(buffer, n);
        }
        return result;
    }
};
QByteArray randomBytes(int size) {
    QByteArray bytes(size, Qt::Uninitialized);
    quint32 value = 0x12345678;
    for (int i = 0; i < size; ++i) { value ^= value << 13; value ^= value >> 17; value ^= value << 5; bytes[i] = char(value); }
    return bytes;
}
std::future<QByteArray> exchange(quint16 port, QByteArray payload) {
    return std::async(std::launch::async, [port, payload] {
        Native client(port);
        if (client.fd == -1) return QByteArray("connect failed");
        std::thread writer([&] { client.sendAll(payload); client.eof(); });
        auto result = client.receive(); writer.join(); return result;
    });
}
bool ready(std::future<QByteArray>& future) { return future.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
struct Resources { quint64 bytes = 0, cpu = 0; int handles = 0, threads = 0; };
Resources resources() {
    Resources r;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
    GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
    r.bytes = memory.PrivateUsage;
    DWORD handles = 0; GetProcessHandleCount(GetCurrentProcess(), &handles); r.handles = int(handles);
    FILETIME creation{}, exit{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user);
    r.cpu = ((quint64(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime) +
            ((quint64(user.dwHighDateTime) << 32) | user.dwLowDateTime);
    auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{}; entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) do {
        if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++r.threads;
    } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
#endif
    return r;
}
struct Harness {
    rs::TunnelService service;
    QHash<QString, QString> states, reasons;
    QSignalSpy idle, auth, rejected, needsInput, keys;
    int streamErrors = 0;
    bool accept = true;
    Harness(const rs::Profile& p, QByteArray secret = "fixture-only")
        : service(p, secret), idle(&service, &rs::TunnelService::idle), auth(&service, &rs::TunnelService::authenticated),
          rejected(&service, &rs::TunnelService::passwordRejected), needsInput(&service, &rs::TunnelService::keyAuthenticationNeedsInput),
          keys(&service, &rs::TunnelService::hostKey) {
        QObject::connect(&service, &rs::TunnelService::hostKey, &service, [this](QString, bool) { service.trust(accept); });
        QObject::connect(&service, &rs::TunnelService::ruleStateChanged, &service, [this](QString id, QString state, QString reason) {
            states[id] = state; reasons[id] = reason;
            if (state == "running" && !reason.isEmpty()) ++streamErrors;
        });
    }
    ~Harness() { service.stopAll(); until([&] { return service.activeCount() == 0; }, 5000); QTest::qWait(20); }
    bool start(const rs::TunnelRule& r) {
        service.startRule(r);
        return until([&] { return states[r.id] == "running" || states[r.id] == "failed"; }) && states[r.id] == "running";
    }
    bool stop() { int count = idle.count(); service.stopAll(); return until([&] { return idle.count() > count; }); }
};
} // namespace

class TunnelTests final : public QObject {
    Q_OBJECT
    std::unique_ptr<QTemporaryDir> temporary;
    rs::Profile profile;
    QJsonObject fixture;
    rs::TunnelRule rule(const char* mode = "echo") {
        rs::TunnelRule r; r.profileId = profile.id; r.localPort = freePort();
        r.remotePort = quint16(fixture.value("targets").toObject().value(mode).toInt()); return r;
    }
    QJsonObject control(const char* command = "stats") {
        QTcpSocket peer;
        peer.connectToHost(QHostAddress::LocalHost, quint16(fixture["control"].toInt()));
        if (!peer.waitForConnected(2000)) return {};
        peer.write(QByteArray(command) + '\n'); peer.waitForBytesWritten(2000);
        QByteArray bytes;
        while (!bytes.endsWith('\n') && peer.waitForReadyRead(2000)) bytes.append(peer.readAll());
        return QJsonDocument::fromJson(bytes).object();
    }
private slots:
    void init() {
        temporary = std::make_unique<QTemporaryDir>(); QVERIFY(temporary->isValid());
        qputenv("RABBITSHELL_DATA_DIR", temporary->path().toUtf8());
        QFile readyFile(qEnvironmentVariable("RABBITSHELL_TUNNEL_FIXTURE"));
        if (readyFile.open(QIODevice::ReadOnly)) fixture = QJsonDocument::fromJson(readyFile.readAll()).object();
        profile = rs::Profile{}; profile.host = "127.0.0.1"; profile.user = "fixture";
        profile.port = fixture.value("port").toInt(); profile.timeout = 5;
    }
    void cleanup() { QTest::qWait(50); temporary.reset(); }
    void configurationRoundTrip() {
        auto a = rule(); a.remotePort = 80; a.name = "测试备注";
        QString error;
        QVERIFY(rs::saveTunnelRules(profile.id, {a}, &error)); QVERIFY(error.isEmpty());
        auto b = a; b.id = QUuid::createUuid().toString(); b.profileId = QUuid::createUuid().toString(); b.localPort++;
        QVERIFY(rs::saveTunnelRules(b.profileId, {b}, &error));
        QCOMPARE(rs::loadTunnelRules(profile.id, &error).size(), 1);
        auto loaded = rs::loadTunnelRules(profile.id, &error).first();
        QCOMPARE(loaded.name, a.name); QCOMPARE(loaded.localPort, a.localPort); QVERIFY(!loaded.autoStart);
        QVERIFY(rs::saveTunnelRules(profile.id, {}, &error));
        QCOMPARE(rs::loadTunnelRules(b.profileId).first().id, b.id);
        QFile file(rs::dataPath() + "/tunnels.json"); QVERIFY(file.open(QIODevice::ReadOnly));
        auto bytes = file.readAll(); QVERIFY(!bytes.contains("password")); QVERIFY(!bytes.contains("fixture-only"));
    }
    void strictConfiguration() {
        auto r = rule(); r.remotePort = 80;
        QVERIFY(rs::saveTunnelRules(profile.id, {r}));
        QFile f(rs::dataPath() + "/tunnels.json"); QVERIFY(f.open(QIODevice::ReadOnly));
        const auto original = f.readAll(); f.close();
        const auto doc = QJsonDocument::fromJson(original).object();
        QVector<QByteArray> invalid = {"[]", "{\"version\":2,\"rules\":[]}", QByteArray(1024 * 1024 + 1, ' ')};
        for (const auto& pair : QVector<QPair<QString, QJsonValue>>{
                 {"localPort", 0}, {"remotePort", 65536}, {"localPort", 1.5}, {"remotePort", "22"},
                 {"autoStart", 1}, {"name", QString(257, 'x')}, {"localAddress", "localhost"},
                 {"remoteAddress", "bad host"}, {"remoteAddress", "x\nsecret"}, {"id", ""}, {"profileId", false}}) {
            auto o = doc["rules"].toArray().first().toObject(); o[pair.first] = pair.second;
            invalid.append(QJsonDocument(QJsonObject{{"version", 1}, {"rules", QJsonArray{o}}}).toJson());
        }
        auto duplicate = doc; auto array = doc["rules"].toArray(); array.append(array.first()); duplicate["rules"] = array;
        invalid.append(QJsonDocument(duplicate).toJson());
        for (const auto& bytes : invalid) {
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate)); QCOMPARE(f.write(bytes), bytes.size()); f.close();
            QString error; QVERIFY(rs::loadTunnelRules(profile.id, &error).isEmpty()); QVERIFY(!error.isEmpty());
            QVERIFY(!rs::saveTunnelRules(profile.id, {r}, &error));
            QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), bytes); f.close();
        }
    }
    void realForwardingAndHalfClose() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        Harness h(profile); auto r = rule("tail");
        QVERIFY2(h.start(r), qPrintable(h.reasons[r.id]));
        QCOMPARE(h.auth.count(), 1); QCOMPARE(h.keys.count(), 1);
        QCOMPARE(h.keys[0][0].toString(), fixture["fingerprint"].toString()); QVERIFY(!h.keys[0][1].toBool());
        auto request = randomBytes(333333); auto client = exchange(r.localPort, request);
        QVERIFY(until([&] { return ready(client); }, 15000));
        QByteArray pattern; for (int i = 0; i < 1024; ++i) for (int c = 0; c < 256; ++c) pattern.append(char(c));
        auto response = client.get();
        QCOMPARE(response.size(), pattern.size() + 70);
        QCOMPARE(response, "TAIL:" + QCryptographicHash::hash(request, QCryptographicHash::Sha256).toHex() + ":" + pattern);
        QCOMPARE(h.states[r.id], "running"); QVERIFY(h.stop());
        QTcpServer rebound; QVERIFY(rebound.listen(QHostAddress::LocalHost, r.localPort));
    }
    void randomDuplexMultipleRules() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        const int before = control()["connections"].toInt();
        Harness h(profile); auto a = rule(), b = rule();
        QSignalSpy traffic(&h.service,&rs::TunnelService::ruleTraffic);
        QVERIFY(h.start(a)); QVERIFY(h.start(b)); QCOMPARE(h.auth.count(), 1);
        auto payload = randomBytes(2 * 1024 * 1024 + 97);
        auto x = exchange(a.localPort, payload), y = exchange(b.localPort, payload);
        QVERIFY(until([&] { return ready(x) && ready(y); }, 30000));
        auto first = x.get(), second = y.get();
        QCOMPARE(first.size(), payload.size()); QCOMPARE(second.size(), payload.size());
        QCOMPARE(first, payload); QCOMPARE(second, payload);
        auto more1=exchange(a.localPort,QByteArray(17000,'a')),more2=exchange(a.localPort,QByteArray(23000,'b'));
        QVERIFY(until([&]{return ready(more1)&&ready(more2);},15000));QCOMPARE(more1.get().size(),17000);QCOMPARE(more2.get().size(),23000);
        auto observed=[&](const QString &id,quint64 bytes,bool idle){for(const auto &row:traffic)if(row[0].toString()==id&&row[1].toULongLong()==bytes&&row[2].toULongLong()==bytes&&(!idle||(row[3].toULongLong()==0&&row[4].toULongLong()==0)))return true;return false;};
        QVERIFY(until([&]{return observed(a.id,quint64(payload.size()+40000),false)&&observed(b.id,quint64(payload.size()),false);},6000));
        QVERIFY(until([&]{return observed(a.id,quint64(payload.size()+40000),true);},5000));
        QCOMPARE(control()["connections"].toInt() - before, 1);
        h.service.stopRule(a.id); QVERIFY(until([&] { return h.states[a.id] == "stopped"; }));
        QCOMPARE(h.idle.count(), 0); QCOMPARE(h.service.activeCount(), 1);
        QTcpServer rebound; QVERIFY(rebound.listen(QHostAddress::LocalHost, a.localPort)); rebound.close();
        traffic.clear();QVERIFY(h.start(a)); auto z = exchange(a.localPort, "restart");
        QVERIFY(until([&] { return ready(z); })); QCOMPARE(z.get(), QByteArray("restart"));
        QVERIFY(until([&]{return observed(a.id,7,false);},5000));
        QVERIFY(h.stop()); QCOMPARE(h.service.activeCount(), 0);
    }
    void reverseHalfClose() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        Harness h(profile); auto r = rule("reverse"); QVERIFY(h.start(r));
        int before = control()["reverse_bytes"].toInt();
        auto result = std::async(std::launch::async, [port = r.localPort] {
            Native peer(port); auto greeting = peer.receive();
            bool sent = peer.sendAll(QByteArray(131071, 'r')); peer.eof();
            return sent ? greeting : QByteArray("send failed after EOF");
        });
        QVERIFY(until([&] { return ready(result); }, 15000)); QCOMPARE(result.get(), QByteArray("REVERSE-EOF"));
        QVERIFY(until([&] { return control()["reverse_bytes"].toInt() == before + 131071; }));
        QVERIFY(h.stop());
    }
    void occupiedPortAndPerStreamDenial() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        Harness h(profile); auto good = rule(), denied = rule("deny"), occupied = rule();
        QVERIFY(h.start(good)); QVERIFY(h.start(denied));
        QTcpServer blocker; QVERIFY(blocker.listen(QHostAddress::LocalHost, occupied.localPort));
        QVERIFY(!h.start(occupied)); QCOMPARE(h.states[occupied.id], "failed"); QVERIFY(h.reasons[occupied.id].contains(QString::number(occupied.localPort)));
        auto failed = exchange(denied.localPort, "denied"), ok = exchange(good.localPort, "survives");
        QVERIFY(until([&] { return ready(failed) && ready(ok); }));
        QVERIFY(failed.get().isEmpty()); QCOMPARE(ok.get(), QByteArray("survives"));
        QCOMPARE(h.states[denied.id], "running"); QVERIFY(h.streamErrors >= 1); QCOMPARE(h.idle.count(), 0);
        auto again = exchange(denied.localPort, "again"); QVERIFY(until([&] { return ready(again); })); QVERIFY(again.get().isEmpty());
        QVERIFY(h.stop());
    }
    void wildcardReuseOccupation() {
        if (fixture.isEmpty()) QSKIP("Use run_tunnel_integration.py");
#ifdef Q_OS_WIN
        auto r = rule();
        const SOCKET blocker = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        QVERIFY(blocker != INVALID_SOCKET);
        struct Close { SOCKET value; ~Close() { closesocket(value); } } cleanup{blocker};
        const int reuse = 1;
        QCOMPARE(setsockopt(blocker, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse)), 0);
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(r.localPort);
        address.sin_addr.s_addr = htonl(INADDR_ANY);
        QCOMPARE(::bind(blocker, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
        QCOMPARE(::listen(blocker, 8), 0);
        Harness h(profile); h.start(r);
        QCOMPARE(h.states[r.id], QString("failed"));
        QVERIFY(h.reasons[r.id].contains(QString::number(r.localPort)));
#else
        QSKIP("Windows socket sharing regression");
#endif
    }
    void independentAddressScopesAndRetry() {
        QVERIFY(!fixture.isEmpty());
        Harness h(profile);auto a=rule(),b=rule(),conflict=rule();
        b.localAddress="127.0.0.2";b.localPort=a.localPort;conflict.localPort=a.localPort;
        QVERIFY2(h.start(a),qPrintable(h.reasons[a.id]));
        QVERIFY2(h.start(b),qPrintable(h.reasons[b.id]));
        QVERIFY(!h.start(conflict));QCOMPARE(h.states[conflict.id],QString("failed"));
        QCOMPARE(h.service.activeCount(),2);
        auto data=exchange(a.localPort,"distinct-addresses");QVERIFY(until([&]{return ready(data);}));QCOMPARE(data.get(),QByteArray("distinct-addresses"));
        QTcpServer busy;auto retry=rule();QVERIFY(busy.listen(QHostAddress::AnyIPv4,retry.localPort));
        QVERIFY(!h.start(retry));QCOMPARE(h.service.activeCount(),2);busy.close();
        h.states.remove(retry.id);QVERIFY2(h.start(retry),qPrintable(h.reasons[retry.id]));
        auto payload=exchange(retry.localPort,"released");QVERIFY(until([&]{return ready(payload);}));QCOMPARE(payload.get(),QByteArray("released"));
        QVERIFY(h.stop());
        QVERIFY(h.start(a));QVERIFY(h.stop());
    }
    void exclusiveOccupationAndIpv6Scope() {
        QVERIFY(!fixture.isEmpty());
        auto r=rule();QTcpServer other;QVERIFY(other.listen(QHostAddress::AnyIPv4,r.localPort));
        {Harness h(profile);QVERIFY(!h.start(r));QCOMPARE(h.service.activeCount(),0);QVERIFY(until([&]{return h.idle.count()==1;}));}
        other.close();
        Harness h(profile);QVERIFY(h.start(r));
#ifdef Q_OS_WIN
        const SOCKET reused=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);QVERIFY(reused!=INVALID_SOCKET);
        const int one=1;QCOMPARE(setsockopt(reused,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&one),sizeof(one)),0);
        sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(r.localPort);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        const int result=::bind(reused,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));closesocket(reused);QCOMPARE(result,SOCKET_ERROR);
#endif
        auto v6=rule();v6.localAddress="::";v6.localPort=r.localPort;
        QVERIFY2(h.start(v6),qPrintable(h.reasons[v6.id]));QCOMPARE(h.service.activeCount(),2);
        QVERIFY(h.stop());
    }
    void dualStackWildcardAndV6Only() {
        QVERIFY(!fixture.isEmpty());
#ifdef Q_OS_WIN
        for(int v6only:{0,1}) {
            auto r=rule();const SOCKET blocker=::socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);QVERIFY(blocker!=INVALID_SOCKET);
            struct Close{SOCKET value;~Close(){closesocket(value);}} cleanup{blocker};
            const int reuse=1;
            QCOMPARE(setsockopt(blocker,SOL_SOCKET,SO_REUSEADDR,reinterpret_cast<const char*>(&reuse),sizeof(reuse)),0);
            QCOMPARE(setsockopt(blocker,IPPROTO_IPV6,IPV6_V6ONLY,reinterpret_cast<const char*>(&v6only),sizeof(v6only)),0);
            sockaddr_in6 address{};address.sin6_family=AF_INET6;address.sin6_port=htons(r.localPort);
            QCOMPARE(::bind(blocker,reinterpret_cast<sockaddr*>(&address),sizeof(address)),0);QCOMPARE(::listen(blocker,8),0);
            Harness h(profile);const bool started=h.start(r);QCOMPARE(started,bool(v6only));
            if(started){auto data=exchange(r.localPort,"v6-only-does-not-cover-v4");QVERIFY(until([&]{return ready(data);}));QCOMPARE(data.get(),QByteArray("v6-only-does-not-cover-v4"));QVERIFY(h.stop());}
            else{QVERIFY(h.reasons[r.id].contains(QString::number(r.localPort)));QVERIFY(until([&]{return h.idle.count()==1;}));}
        }
#else
        QSKIP("Windows dual-stack/reuse regression");
#endif
    }
    void trustCancellationChangedKeyAndPasswordRejection() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        auto r = rule();
        { Harness h(profile); h.accept = false; QVERIFY(!h.start(r)); QVERIFY(until([&] { return h.idle.count() == 1; })); QCOMPARE(h.auth.count(), 0); }
        { Harness h(profile, "wrong-fixture-password"); QVERIFY(!h.start(r)); QCOMPARE(h.rejected.count(), 1); QCOMPARE(h.auth.count(), 0); }
        { Harness h(profile); QVERIFY(h.start(r)); QCOMPARE(h.keys.count(), 0); QVERIFY(h.stop()); }
        control("rotate");
        { Harness h(profile); QVERIFY(!h.start(r)); QCOMPARE(h.auth.count(), 0); QCOMPARE(h.keys.count(), 1); QVERIFY(h.keys[0][1].toBool()); }
        control("rotate");
    }
    void keyAuthentication() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        profile.authentication = "key"; profile.keyPath = fixture["key"].toString();
        auto r = rule();
        { Harness h(profile, {}); QVERIFY2(h.start(r), qPrintable(h.reasons[r.id])); auto f = exchange(r.localPort, "key"); QVERIFY(until([&] { return ready(f); })); QCOMPARE(f.get(), QByteArray("key")); QVERIFY(h.stop()); }
        profile.keyPath = fixture["encrypted"].toString();
        { Harness h(profile, "wrong-passphrase"); QVERIFY(!h.start(r)); QCOMPARE(h.needsInput.count(), 1); QCOMPARE(h.rejected.count(), 0); }
        { Harness h(profile, "fixture-key"); QVERIFY(h.start(r)); QVERIFY(h.stop()); }
    }
    void trustRechecksConcurrentConfirmation() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        auto r = rule();
        rs::TunnelService pending(profile, "fixture-only");
        QSignalSpy keys(&pending, &rs::TunnelService::hostKey), auth(&pending, &rs::TunnelService::authenticated),
            idle(&pending, &rs::TunnelService::idle);
        pending.startRule(r); QVERIFY(until([&] { return keys.count() == 1; }));
        QVERIFY(!keys[0][1].toBool());
        QTcpServer noPrematureListener; QVERIFY(noPrematureListener.listen(QHostAddress::LocalHost, r.localPort));
        noPrematureListener.close();
        control("rotate");
        { Harness other(profile); QVERIFY(other.start(r)); QVERIFY(other.stop()); }
        pending.trust(true);
        QVERIFY(until([&] { return idle.count() == 1; }));
        QCOMPARE(auth.count(), 0); QCOMPARE(keys.count(), 2); QVERIFY(keys[1][1].toBool());
        control("rotate");
    }
    void handshakeDeadlineAndCancellation() {
        QTcpServer silent; QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        profile.port = silent.serverPort(); profile.timeout = 3;
        auto r = rule(); r.remotePort = 80;
        {
            Harness h(profile); QElapsedTimer clock; clock.start();
            QVERIFY(!h.start(r)); QVERIFY(clock.elapsed() >= 2500); QVERIFY(clock.elapsed() < 6000);
            QVERIFY(until([&] { return h.idle.count() == 1; })); QCOMPARE(h.auth.count(), 0);
        }
        {
            rs::TunnelService service(profile, "fixture-only"); QSignalSpy idle(&service, &rs::TunnelService::idle);
            service.startRule(r); QTest::qWait(30); service.stopAll();
            QVERIFY(until([&] { return idle.count() == 1; }, 1000));
        }
        while (silent.hasPendingConnections()) delete silent.nextPendingConnection();
        QTcpServer local; QVERIFY(local.listen(QHostAddress::LocalHost, r.localPort));
    }
    void cancelOneOpeningRulePreservesOther() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        Harness h(profile); auto stable = rule(), delayed = rule("delay");
        QVERIFY(h.start(stable)); QVERIFY(h.start(delayed));
        auto opening = exchange(delayed.localPort, "cancel opening");
        QTest::qWait(100); h.service.stopRule(delayed.id);
        QVERIFY(until([&] { return h.states[delayed.id] == "stopped"; }, 5000));
        QVERIFY(until([&] { return ready(opening); })); opening.get();
        QCOMPARE(h.idle.count(), 0); QCOMPARE(h.states[stable.id], "running");
        auto survivor = exchange(stable.localPort, "unaffected");
        QVERIFY(until([&] { return ready(survivor); })); QCOMPARE(survivor.get(), QByteArray("unaffected"));
        QVERIFY(h.start(delayed)); QVERIFY(h.stop());
    }
    void retirementSettlesBackToBackStopsAndFailureRetry() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        auto a = rule(), b = rule(), c = rule();
        {
            Harness h(profile); QVERIFY(h.start(a)); QVERIFY(h.start(b));
            QHash<QString, int> terminalCounts;
            connect(&h.service, &rs::TunnelService::ruleStateChanged, &h.service,
                    [&](QString id, QString state, QString) {
                if (state == "stopped" || state == "failed") terminalCounts[id] = h.service.activeCount();
            });
            // Deliberately no event-loop yield between these three commands.
            h.service.stopRule(a.id);
            h.service.stopRule(b.id);
            h.service.startRule(c);
            QCOMPARE(h.states[c.id], "failed");
            QVERIFY(until([&] { return h.idle.count() == 1; }));
            QCOMPARE(h.service.activeCount(), 0);
            QCOMPARE(terminalCounts[a.id], 1); QCOMPARE(terminalCounts[b.id], 0);
            QCOMPARE(h.states[c.id], "failed");
        }
        {
            Harness fresh(profile); QVERIFY(fresh.start(c));
            auto flow = exchange(c.localPort, "fresh after retirement");
            QVERIFY(until([&] { return ready(flow); })); QCOMPARE(flow.get(), QByteArray("fresh after retirement"));
            QVERIFY(fresh.stop());
        }
        {
            Harness h(profile); QVERIFY(h.start(a)); QVERIFY(h.start(b));
            bool attempted = false;
            connect(&h.service, &rs::TunnelService::ruleStateChanged, &h.service,
                    [&](QString id, QString state, QString) {
                if (!attempted && id == a.id && state == "failed") {
                    attempted = true;
                    // Retry from a terminal callback before the other rule's
                    // queued terminal state and QThread::finished are handled.
                    h.service.startRule(c);
                }
            });
            control("cut");
            QVERIFY(until([&] { return h.idle.count() == 1; }));
            QVERIFY(attempted); QCOMPARE(h.states[c.id], "failed");
            QCOMPARE(h.service.activeCount(), 0);
        }
    }
    void concurrentStreamsAndGlobalLimit() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        const int baseline = control()["direct"].toInt();
        auto pb = profile, pc = profile;
        pb.id = QUuid::createUuid().toString(); pc.id = QUuid::createUuid().toString();
        Harness a(profile), b(pb), c(pc);
        auto ra = rule(), rb = rule(), rc = rule(); rb.profileId = pb.id; rc.profileId = pc.id;
        QVERIFY(a.start(ra)); QVERIFY(b.start(rb)); QVERIFY(c.start(rc));
        std::vector<std::unique_ptr<QTcpSocket>> clients;
        for (int i = 0; i < 64; ++i) {
            auto peer = std::make_unique<QTcpSocket>(); peer->setReadBufferSize(65536);
            peer->connectToHost(QHostAddress::LocalHost, i < 32 ? ra.localPort : rb.localPort);
            clients.push_back(std::move(peer));
        }
        QVERIFY(until([&] { return control()["direct"].toInt() - baseline == 64; }, 20000));
        QTcpSocket overServer, overGlobal;
        overServer.connectToHost(QHostAddress::LocalHost, ra.localPort);
        overGlobal.connectToHost(QHostAddress::LocalHost, rc.localPort);
        QVERIFY(until([&] { return a.streamErrors && c.streamErrors; }));
        QCOMPARE(control()["direct"].toInt() - baseline, 64);
        for (int i = 0; i < 64; ++i) clients[i]->write(QByteArray::number(i) + ":parallel");
        QVERIFY(until([&] { return std::all_of(clients.begin(), clients.end(), [](const auto& p) { return p->bytesAvailable() > 0; }); }, 15000));
        for (int i = 0; i < 64; ++i) QCOMPARE(clients[i]->readAll(), QByteArray::number(i) + ":parallel");
        QVERIFY(a.stop());
        auto freed = exchange(rc.localPort, "global slot released");
        QVERIFY(until([&] { return ready(freed); })); QCOMPARE(freed.get(), QByteArray("global slot released"));
        QVERIFY(b.stop()); QVERIFY(c.stop()); clients.clear();
    }
    void slowReaderBackpressureAndIdleResources() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        Harness h(profile); auto slow = rule("source"), good = rule();
        QVERIFY(h.start(slow)); QVERIFY(h.start(good));
        auto beforeIdle = resources(); QTest::qWait(600); auto afterIdle = resources();
        qInfo() << "Idle CPU milliseconds" << (afterIdle.cpu - beforeIdle.cpu) / 10000;
#ifdef Q_OS_WIN
        QVERIFY(afterIdle.cpu - beforeIdle.cpu < 1500000); // <150 ms CPU over 600 ms wall time.
#endif
        std::vector<std::unique_ptr<Native>> clients;
        for (int i = 0; i < 8; ++i) {
            auto peer = std::make_unique<Native>(slow.localPort); QVERIFY(peer->fd != -1);
            int receiveCapacity = 8192;
            setsockopt(peer->fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&receiveCapacity), sizeof(receiveCapacity));
            clients.push_back(std::move(peer));
        }
        QTest::qWait(1000); auto first = resources();
        QTest::qWait(1500); auto second = resources();
        qInfo() << "8 paused readers private bytes" << first.bytes << second.bytes << "handles" << second.handles << "threads" << second.threads;
#ifdef Q_OS_WIN
        QVERIFY2(second.bytes <= first.bytes + 8 * 1024 * 1024, "Paused streams continued growing private memory");
#endif
        auto survivor = exchange(good.localPort, randomBytes(128 * 1024));
        QVERIFY(until([&] { return ready(survivor); }, 15000)); QCOMPARE(survivor.get(), randomBytes(128 * 1024));
        // Drain a paused native reader slowly and verify bytes across SSH windows.
        auto draining = std::async(std::launch::async, [&] {
            QByteArray bytes; char buffer[2048];
            while (bytes.size() < 256 * 1024) {
                auto n = ::recv(clients[0]->fd, buffer, sizeof(buffer), 0);
                if (n <= 0) break;
                bytes.append(buffer, n); std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return bytes;
        });
        QVERIFY(until([&] { return ready(draining); }, 15000)); auto received = draining.get();
        QCOMPARE(received.size(), 256 * 1024);
        for (int i = 0; i < received.size(); ++i) QCOMPARE(received[i], char(i % 256));
        h.service.stopRule(slow.id);
        QVERIFY(until([&] { return h.states[slow.id] == "stopped"; }));
        QCOMPARE(h.states[good.id], "running"); QVERIFY(h.stop()); clients.clear();
    }
    void slowSinkAndStopWithPendingWrites() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        Harness h(profile); auto slow = rule("sink"), good = rule(); QVERIFY(h.start(slow)); QVERIFY(h.start(good));
        Native producer(slow.localPort); QVERIFY(producer.fd != -1);
        int sendCapacity = 8192;
        setsockopt(producer.fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&sendCapacity), sizeof(sendCapacity));
        auto sending = std::async(std::launch::async, [&] {
            const bool sent = producer.sendAll(QByteArray(16 * 1024 * 1024, 'w'));
            producer.eof(); return QByteArray(sent ? "sent" : "canceled");
        });
        QTest::qWait(300);
        QVERIFY(!ready(sending));
        auto flow = exchange(good.localPort, "works during slow upload");
        QVERIFY(until([&] { return ready(flow); })); QCOMPARE(flow.get(), QByteArray("works during slow upload"));
        h.service.stopRule(slow.id); QVERIFY(until([&] { return h.states[slow.id] == "stopped"; }));
        QVERIFY(until([&] { return ready(sending); })); QCOMPARE(sending.get(), QByteArray("canceled"));
        QCOMPARE(h.states[good.id], "running"); QVERIFY(h.stop());
    }
    void transportLossAndCancelOpening() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        { Harness h(profile); auto a = rule(), b = rule("cut"); QVERIFY(h.start(a)); QVERIFY(h.start(b));
          auto cut = exchange(b.localPort, "cut"); QVERIFY(until([&] { return h.idle.count() == 1; }));
          QCOMPARE(h.states[a.id], "failed"); QCOMPARE(h.states[b.id], "failed");
          QTcpServer rebound; QVERIFY(rebound.listen(QHostAddress::LocalHost, a.localPort));
          QVERIFY(until([&] { return ready(cut); })); cut.get(); }
        { Harness h(profile); auto delayed = rule("delay"); QVERIFY(h.start(delayed));
          auto client = exchange(delayed.localPort, "opening"); QTest::qWait(50);
          QElapsedTimer clock; clock.start(); QVERIFY(h.stop()); QVERIFY(clock.elapsed() < 1500);
          QVERIFY(until([&] { return ready(client); })); client.get(); }
    }
    void repeatedLifecycleAndImmediateDeletion() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        auto r = rule();
        Harness h(profile);
        QVERIFY(h.start(r)); QVERIFY(h.stop()); // Warm process allocators and Qt's network backend.
        QTest::qWait(100); const auto baseline = resources();
        for (int i = 0; i < 12; ++i) {
            QVERIFY2(h.start(r), qPrintable(h.reasons[r.id]));
            auto f = exchange(r.localPort, QByteArray::number(i)); QVERIFY(until([&] { return ready(f); })); QCOMPARE(f.get(), QByteArray::number(i));
            QVERIFY(h.stop());
        }
        for (int i = 0; i < 12; ++i) {
            auto* service = new rs::TunnelService(profile, "fixture-only");
            service->startRule(r); delete service;
        }
        QVERIFY(until([&] { return control()["active_transports"].toInt() == 0; }));
        QTest::qWait(100);
        const auto final = resources();
        qInfo() << "Lifecycle resources baseline/final bytes" << baseline.bytes << final.bytes
                << "handles" << baseline.handles << final.handles << "threads" << baseline.threads << final.threads;
#ifdef Q_OS_WIN
        QVERIFY(final.threads <= baseline.threads + 1);
        QVERIFY(final.handles <= baseline.handles + 3);
#endif
        QTcpServer rebound; QVERIFY(rebound.listen(QHostAddress::LocalHost, r.localPort));
    }
    void deleteAfterIdleAndActiveDestructor() {
        if (fixture.isEmpty()) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE via run_tunnel_integration.py");
        auto r = rule("source");
        for (int iteration = 0; iteration < 8; ++iteration) {
            auto* service = new rs::TunnelService(profile, "fixture-only");
            QPointer<rs::TunnelService> guarded(service);
            bool running = false;
            connect(service, &rs::TunnelService::hostKey, service, [service](QString, bool) { service->trust(true); });
            connect(service, &rs::TunnelService::ruleStateChanged, service, [&](QString, QString state, QString) { if (state == "running") running = true; });
            service->startRule(r); QVERIFY(until([&] { return running; }));
            Native client(r.localPort); QVERIFY(client.fd != -1); QTest::qWait(20);
            if (iteration % 2) {
                connect(service, &rs::TunnelService::idle, service, &QObject::deleteLater);
                service->stopAll();
            } else {
                QElapsedTimer clock; clock.start(); delete service; QVERIFY(clock.elapsed() < 100);
            }
            QVERIFY(until([&] { return guarded.isNull(); }));
            QVERIFY(until([&] { return control()["active_transports"].toInt() == 0; }));
            QTcpServer rebound; QVERIFY(until([&] { return rebound.listen(QHostAddress::LocalHost, r.localPort); }));
        }
    }
};
QTEST_GUILESS_MAIN(TunnelTests)
#include "test_tunnel.moc"
