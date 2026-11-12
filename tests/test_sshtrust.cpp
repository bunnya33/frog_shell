#include "../src/core/session.h"
#include "../src/core/tunnel.h"
#include <QTcpServer>
#include <QtTest>
#include <functional>

namespace {
bool until(const std::function<bool()>& predicate, int timeout = 10000) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed() < timeout) QTest::qWait(5);
    return predicate();
}
struct Outcome {
    bool finished = false;
    int authenticated = 0;
    QList<bool> prompts;
    QString reason;
};
}

class SshTrustTests : public QObject {
    Q_OBJECT
    QJsonObject fixture;
    std::unique_ptr<QTemporaryDir> directory;
    rs::Profile profile() const {
        rs::Profile p; p.host = "127.0.0.1"; p.port = fixture.value("port").toInt();
        p.user = "fixture"; p.timeout = 5; return p;
    }
    QString path() const { return directory->path() + "/known_hosts"; }
    QByteArray record(bool alternate = false, QByteArray host = {}) const {
        if (host.isEmpty()) host = "[127.0.0.1]:" + QByteArray::number(profile().port);
        return host + ' ' + fixture.value("hostkey_type").toString().toLatin1() + ' ' +
               fixture.value(alternate ? "alternate_hostkey" : "hostkey").toString().toLatin1();
    }
    bool write(const QByteArray& bytes) const {
        QFile f(path()); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
    }
    QByteArray read() const {
        QFile f(path()); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
    }
    Outcome exercise(bool tunnel, std::function<void(bool)> prompt = {}) {
        Outcome result;
        const auto p = profile();
        if (!tunnel) {
            rs::Session session(p, "fixture-only");
            connect(&session, &rs::Session::authenticated, this, [&] { ++result.authenticated; });
            connect(&session, &rs::Session::hostKey, this, [&](QString, bool changed) {
                result.prompts.append(changed); if (prompt) prompt(changed); session.trust(!changed);
            });
            connect(&session, &rs::Session::disconnected, this, [&](QString reason) {
                result.reason = reason; result.finished = true;
            });
            session.start(); until([&] { return result.finished; });
            session.stop(); session.wait();
        } else {
            QTcpServer reservation;
            if (!reservation.listen(QHostAddress::LocalHost, 0)) return result;
            rs::TunnelRule rule; rule.profileId = p.id; rule.localPort = reservation.serverPort();
            rule.remotePort = quint16(fixture.value("targets").toObject().value("echo").toInt());
            reservation.close();
            rs::TunnelService service(p, "fixture-only");
            bool terminal = false, idle = false;
            connect(&service, &rs::TunnelService::authenticated, this, [&] { ++result.authenticated; });
            connect(&service, &rs::TunnelService::hostKey, this, [&](QString, bool changed) {
                result.prompts.append(changed); if (prompt) prompt(changed); service.trust(!changed);
            });
            connect(&service, &rs::TunnelService::ruleStateChanged, this,
                    [&](QString, QString state, QString reason) {
                if (state == "running" || state == "failed") { terminal = true; result.reason = reason; }
            });
            connect(&service, &rs::TunnelService::idle, this, [&] { idle = true; });
            service.startRule(rule); until([&] { return terminal; });
            service.stopAll(); result.finished = until([&] { return idle; }) && terminal;
        }
        return result;
    }
    static void transports() { QTest::addColumn<bool>("tunnel"); }
  private slots:
    void initTestCase() {
        QFile f(qEnvironmentVariable("RABBITSHELL_TUNNEL_FIXTURE"));
        if (!f.open(QIODevice::ReadOnly)) QSKIP("Set RABBITSHELL_TUNNEL_FIXTURE using run_sshtrust_integration.py");
        fixture = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(fixture.value("port").toInt() > 0);
        QVERIFY(!fixture.value("hostkey").toString().isEmpty());
    }
    void init() {
        directory = std::make_unique<QTemporaryDir>(); QVERIFY(directory->isValid());
        qputenv("RABBITSHELL_DATA_DIR", directory->path().toUtf8());
    }
    void trustedRecords_data() {
        transports(); QTest::addColumn<QByteArray>("ending"); QTest::addColumn<bool>("mixed");
        for (bool tunnel : {false, true}) {
            const QByteArray prefix = tunnel ? "tunnel-" : "session-";
            QTest::newRow(prefix + "LF") << tunnel << QByteArray("\n") << false;
            QTest::newRow(prefix + "CRLF-no-comment") << tunnel << QByteArray("\r\n") << false;
            QTest::newRow(prefix + "no-final-newline") << tunnel << QByteArray() << false;
            QTest::newRow(prefix + "mixed-blank-comments") << tunnel << QByteArray("\r\n") << true;
        }
    }
    void trustedRecords() {
        QFETCH(bool, tunnel); QFETCH(QByteArray, ending); QFETCH(bool, mixed);
        auto bytes = record() + ending;
        if (mixed) bytes = "\r\n \t\n # fixture comment\r\n" + record(false, "other.test") + "\n" + bytes + "\t \r\n";
        QVERIFY(write(bytes)); const auto result = exercise(tunnel);
        QVERIFY(result.finished); QCOMPARE(result.authenticated, 1); QVERIFY(result.prompts.isEmpty());
        if (!tunnel) QVERIFY(result.reason.contains("SFTP"));
        QCOMPARE(read(), bytes); // Reading a trusted file never rewrites its format.
    }
    void changedRecords_data() {
        transports(); QTest::addColumn<QByteArray>("ending");
        for (bool tunnel : {false, true}) for (const auto& ending : {QByteArray("\n"), QByteArray("\r\n"), QByteArray()})
            QTest::newRow(qPrintable(QString("%1-%2").arg(tunnel ? "tunnel" : "session").arg(ending.toHex()))) << tunnel << ending;
    }
    void changedRecords() {
        QFETCH(bool, tunnel); QFETCH(QByteArray, ending);
        const auto bytes = record(true) + ending; QVERIFY(write(bytes)); const auto result = exercise(tunnel);
        QVERIFY(result.finished); QCOMPARE(result.authenticated, 0); QCOMPARE(result.prompts, QList<bool>{true});
        QVERIFY(result.reason.contains("变化")); QCOMPARE(read(), bytes);
    }
    void invalidRecords_data() {
        transports(); QTest::addColumn<int>("kind");
        for (bool tunnel : {false, true}) for (int kind = 0; kind < 7; ++kind)
            QTest::newRow(qPrintable(QString("%1-invalid-%2").arg(tunnel ? "tunnel" : "session").arg(kind))) << tunnel << kind;
    }
    void invalidRecords() {
        QFETCH(bool, tunnel); QFETCH(int, kind);
        QByteArray bytes;
        switch (kind) {
        case 0: bytes = "broken-record\n"; break;
        case 1: bytes = record(); bytes.insert(bytes.size() - 8, '\r'); bytes += "\r\n"; break;
        case 2: bytes = record() + '\r'; break; // A bare CR is not a CRLF terminator.
        case 3: bytes = record(); bytes.insert(bytes.size() - 8, '\0'); bytes += '\n'; break;
        case 4: bytes = QByteArray(16385, 'x') + '\n'; break;
        case 5: bytes = QByteArray(4 * 1024 * 1024 + 1, 'x'); break;
        case 6: bytes = record() + "\ninvalid-tail\n"; break;
        }
        QVERIFY(write(bytes)); const auto result = exercise(tunnel);
        QVERIFY(result.finished); QCOMPARE(result.authenticated, 0); QVERIFY(result.prompts.isEmpty());
        QVERIFY2(result.reason.contains("校验失败"), qPrintable(result.reason));
        QVERIFY(!result.reason.contains("变化")); QVERIFY(result.reason.contains("known_hosts"));
        QCOMPARE(read(), bytes);
    }
    void trustAndPreserve_data() {
        transports(); QTest::newRow("session") << false; QTest::newRow("tunnel") << true;
    }
    void trustAndPreserve() {
        QFETCH(bool, tunnel);
        QVERIFY(write(record(true, "other.test") + "\r\n"));
        auto first = exercise(tunnel); QVERIFY(first.finished); QCOMPARE(first.authenticated, 1);
        QCOMPARE(first.prompts, QList<bool>{false});
        const auto saved = read(); QVERIFY(saved.contains(record(true, "other.test")));
        QVERIFY(saved.contains(record())); QVERIFY(!saved.contains('\r'));
        auto second = exercise(!tunnel); QVERIFY(second.finished); QCOMPARE(second.authenticated, 1);
        QVERIFY(second.prompts.isEmpty()); QCOMPARE(read(), saved);
    }
    void confirmationRace_data() {
        transports(); QTest::addColumn<bool>("corrupt");
        for (bool tunnel : {false, true}) for (bool corrupt : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2").arg(tunnel ? "tunnel" : "session").arg(corrupt ? "corrupt" : "changed"))) << tunnel << corrupt;
    }
    void confirmationRace() {
        QFETCH(bool, tunnel); QFETCH(bool, corrupt);
        const QByteArray replacement = corrupt ? QByteArray("broken-record\n") : record(true) + "\r\n";
        const auto result = exercise(tunnel, [&](bool changed) { if (!changed) QVERIFY(write(replacement)); });
        QVERIFY(result.finished); QCOMPARE(result.authenticated, 0);
        QCOMPARE(result.prompts, corrupt ? QList<bool>{false} : (QList<bool>{false, true}));
        QVERIFY(result.reason.contains(corrupt ? "校验失败" : "变化")); QCOMPARE(read(), replacement);
    }
};
QTEST_GUILESS_MAIN(SshTrustTests)
#include "test_sshtrust.moc"
