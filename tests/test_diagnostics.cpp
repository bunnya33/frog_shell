#include "../src/core/diagnostics.h"
#include "../src/ui/diagnosticapplication.h"
#include <QtTest>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <crtdbg.h>
#include <exception>
static LONG WINAPI fixtureFilter(EXCEPTION_POINTERS *) { return EXCEPTION_CONTINUE_SEARCH; }
[[noreturn]] static void fixtureTerminate() { std::abort(); }
#endif

namespace rs {
class BlockingReceiver final : public QObject {
    bool event(QEvent *event) override {
        if(event->type()==QEvent::User){QThread::msleep(3200);return true;}
        return QObject::event(event);
    }
};
class CallbackReceiver final : public QObject {
  public:
    std::function<void()> callback;
    bool event(QEvent *event) override {
        if(event->type()>=QEvent::User){callback();return true;}
        return QObject::event(event);
    }
};
class DiagnosticTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
    QByteArray readLogs() {
        QByteArray bytes;
        for (const auto &name : QDir(DiagnosticLog::directory()).entryList({"*.jsonl"},QDir::Files)) {
            QFile file(QDir(DiagnosticLog::directory()).filePath(name));
            if(file.open(QIODevice::ReadOnly))bytes+=file.readAll();
        }
        return bytes;
    }
  private slots:
    void initTestCase() {
        QVERIFY(data.isValid());qputenv("RABBITSHELL_DATA_DIR",data.path().toUtf8());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("RabbitShellDiagnosticTests");
        QCOMPARE(DiagnosticLog::directory(),data.filePath("logs"));
    }
    void fatalCaptureInIsolatedProcesses() {
#ifdef Q_OS_WIN
        auto runChild = [](const QString &mode, const QString &path) {
            QProcess child;
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("RABBITSHELL_DATA_DIR", path); env.insert("QT_QPA_PLATFORM", "offscreen");
            child.setProcessEnvironment(env);
            child.start(QCoreApplication::applicationFilePath(), {"--diagnostic-child", mode});
            if (!child.waitForStarted(10000) || !child.waitForFinished(15000)) return qint64(-1);
            return qint64(quint32(child.exitCode()));
        };
        auto records = [](const QString &path) {
            QList<QJsonObject> result;
            const QDir logs(QDir(path).filePath("logs"));
            for (const auto &name : logs.entryList({"crash-*.log"}, QDir::Files)) {
                QFile file(logs.filePath(name));
                if (!file.open(QIODevice::ReadOnly)) continue;
                const auto bytes = file.readAll();
                if (bytes.isEmpty()) continue;
                const auto document = QJsonDocument::fromJson(bytes);
                result.append(document.object());
            }
            return result;
        };
        QTemporaryDir normal, exception, terminated, blocked;
        QVERIFY(normal.isValid() && exception.isValid() && terminated.isValid() && blocked.isValid());
        QCOMPARE(runChild("normal", normal.path()), qint64(0));
        QVERIFY(records(normal.path()).isEmpty());
        QCOMPARE(runChild("restart", normal.path()), qint64(0));
        QVERIFY(records(normal.path()).isEmpty());
        QCOMPARE(runChild("exception", exception.path()), qint64(0xe0424242u));
        auto rows = records(exception.path()); QCOMPARE(rows.size(), 1); const auto row = rows.first();
        QCOMPARE(row.value("event").toString(), QString("UnhandledException"));
        QCOMPARE(row.value("code").toString(), QString("0xe0424242"));
        QVERIFY(!row.value("v").toString().isEmpty());
        QVERIFY(QDateTime::fromString(row.value("utc").toString(), Qt::ISODateWithMs).isValid());
        QVERIFY(row.value("pid").toInt() > 0); QVERIFY(row.value("tid").toInt() > 0);
        QVERIFY(!row.value("module").toString().isEmpty()); QVERIFY(row.value("offset").toString().startsWith("0x"));
        const QSet<QString> allowed{"v", "event", "utc", "pid", "tid", "code", "module", "offset"};
        for (auto it = row.begin(); it != row.end(); ++it) QVERIFY(allowed.contains(it.key()));
        QVERIFY(runChild("terminate", terminated.path()) != 0);
        rows = records(terminated.path()); QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().value("event").toString(), QString("Terminate"));
        // A non-directory data root makes both log paths fail without preventing normal exit.
        QFile obstacle(blocked.filePath("file")); QVERIFY(obstacle.open(QIODevice::WriteOnly)); obstacle.close();
        QCOMPARE(runChild("normal", obstacle.fileName()), qint64(0));
        QVERIFY(!QFileInfo::exists(obstacle.fileName() + "/logs"));

        // Fill all slots; a new crash replaces one old record, never grows the set.
        const QDir logs(QDir(exception.path()).filePath("logs"));
        for (int i = 0; i < 16; ++i) {
            QFile file(logs.filePath(QString("crash-%1.log").arg(i, 2, 10, QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{\"event\":\"old\"}\n");
        }
        QCOMPARE(runChild("exception", exception.path()), qint64(0xe0424242u));
        QCOMPARE(logs.entryList({"crash-*.log"}, QDir::Files).size(), 16);
        rows = records(exception.path()); QCOMPARE(rows.size(), 16);
        int found = 0;
        for (const auto &item : rows) if (item.value("event") == "UnhandledException") ++found;
        QCOMPARE(found, 1);
        for (const auto &name : logs.entryList({"crash-*.log"}, QDir::Files)) QVERIFY(QFileInfo(logs.filePath(name)).size() < 512);

        QTemporaryDir occupied; QVERIFY(occupied.isValid());
        QVERIFY(QDir().mkpath(occupied.filePath("logs")));
        QList<HANDLE> locks;
        for (int i = 0; i < 16; ++i) {
            const auto path = QDir::toNativeSeparators(occupied.filePath(QString("logs/crash-%1.log").arg(i, 2, 10, QLatin1Char('0'))));
            HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_WRITE, FILE_SHARE_READ,
                                        nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            QVERIFY(handle != INVALID_HANDLE_VALUE); locks.append(handle);
        }
        const auto occupiedExit = runChild("exception", occupied.path());
        for (const auto handle : locks) CloseHandle(handle);
        QCOMPARE(occupiedExit, qint64(0xe0424242u));
        QVERIFY(records(occupied.path()).isEmpty());
#else
        QSKIP("Windows exception capture only");
#endif
    }
    void backgroundPersistsDuringArtificialUiBlock() {
        auto &log=DiagnosticLog::instance();log.startLogging();
        diagnose(DiagEvent::RemoveBegin,123,77,"/private/never-log-this-filename",12);
        // Intentional diagnostic fixture, NOT a reproduction of the user's hang.
        // No event processing on the UI thread; writer/watchdog must work independently.
        BlockingReceiver receiver;receiver.setObjectName("never-log-this-objectname");
        QEvent event(QEvent::User);QCoreApplication::sendEvent(&receiver,&event);
        const auto bytes=readLogs();
        QVERIFY(bytes.contains("RemoveBegin"));QVERIFY(bytes.contains("UiStall"));
        QVERIFY(!bytes.contains("never-log-this"));
        bool stalled=false;
        for(const auto &line:bytes.split('\n'))if(!line.isEmpty()) {
            const auto object=QJsonDocument::fromJson(line).object();QVERIFY(!object.isEmpty());
            if(object["event"]=="UiStall") {
                stalled=true;QCOMPARE(object["lastUi"].toString(),QString("RemoveBegin"));
                QCOMPARE(object["job"].toInt(),77);QVERIFY(object["uiIdleMs"].toInteger()>=2000);
                QCOMPARE(object["thread"].toString(),QString("worker"));
                QCOMPARE(object["pathHash"].toString().size(),24);
                QCOMPARE(object["dispatchEvent"].toInt(),int(QEvent::User));
                QCOMPARE(object["dispatchClass"].toString(),QString("QObject"));
                QVERIFY(object["dispatchDepth"].toInt()>=1);QVERIFY(object["dispatchAgeMs"].toInteger()>=2000);
            }
        }
        QVERIFY(stalled);QTest::qWait(550);log.stopLogging();
        const auto report=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR");
        if(!report.isEmpty()){QFile file(report+"/artificial-ui-stall.jsonl");QVERIFY(file.open(QIODevice::WriteOnly));file.write(readLogs());}
        QVERIFY(!log.isRunning());QVERIFY(!log.pulse.isActive());QVERIFY(readLogs().contains("Stopped"));
        const auto size=readLogs().size();diagnose(DiagEvent::DeleteBegin,1,999);QTest::qWait(20);QCOMPARE(readLogs().size(),size);
    }
    void notifyNestingRestoresOuterAndIdle() {
        auto &log=DiagnosticLog::instance();log.startLogging();
        auto state=[&]{QMutexLocker lock(&log.mutex);return log.currentDispatch;};
        const auto initial=state();DiagnosticDispatchState outerState;
        CallbackReceiver outer,inner;inner.setParent(&outer);
        inner.setObjectName("private-never-log-name");
        inner.callback=[&]{const auto nested=state();QCOMPARE(nested.event,int(QEvent::User)+1);
            QCOMPARE(nested.depth,outerState.depth+1);QCOMPARE(nested.receiver,"QObject");QCOMPARE(nested.owner,"QObject");};
        outer.callback=[&]{outerState=state();QCOMPARE(outerState.event,int(QEvent::User));QCOMPARE(outerState.depth,initial.depth+1);
            QEvent nested(QEvent::Type(int(QEvent::User)+1));QCoreApplication::sendEvent(&inner,&nested);
            const auto restored=state();QCOMPARE(restored.event,outerState.event);QCOMPARE(restored.depth,outerState.depth);
            QCOMPARE(restored.started,outerState.started);QCOMPARE(restored.receiver,outerState.receiver);};
        QEvent event(QEvent::User);QCoreApplication::sendEvent(&outer,&event);
        const auto restored=state();QCOMPARE(restored.event,initial.event);QCOMPARE(restored.depth,initial.depth);
        QCOMPARE(restored.started,initial.started);log.stopLogging();QVERIFY(!readLogs().contains("private-never-log-name"));
    }
    void boundedQueueRotationAndSchema() {
        auto &log=DiagnosticLog::instance();log.maxFileBytes=4096;log.startLogging();
        for(int batch=0;batch<4;++batch) {
            for(int i=0;i<5000;++i)diagnose(DiagEvent::ListResult,456,i,"/hash-only/path",i);
            {QMutexLocker lock(&log.mutex);QVERIFY(log.queue.size()<=512);QVERIFY(log.queuedBytes<=256*1024);QVERIFY(log.dropped>0);}
            QTest::qWait(1100);
        }
        log.stopLogging();
        const auto names=QDir(log.directory()).entryList({"*.jsonl"},QDir::Files);QCOMPARE(names.size(),3);
        for(const auto &name:names)QVERIFY(QFileInfo(QDir(log.directory()).filePath(name)).size()<=4096);
        const auto bytes=readLogs();QVERIFY(bytes.contains("Overflow"));QVERIFY(!bytes.contains("hash-only"));
        const QSet<QString> allowed{"v","ms","event","thread","session","job","count","pathHash","lastUi","uiIdleMs",
            "dispatchEvent","dispatchClass","dispatchOwner","dispatchDepth","dispatchAgeMs"};
        for(const auto &line:bytes.split('\n'))if(!line.isEmpty()) {
            const auto object=QJsonDocument::fromJson(line).object();QVERIFY(!object.isEmpty());QVERIFY(line.size()<512);
            for(auto it=object.begin();it!=object.end();++it)QVERIFY(allowed.contains(it.key()));
        }
        log.maxFileBytes=2*1024*1024;
    }
};
}
int main(int argc,char **argv){
#ifdef Q_OS_WIN
    const bool child = argc == 3 && QByteArray(argv[1]) == "--diagnostic-child";
    if (child) {
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_error_mode(_OUT_TO_STDERR);
    }
#endif
    rs::DiagnosticApplication app(argc,argv);
#ifdef Q_OS_WIN
    if (child) {
        if (QByteArray(argv[2]) == "restart") {
            const auto oldFilter = SetUnhandledExceptionFilter(fixtureFilter);
            const auto oldTerminate = std::set_terminate(fixtureTerminate);
            bool restored = true;
            for (int i = 0; i < 2; ++i) {
                rs::DiagnosticLog::instance().startLogging();
                rs::DiagnosticLog::instance().stopLogging();
                restored &= SetUnhandledExceptionFilter(fixtureFilter) == fixtureFilter;
                restored &= std::set_terminate(fixtureTerminate) == fixtureTerminate;
            }
            SetUnhandledExceptionFilter(oldFilter); std::set_terminate(oldTerminate);
            return restored ? 0 : 3;
        }
        rs::DiagnosticLog::instance().startLogging();
        if (QByteArray(argv[2]) == "exception") RaiseException(0xe0424242u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        if (QByteArray(argv[2]) == "terminate") std::terminate();
        rs::DiagnosticLog::instance().stopLogging(); return 0;
    }
#endif
    rs::DiagnosticTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_diagnostics.moc"
