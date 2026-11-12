#include "../src/core/session.h"
#include <QtTest>
class SessionTests : public QObject {
    Q_OBJECT
  private slots:
    void resumableProtocolWait(){
        const auto scenario=qEnvironmentVariable("RABBITSHELL_MONITOR_SCENARIO");const int port=qEnvironmentVariableIntValue("RABBITSHELL_MONITOR_PORT");
        if(!port)QSKIP("Requires private monitor_fixture.py; no real server");
        QTemporaryDir data;qputenv("RABBITSHELL_DATA_DIR",data.path().toUtf8());rs::DiagnosticLog::instance().startLogging();
        rs::Profile p;p.host="127.0.0.1";p.port=port;p.user="fixture";rs::Session s(p,"fixture-only");
        const auto cleanup=qScopeGuard([&]{s.stop();s.wait(2000);rs::DiagnosticLog::instance().stopLogging();qunsetenv("RABBITSHELL_DATA_DIR");});
        QSignalSpy ready(&s,&rs::Session::connected),closed(&s,&rs::Session::disconnected),done(&s,&rs::Session::completed),failed(&s,&rs::Session::failed),waiting(&s,&rs::Session::responseWaiting);QByteArray output;
        connect(&s,&rs::Session::hostKey,&s,[&](auto,bool){s.trust(true);});connect(&s,&rs::Session::terminalOutput,&s,[&](int,const QByteArray &b){output+=b;s.acknowledge(b.size());});
        s.start();QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,10000);rs::Job job;job.type="open-terminal";job.id=27;s.submit(job);QTRY_VERIFY_WITH_TIMEOUT(output.contains("private-fixture$"),3000);
        job={};job.type="monitor";job.id=99;s.submit(job);QTest::qWait(1200);
        if(scenario=="user-stop"){QVERIFY(closed.isEmpty());QElapsedTimer stop;stop.start();s.stop();QTRY_VERIFY_WITH_TIMEOUT(!s.isRunning(),2000);QVERIFY(stop.elapsed()<2000);}
        else if(scenario=="peer-close"){QTRY_VERIFY_WITH_TIMEOUT(!closed.isEmpty(),3000);}
        else{
            job={};job.type="input";job.id=27;job.data="PRIVATE_DURING_WAIT\r";s.submit(job);QTRY_VERIFY_WITH_TIMEOUT(output.contains("PRIVATE_DURING_WAIT"),1500);
            auto complete=[&]{for(const auto &row:done)if(row[0].toString()=="monitor")return row[2].toMap()["data"].toByteArray().contains("PRIVATE_MONITOR_RESULT");return false;};
            QTRY_VERIFY_WITH_TIMEOUT(complete(),12000);QCOMPARE(output.count("PRIVATE_DURING_WAIT"),1);QVERIFY(closed.isEmpty());QVERIFY(failed.isEmpty());
            bool on=false,off=false;for(const auto &row:waiting){if(row[0].toBool())on=true;else off=true;}QVERIFY(on&&off);
        }
        s.stop();QVERIFY(s.wait(2000));rs::DiagnosticLog::instance().stopLogging();
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");QDir().mkpath(report);QFile log(data.filePath("logs/diagnostic.jsonl"));QVERIFY(log.open(QIODevice::ReadOnly));const auto bytes=log.readAll();
        QVERIFY(!bytes.contains("fixture-only"));QVERIFY(!bytes.contains("127.0.0.1"));QVERIFY(!bytes.contains("PRIVATE_DURING_WAIT"));QVERIFY(!bytes.contains("Timed out"));
        bool exit=false;for(auto line:bytes.split('\n'))if(!line.isEmpty()){const auto o=QJsonDocument::fromJson(line).object();QVERIFY(line.size()<512);if(o["event"]=="ConnectionExit"){exit=true;QVERIFY(o.contains("stage")&&o.contains("mode")&&o.contains("sshErrno")&&o.contains("elapsedMs")&&o.contains("timeoutMs")&&o.contains("voluntary"));QCOMPARE(o["voluntary"].toBool(),scenario!="peer-close");}}
        QVERIFY(exit);QFile saved(report+"/connection-diagnostics.jsonl");QVERIFY(saved.open(QIODevice::WriteOnly));saved.write(bytes);
    }
    void sshAndSftp() {
        int port = qEnvironmentVariableIntValue("RABBITSHELL_TEST_PORT");
        if (!port)
            QSKIP("Set RABBITSHELL_TEST_PORT to the loopback-only ssh_fixture.py port");
        QTemporaryDir local;
        qputenv("RABBITSHELL_DATA_DIR", local.path().toUtf8());
        rs::Profile p;
        p.name = "Fixture";
        p.user = "fixture";
        p.host = "127.0.0.1";
        p.port = port;
        rs::Session s(p, "fixture-only");
        QSignalSpy connected(&s, &rs::Session::connected), closed(&s, &rs::Session::disconnected),
            done(&s, &rs::Session::completed), fail(&s, &rs::Session::failed),
            output(&s, &rs::Session::terminalOutput), stats(&s, &rs::Session::transferStatistics);
        connect(&s, &rs::Session::hostKey, &s, [&](const QString &fingerprint, bool changed) {
            QVERIFY(fingerprint.startsWith("SHA256:"));
            QVERIFY(!changed);
            s.trust(true);
        });
        connect(&s, &rs::Session::terminalOutput, &s,
                [&](int, const QByteArray &bytes) { s.acknowledge(bytes.size()); });
        s.start();
        QTRY_VERIFY_WITH_TIMEOUT(connected.count() == 1 || !closed.isEmpty(), 15000);
        QVERIFY2(connected.count() == 1,
                 closed.isEmpty() ? "not connected" : qPrintable(closed[0][0].toString()));
        auto run = [&](rs::Job j) {
            int count = done.size() + fail.size();
            s.submit(j);
            QElapsedTimer t;
            t.start();
            while (done.size() + fail.size() == count && t.elapsed() < 10000)
                QTest::qWait(10);
            return done.size() + fail.size() > count;
        };
        rs::Job j;
        j.type = "list";
        j.path = "/";
        j.id = 1;
        QVERIFY(run(j));
        QVERIFY2(fail.isEmpty(), fail.isEmpty() ? "" : qPrintable(fail.last()[2].toString()));
        QVERIFY(done.last()[2].toMap()["entries"].toList().size() >= 2);
        j.type = "read";
        j.path = "/config.json";
        j.id = 2;
        QVERIFY(run(j));
        auto original = done.last()[2].toMap();
        QCOMPARE(original["data"].toByteArray(), QByteArray("{\"version\":1}\n"));
        j.type = "save";
        j.id = 3;
        j.data = "{\"version\":2}\n";
        j.expectedHash = "incorrect";
        QVERIFY(run(j));
        QCOMPARE(fail.size(), 1);
        QVERIFY(fail.last()[2].toString().startsWith("CONFLICT:"));
        fail.clear();
        j.expectedHash = original["hash"].toByteArray();
        QVERIFY(run(j));
        QVERIFY2(fail.isEmpty(), fail.isEmpty() ? "" : qPrintable(fail.last()[2].toString()));
        j = {};
        j.type = "read";
        j.path = "/config.json";
        j.id = 4;
        QVERIFY(run(j));
        QCOMPARE(done.last()[2].toMap()["data"].toByteArray(), QByteArray("{\"version\":2}\n"));
        j = {};
        j.type = "open-terminal";
        j.id = 10;
        QVERIFY(run(j));
        QTRY_VERIFY_WITH_TIMEOUT(!output.isEmpty(), 3000);
        j.type = "input";
        j.data = "hello\r";
        s.submit(j);
        QTRY_VERIFY_WITH_TIMEOUT(output.size() >= 2, 3000);
        QVERIFY(output.last()[1].toByteArray().contains("hello"));
        j = {};
        j.type = "mkdir";
        j.path = "/created";
        j.id = 5;
        QVERIFY(run(j));
        QVERIFY(fail.isEmpty());
        QFile upload(local.filePath("upload.bin"));
        QVERIFY(upload.open(QIODevice::WriteOnly));
        QByteArray payload(256 * 1024, 'x');
        upload.write(payload);
        upload.close();
        j = {};
        j.type = "upload";
        j.id = 6;
        j.path = "/created/data.bin";
        j.destination = upload.fileName();
        j.collision = "overwrite";
        QVERIFY(run(j));
        QVERIFY2(fail.isEmpty(), fail.isEmpty() ? "" : qPrintable(fail.last()[2].toString()));
        QCOMPARE(done.last()[2].toMap()["payloadBytes"].toULongLong(), quint64(payload.size()));
        QVERIFY(!stats.isEmpty() && stats.last()[4].toBool());
        j.type = "download";
        j.id = 7;
        j.destination = local.filePath("download.bin");
        QVERIFY(run(j));
        QVERIFY(fail.isEmpty());
        QCOMPARE(done.last()[2].toMap()["payloadBytes"].toULongLong(), quint64(payload.size()));
        QFile downloaded(j.destination);
        QVERIFY(downloaded.open(QIODevice::ReadOnly));
        QCOMPARE(downloaded.readAll(), payload);
        downloaded.close();
        // A directory may contain an invalid local target. Other files still complete.
        QVERIFY(QDir().mkpath(local.filePath("partial/data.bin")));
        j.type = "download";
        j.path = "/created";
        j.destination = local.filePath("partial");
        QVERIFY(run(j));
        QCOMPARE(done.last()[2].toMap()["failed"].toInt(), 1);
        QCOMPARE(done.last()[2].toMap()["completed"].toInt(), 0);
        QVERIFY(!done.last()[2].toMap()["errors"].toStringList().isEmpty());
        j.path = "/created/data.bin";
        j.destination = local.filePath("download.bin");
        j.collision = "skip";
        QVERIFY(run(j));
        QCOMPARE(done.last()[2].toMap()["skipped"].toInt(), 1);
        QCOMPARE(done.last()[2].toMap()["payloadBytes"].toULongLong(), quint64(0));
        QCOMPARE(stats.last()[1].toULongLong(), quint64(0));
        // Directory totals count actual payload across short children, without per-file resets.
        const auto source = local.filePath("meter-directory"); QVERIFY(QDir().mkpath(source));
        for (int n = 0; n < 3; ++n) {
            QFile file(source + QString("/%1.txt").arg(n)); QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(QByteArray(n + 1, 's')), qint64(n + 1));
        }
        rs::Job metered; metered.id = 30; metered.type = "upload"; metered.path = "/meter-directory";
        metered.destination = source; metered.collision = "overwrite";
        stats.clear(); QVERIFY(run(metered)); QVERIFY(fail.isEmpty());
        QCOMPARE(done.last()[2].toMap()["payloadBytes"].toULongLong(), quint64(6));
        QCOMPARE(done.last()[2].toMap()["completed"].toInt(), 3);
        quint64 previous = 0;
        for (const auto &sample : stats) { QVERIFY(sample[1].toULongLong() >= previous); previous = sample[1].toULongLong(); }
        QCOMPARE(previous, quint64(6)); QVERIFY(stats.last()[4].toBool());
        metered.type = "download"; metered.destination = local.filePath("meter-download");
        QVERIFY(run(metered)); QCOMPARE(done.last()[2].toMap()["payloadBytes"].toULongLong(), quint64(6));
        metered.collision = "skip"; QVERIFY(run(metered));
        QCOMPARE(done.last()[2].toMap()["payloadBytes"].toULongLong(), quint64(0));
        QCOMPARE(done.last()[2].toMap()["skipped"].toInt(), 3);
        // A short empty file has a final zero-byte report independent of the progress throttle.
        QFile empty(local.filePath("empty")); QVERIFY(empty.open(QIODevice::WriteOnly)); empty.close();
        metered.type = "upload"; metered.path = "/meter-empty"; metered.destination = empty.fileName();
        metered.collision = "overwrite"; stats.clear(); QVERIFY(run(metered));
        QCOMPARE(done.last()[2].toMap()["payloadBytes"].toULongLong(), quint64(0));
        QCOMPARE(done.last()[2].toMap()["completed"].toInt(), 1);
        QCOMPARE(stats.last()[3].toDouble(), 0.0); QVERIFY(stats.last()[4].toBool());
        // Delay only this fixture file to exercise real pause, live intervals and attempt reset.
        metered.path = "/meter-slow.bin"; metered.destination = upload.fileName();
        bool pausedOnce = false; qint64 pauseWall = 0; QElapsedTimer wall; wall.start();
        auto pauseConnection = connect(&s, &rs::Session::transferStatistics, &s,
            [&](int id, quint64 bytes, qint64, double, bool final) {
                if (id != metered.id || !bytes || final || pausedOnce) return;
                pausedOnce = true; pauseWall = wall.elapsed(); s.pauseTransfer(true);
                QTimer::singleShot(300, &s, [&] { s.pauseTransfer(false); });
            });
        stats.clear(); QVERIFY(run(metered)); disconnect(pauseConnection);
        QVERIFY(pausedOnce); QVERIFY(wall.elapsed() > pauseWall + 250);
        auto measured = done.last()[2].toMap();
        QCOMPARE(measured["payloadBytes"].toULongLong(), quint64(payload.size()));
        QVERIFY(wall.elapsed() - measured["activeMs"].toLongLong() >= 240);
        QVERIFY(stats.size() >= 2);
        // Failure and retry on the same task ID must start at zero, including cancellation while paused.
        s.pauseTransfer(true); s.submit(metered); s.cancelTransfer();
        QTRY_VERIFY_WITH_TIMEOUT(!fail.isEmpty(), 3000);
        QCOMPARE(stats.last()[1].toULongLong(), quint64(0));
        fail.clear(); s.pauseTransfer(false); stats.clear();
        QVERIFY(run(metered)); QVERIFY(fail.isEmpty());
        QCOMPARE(done.last()[2].toMap()["payloadBytes"].toULongLong(), quint64(payload.size()));
        metered.type = "download"; metered.path = "/meter-directory"; metered.destination = local.filePath("partial-meter");
        QVERIFY(QDir().mkpath(metered.destination + "/0.txt"));
        QVERIFY(run(metered)); auto partial = done.last()[2].toMap();
        QCOMPARE(partial["failed"].toInt(), 1); QCOMPARE(partial["completed"].toInt(), 2);
        // The directory target fails before remote payload is read; only the two transferred files count.
        QCOMPARE(partial["payloadBytes"].toULongLong(), quint64(5));
        // Cancellation before a queued operation starts must not be reset by the worker.
        s.pauseTransfer(true);
        j.destination = local.filePath("cancelled.bin");
        j.collision = "overwrite";
        s.submit(j);
        s.cancelTransfer();
        QTRY_VERIFY_WITH_TIMEOUT(!fail.isEmpty(), 3000);
        QVERIFY(fail.last()[2].toString().contains("取消"));
        QVERIFY(!QFileInfo::exists(j.destination));
        fail.clear();
        s.pauseTransfer(false);
        j = {};
        j.type = "rename";
        j.id = 8;
        j.path = "/created/data.bin";
        j.destination = "/created/renamed.bin";
        QVERIFY(run(j));
        QVERIFY(fail.isEmpty());
        j = {};
        j.type = "delete";
        j.id = 9;
        j.path = "/created";
        j.recursive = true;
        QVERIFY(run(j));
        QVERIFY(fail.isEmpty());
        s.stop();
        QVERIFY(s.wait(7000));
        QVERIFY(QFileInfo::exists(local.filePath("known_hosts")));
        rs::Session wrong(p, "wrong");
        QSignalSpy rejected(&wrong, &rs::Session::disconnected);
        QSignalSpy passwordRejected(&wrong, &rs::Session::passwordRejected);
        wrong.start();
        QTRY_VERIFY_WITH_TIMEOUT(!rejected.isEmpty(), 10000);
        QVERIFY(rejected.last()[0].toString().contains("认证失败"));
        QCOMPARE(passwordRejected.size(), 1);
        wrong.stop();
        QVERIFY(wrong.wait(3000));
        auto keyProfile = p;
        keyProfile.authentication = "key";
        keyProfile.keyPath = qEnvironmentVariable("RABBITSHELL_TEST_KEY");
        QVERIFY(!keyProfile.keyPath.isEmpty());
        rs::Session badPassphrase(keyProfile, "synthetic-wrong-passphrase");
        QSignalSpy keyReview(&badPassphrase, &rs::Session::keyAuthenticationNeedsInput),
            keyPasswordRejected(&badPassphrase, &rs::Session::passwordRejected),
            keyClosed(&badPassphrase, &rs::Session::disconnected);
        badPassphrase.start();
        QTRY_VERIFY_WITH_TIMEOUT(!keyClosed.isEmpty(), 10000);
        QCOMPARE(keyReview.size(), 1);
        QVERIFY(keyPasswordRejected.isEmpty());
        QVERIFY(badPassphrase.wait(3000));
        rs::Session correctPassphrase(keyProfile, "fixture-passphrase");
        QSignalSpy keyConnected(&correctPassphrase, &rs::Session::connected),
            keyFailed(&correctPassphrase, &rs::Session::disconnected);
        correctPassphrase.start();
        QTRY_VERIFY_WITH_TIMEOUT(!keyConnected.isEmpty() || !keyFailed.isEmpty(), 10000);
        QCOMPARE(keyConnected.size(), 1);
        correctPassphrase.stop();
        QVERIFY(correctPassphrase.wait(3000));
        QFile known(local.filePath("known_hosts"));
        QVERIFY(known.open(QIODevice::ReadOnly));
        auto knownBytes = known.readAll();
        known.close();
        int end = knownBytes.indexOf('\n');
        if (end < 0)
            end = knownBytes.size();
        int alter = end - 5;
        QVERIFY(alter > 20);
        knownBytes[alter] = knownBytes[alter] == 'A' ? 'B' : 'A';
        QVERIFY(known.open(QIODevice::WriteOnly | QIODevice::Truncate));
        known.write(knownBytes);
        known.close();
        rs::Session changed(p, "fixture-only");
        QSignalSpy changedKey(&changed, &rs::Session::hostKey),
            changedPasswordRejected(&changed, &rs::Session::passwordRejected),
            changedConnected(&changed, &rs::Session::connected),
            changedClosed(&changed, &rs::Session::disconnected);
        changed.start();
        QTRY_VERIFY_WITH_TIMEOUT(!changedClosed.isEmpty(), 10000);
        QVERIFY(changedConnected.isEmpty());
        QVERIFY(changedPasswordRejected.isEmpty());
        QCOMPARE(changedKey.size(), 1);
        QVERIFY(changedKey[0][1].toBool());
        changed.stop();
        QVERIFY(changed.wait(3000));
        QTemporaryDir untrusted;
        qputenv("RABBITSHELL_DATA_DIR", untrusted.path().toUtf8());
        rs::Session cancelled(p, "fixture-only");
        QSignalSpy cancelledConnected(&cancelled, &rs::Session::connected),
            cancelledPasswordRejected(&cancelled, &rs::Session::passwordRejected),
            cancelledClosed(&cancelled, &rs::Session::disconnected);
        connect(&cancelled, &rs::Session::hostKey, &cancelled,
                [&](const QString &, bool) { cancelled.trust(false); });
        cancelled.start();
        QTRY_VERIFY_WITH_TIMEOUT(!cancelledClosed.isEmpty(), 10000);
        QVERIFY(cancelledConnected.isEmpty());
        QVERIFY(cancelledPasswordRejected.isEmpty());
        cancelled.stop();
        QVERIFY(cancelled.wait(3000));
        qunsetenv("RABBITSHELL_DATA_DIR");
    }
};
QTEST_GUILESS_MAIN(SessionTests)
#include "test_session.moc"
