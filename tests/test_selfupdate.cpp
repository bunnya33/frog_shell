#include "../src/core/selfupdate.h"
#include <QtTest>
#include <future>
#include <atomic>
#define NOMINMAX
#include <windows.h>

using namespace rs::selfupdate;
namespace {
void put(const QString &path,const QByteArray &data){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(data)!=data.size())qFatal("fixture write");}
QByteArray read(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
QJsonObject json(const QString &path){return QJsonDocument::fromJson(read(path)).object();}
QString payload(const PreparedInstall &p){return json(p.planPath()).value("source").toString();}
void evidence(const QString &label,const QString &receipt){
    const auto base=qEnvironmentVariable("SELFUPDATE_EVIDENCE_DIR");if(base.isEmpty())return;
    put(base+"/"+label+"-status.json",read(receipt));put(base+"/"+label+"-plan.json",read(QFileInfo(receipt).absolutePath()+"/install.json"));
}
bool waitPhase(const QString &receipt,const QString &phase,int timeout,const QString &label){
    QElapsedTimer timer;timer.start();
    do {if(json(receipt).value("phase").toString()==phase){evidence(label,receipt);return true;}QTest::qWait(50);}while(timer.elapsed()<timeout);
    evidence(label,receipt);return false;
}
ProcessIdentity observe(quint32 pid){
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!h)qFatal("fixture open process");
    FILETIME c{},e{},k{},u{};wchar_t path[32768]{};DWORD n=32768;
    if(!GetProcessTimes(h,&c,&e,&k,&u)||!QueryFullProcessImageNameW(h,0,path,&n))qFatal("fixture process identity");
    CloseHandle(h);return {pid,(quint64(c.dwHighDateTime)<<32)|c.dwLowDateTime,QDir::fromNativeSeparators(QString::fromWCharArray(path,int(n)))};
}
QJsonObject processRecord(const ProcessIdentity &p){return {{"pid",QString::number(p.pid)},{"created",QString::number(p.created)},{"image",p.imagePath}};}
bool awaitOwnedProcess(const QJsonObject &o,const QString &label){
    const auto pid=o.value("pid").toString().toUInt();const auto created=o.value("created").toString().toULongLong();if(!pid||!created)return false;
    HANDLE h=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);bool finished=false;QString state;
    if(!h){finished=GetLastError()==ERROR_INVALID_PARAMETER;state="already-exited";}
    else {
        FILETIME c{},e{},k{},u{};wchar_t path[32768]{};DWORD count=32768;
        if(GetProcessTimes(h,&c,&e,&k,&u)) {
            const auto actualCreated=(quint64(c.dwHighDateTime)<<32)|c.dwLowDateTime;
            if(actualCreated!=created){finished=true;state="original-exited-pid-reused";}
            else if(WaitForSingleObject(h,0)==WAIT_OBJECT_0){finished=true;state="already-signaled-creation-matched";}
            else if(QueryFullProcessImageNameW(h,0,path,&count)) {
                const auto actualPath=QDir::fromNativeSeparators(QString::fromWCharArray(path,int(count)));
                if(actualPath.compare(o.value("image").toString(),Qt::CaseInsensitive)!=0){finished=false;state="unexpected-live-image";}
                else {finished=WaitForSingleObject(h,10000)==WAIT_OBJECT_0;state=finished?"wait-signaled":"still-live";}
            }
        }
        CloseHandle(h);
    }
    const auto base=qEnvironmentVariable("SELFUPDATE_EVIDENCE_DIR");if(!base.isEmpty()){auto audit=o;audit.insert("finished",finished);audit.insert("state",state);put(base+"/"+label+"-process-audit.json",QJsonDocument(audit).toJson());}
    return finished;
}
struct Processes {
    QProcess launcher,main;
    HANDLE launcherEvent=nullptr,mainEvent=nullptr;
    QString target,runtime,download,base,config;
    PortableOrigin origin;ProcessIdentity app;VerifiedUpdate update;
    QByteArray oldBytes,newBytes="MZ fixture only - never an executable update\r\n";
    explicit Processes(const QString &root) {
        target=root+"/中文 空格/外层程序.exe";runtime=root+"/runtime/main.exe";download=root+"/download/new.exe";base=root+"/plans";config=root+"/配置/connections.json";
        QDir().mkpath(QFileInfo(target).absolutePath());QDir().mkpath(QFileInfo(runtime).absolutePath());QDir().mkpath(base);
        if(!QFile::copy(QCoreApplication::applicationFilePath(),target)||!QFile::copy(QCoreApplication::applicationFilePath(),runtime))qFatal("fixture binary copy");
        oldBytes=read(target);put(download,newBytes);put(config,"configuration bytes including drafts remain untouched");
        auto start=[&](QProcess &p,const QString &exe,HANDLE &event) {
            QString name="Local\\FrogShellSelfUpdateFixture-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            auto native=name.toStdWString();event=CreateEventW(nullptr,TRUE,FALSE,native.c_str());if(!event)qFatal("fixture event");
            p.start(exe,{"--fixture-process",name});if(!p.waitForStarted(5000))qFatal("fixture start");
        };
        start(launcher,target,launcherEvent);origin.singleExecutable=true;origin.launcher=observe(quint32(launcher.processId()));
        if(!snapshotFile(target,&origin.originalFile).ok())qFatal("fixture old snapshot");
        start(main,runtime,mainEvent);app=observe(quint32(main.processId()));
        update={download,QCryptographicHash::hash(newBytes,QCryptographicHash::Sha256),quint64(newBytes.size())};
    }
    void endMain(){SetEvent(mainEvent);if(!main.waitForFinished(10000))qFatal("fixture main did not exit");}
    void endLauncher(){SetEvent(launcherEvent);if(!launcher.waitForFinished(10000))qFatal("fixture launcher did not exit");}
    void end(){if(main.state()!=QProcess::NotRunning)endMain();if(launcher.state()!=QProcess::NotRunning)endLauncher();}
    ~Processes(){end();if(mainEvent)CloseHandle(mainEvent);if(launcherEvent)CloseHandle(launcherEvent);}
    Result prepare(PreparedInstall &p){return fixture::prepare(origin,app,update,base,&p);}
};
QProcessEnvironment originEnv(const PortableOrigin &o){
    auto env=QProcessEnvironment::systemEnvironment();for(auto key:env.keys())if(key.startsWith("RABBITSHELL_PORTABLE_"))env.remove(key);
    const QString p="RABBITSHELL_PORTABLE_ORIGIN_";
    env.insert(p+"KIND","single-exe-v1");env.insert(p+"PATH",o.launcher.imagePath);env.insert(p+"PID",QString::number(o.launcher.pid));
    env.insert(p+"CREATED",QString::number(o.launcher.created));env.insert(p+"VOLUME",QString::number(o.originalFile.volume));
    env.insert(p+"INDEX",QString::number(o.originalFile.index));env.insert(p+"BYTES",QString::number(o.originalFile.bytes));
    env.insert(p+"MODIFIED",QString::number(o.originalFile.modified));env.insert(p+"SHA256",QString::fromLatin1(o.originalFile.sha256.toHex()));return env;
}
int fixtureMain(const QString &jobPath){
    auto job=json(jobPath);PortableOrigin origin;if(!originFromEnvironment(QProcessEnvironment::systemEnvironment(),&origin).ok())return 70;
    VerifiedUpdate update{job.value("download").toString(),QByteArray::fromHex(job.value("hash").toString().toLatin1()),quint64(read(job.value("download").toString()).size())};
    PreparedInstall p;auto r=prepareInstall(origin,update,job.value("base").toString(),&p);if(!r.ok())return 71;
    if(job.contains("mode"))put(QFileInfo(p.planPath()).absolutePath()+"/fixture-mode.json",QJsonDocument(QJsonObject{{"mode",job.value("mode")},{"event",job.value("event")}}).toJson());
    put(job.value("handoff").toString(),QJsonDocument(QJsonObject{{"receipt",p.receiptPath()},{"plan",p.planPath()}}).toJson());
    quint32 helper=0;r=startAfterMainLoopExit(&p,&helper);
    if(r.ok())put(job.value("handoff").toString(),QJsonDocument(QJsonObject{{"receipt",p.receiptPath()},{"plan",p.planPath()},{"helper",processRecord(observe(helper))}}).toJson());
    return r.ok()?0:72;
}
int fixtureParent(const QString &jobPath){
    auto job=json(jobPath);PortableOrigin origin;origin.singleExecutable=true;
    if(!currentProcessIdentity(&origin.launcher).ok()||!snapshotFile(origin.launcher.imagePath,&origin.originalFile).ok())return 73;
    QProcess main;main.setProcessEnvironment(originEnv(origin));main.start(job.value("runtime").toString(),{"--fixture-main",jobPath});
    if(!main.waitForStarted(5000)||!main.waitForFinished(20000))return 74;return main.exitCode();
}
int fixtureAckMain(const QString &receipt,const QString &token){
    const auto dir=QFileInfo(receipt).absolutePath();const auto mode=json(dir+"/fixture-mode.json");
    if(mode.value("mode").toString()=="live") {
        const auto name=mode.value("event").toString().toStdWString();HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,name.c_str());if(!event)return 85;
        put(dir+"/fixture-live-ready","ready");WaitForSingleObject(event,60000);CloseHandle(event);put(dir+"/fixture-live-exit","exited");return 0;
    }
    PortableOrigin origin;auto r=originFromEnvironment(QProcessEnvironment::systemEnvironment(),&origin);
    if(r.ok())r=acknowledgeNormalStartup(receipt,token,origin);
    const auto again=r.ok()?acknowledgeNormalStartup(receipt,token,origin):r;
    put(dir+"/fixture-ack-result.json",QJsonDocument(QJsonObject{{"code",int(r.code)},{"again",int(again.code)},{"message",r.message}}).toJson());return r.ok()&&again.ok()?0:86;
}
int fixtureInstalledLauncher(const QString &receipt,const QString &token){
    const auto dir=QFileInfo(receipt).absolutePath(),mode=json(dir+"/fixture-mode.json").value("mode").toString();
    if(mode=="fail")return 87;
    PortableOrigin origin;origin.singleExecutable=true;
    if(!currentProcessIdentity(&origin.launcher).ok()||!snapshotFile(origin.launcher.imagePath,&origin.originalFile).ok())return 88;
    put(dir+"/fixture-new-launcher.json",QJsonDocument(processRecord(origin.launcher)).toJson());
    const auto runtime=json(dir+"/install.json").value("helper").toString();QProcess main;main.setProcessEnvironment(originEnv(origin));
    main.start(runtime,{"--fixture-ack-main",receipt,token});if(!main.waitForStarted(5000)||!main.waitForFinished(65000))return 89;return main.exitCode();
}
}
class SelfUpdateTests:public QObject {
    Q_OBJECT
private slots:
    void prepareCancelHasNoHelper(){
        QTemporaryDir temp;Processes f(temp.path());PreparedInstall p;auto r=f.prepare(p);QVERIFY2(r.ok(),qPrintable(r.message));
        const auto privatePayload=payload(p),plan=p.planPath(),dir=QFileInfo(plan).absolutePath();
        QVERIFY(QFileInfo::exists(privatePayload));QVERIFY(!json(plan).value("appHandle").toString().toULongLong());
        QCOMPARE(read(f.target),f.oldBytes);QVERIFY(QFile::remove(f.download)); // Original download lifetime is independent.
        r=discardPreparedInstall(&p);QVERIFY2(r.ok(),qPrintable(r.message));QVERIFY(!p.valid());QVERIFY(!QFileInfo::exists(dir));
        QCOMPARE(read(f.target),f.oldBytes);QCOMPARE(read(f.config),QByteArray("configuration bytes including drafts remain untouched"));
    }
    void waitsBothProcessesThenInstalls(){
        QTemporaryDir temp;Processes f(temp.path());PreparedInstall p;QVERIFY(f.prepare(p).ok());const auto receipt=p.receiptPath(),owned=payload(p);
        QVERIFY(QFile::remove(f.download));
        auto future=std::async(std::launch::async,[&]{fixture::Options o;o.waitMs=5000;return fixture::install(&p,o);});
        QTest::qWait(80);QCOMPARE(future.wait_for(std::chrono::milliseconds(0)),std::future_status::timeout);QCOMPARE(read(f.target),f.oldBytes);
        f.endMain();QTest::qWait(80);QCOMPARE(future.wait_for(std::chrono::milliseconds(0)),std::future_status::timeout);QCOMPARE(read(f.target),f.oldBytes);
        f.endLauncher();auto r=future.get();QVERIFY2(r.ok(),qPrintable(r.message));QCOMPARE(read(f.target),f.newBytes);QVERIFY(!QFileInfo::exists(owned));
        InstallStatus status;QVERIFY(readInstallStatus(receipt,&status).ok());QCOMPARE(status.phase,QString("installed-awaiting-startup"));
        QCOMPARE(read(status.backupPath),f.oldBytes);QCOMPARE(read(f.config),QByteArray("configuration bytes including drafts remain untouched"));
    }
    void cancelWhileWaiting(){
        QTemporaryDir temp;Processes f(temp.path());PreparedInstall p;QVERIFY(f.prepare(p).ok());std::atomic_bool stop=false;
        auto future=std::async(std::launch::async,[&]{fixture::Options o;o.canceled=[&]{return stop.load();};return fixture::install(&p,o);});
        QTest::qWait(50);stop=true;auto r=future.get();QCOMPARE(r.code,Code::Canceled);QCOMPARE(read(f.target),f.oldBytes);
    }
    void occupiedTarget(){
        QTemporaryDir temp;Processes f(temp.path());PreparedInstall p;QVERIFY(f.prepare(p).ok());f.end();
        const auto path=QDir::toNativeSeparators(f.target).toStdWString();HANDLE lock=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);QVERIFY(lock!=INVALID_HANDLE_VALUE);
        auto r=fixture::install(&p);CloseHandle(lock);QCOMPARE(r.code,Code::InUse);QCOMPARE(read(f.target),f.oldBytes);
    }
    void changedInputsAndProcessIdentity(){
        QTemporaryDir temp;Processes f(temp.path());PreparedInstall p;
        auto wrong=f.origin;++wrong.launcher.created;auto r=fixture::prepare(wrong,f.app,f.update,f.base,&p);QCOMPARE(r.code,Code::Changed);
        auto bad=f.update;bad.sha256=QByteArray(32,'x');r=fixture::prepare(f.origin,f.app,bad,f.base,&p);QCOMPARE(r.code,Code::Changed);
        QVERIFY(f.prepare(p).ok());put(payload(p),"changed owned payload");f.end();r=fixture::install(&p);QCOMPARE(r.code,Code::Changed);QCOMPARE(read(f.target),f.oldBytes);
    }
    void changedTargetPath(){
        QTemporaryDir temp;Processes f(temp.path());PreparedInstall p;QVERIFY(f.prepare(p).ok());f.end();
        QVERIFY(QFile::rename(f.target,f.target+".moved"));put(f.target,"different target");auto r=fixture::install(&p);QCOMPARE(r.code,Code::Changed);
        QCOMPARE(read(f.target),QByteArray("different target"));QCOMPARE(read(f.target+".moved"),f.oldBytes);
    }
    void linksRejected(){
        QTemporaryDir temp;Processes f(temp.path());const auto link=temp.filePath("hard.exe");
        auto dst=QDir::toNativeSeparators(link).toStdWString(),src=QDir::toNativeSeparators(f.download).toStdWString();
        QVERIFY(CreateHardLinkW(dst.c_str(),src.c_str(),nullptr));PreparedInstall p;auto r=f.prepare(p);QCOMPARE(r.code,Code::InvalidInput);
        QVERIFY(QFile::remove(link));const auto junction=temp.filePath("联接");QProcess mklink;
        mklink.start("cmd.exe",{"/c","mklink","/J",QDir::toNativeSeparators(junction),QDir::toNativeSeparators(f.base)});QVERIFY(mklink.waitForFinished(5000));QCOMPARE(mklink.exitCode(),0);
        r=fixture::prepare(f.origin,f.app,f.update,junction,&p);QCOMPARE(r.code,Code::Changed);
    }
    void rollback_data(){QTest::addColumn<int>("point");QTest::newRow("after-backup")<<1;QTest::newRow("after-install")<<2;QTest::newRow("launch-failure")<<3;}
    void rollback(){
        QFETCH(int,point);QTemporaryDir temp;Processes f(temp.path());PreparedInstall p;QVERIFY(f.prepare(p).ok());auto receipt=p.receiptPath();f.end();
        fixture::Options o;o.failAt=fixture::FailAt(point);auto r=fixture::install(&p,o);evidence("rollback-"+QString::number(point),receipt);QVERIFY2(r.code==Code::RolledBack,qPrintable(r.message));
        QCOMPARE(read(f.target),f.oldBytes);InstallStatus s;QVERIFY(readInstallStatus(receipt,&s).ok());QCOMPARE(s.phase,QString("rolled-back"));
        QCOMPARE(read(f.config),QByteArray("configuration bytes including drafts remain untouched"));
    }
    void realHelperHandshakeAndRollback(){
        QTemporaryDir temp;const auto root=temp.path();const auto target=root+"/外层 空格.exe",runtime=root+"/runtime/main.exe",download=root+"/new.exe",base=root+"/plans",handoff=root+"/handoff.json",job=root+"/job.json";
        QDir().mkpath(QFileInfo(runtime).absolutePath());QDir().mkpath(base);QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),target));QVERIFY(QFile::copy(QCoreApplication::applicationFilePath(),runtime));
        const auto old=read(target);const QByteArray fake="MZ invalid fixture; never a real new version";put(download,fake);
        put(job,QJsonDocument(QJsonObject{{"runtime",runtime},{"download",download},{"base",base},{"handoff",handoff},{"hash",QString::fromLatin1(QCryptographicHash::hash(fake,QCryptographicHash::Sha256).toHex())}}).toJson());
        QProcess parent;parent.start(target,{"--fixture-parent",job});QVERIFY(parent.waitForStarted(5000));QVERIFY(parent.waitForFinished(20000));
        QCOMPARE(parent.exitCode(),0);const auto receipt=json(handoff).value("receipt").toString();QVERIFY(!receipt.isEmpty());
        QVERIFY2(waitPhase(receipt,"rolled-back",15000,"invalid-exe"),qPrintable(QString::fromUtf8(read(receipt))));QCOMPARE(read(target),old);
        QVERIFY(awaitOwnedProcess(json(handoff).value("helper").toObject(),"invalid-exe-helper"));
    }
    void realInstalledStartup_data(){
        QTest::addColumn<QString>("mode");
        for(int i=1;i<=5;++i){QTest::newRow(qPrintable("acknowledged-"+QString::number(i)))<<QString("success");QTest::newRow(qPrintable("startup-failed-"+QString::number(i)))<<QString("fail");}
    }
    void realInstalledStartup(){
        QFETCH(QString,mode);QTemporaryDir temp;const auto root=temp.path();const auto target=root+"/有效更新 外层.exe",runtime=root+"/runtime/main.exe",download=root+"/new.exe",base=root+"/plans",handoff=root+"/handoff.json",job=root+"/job.json";
        QDir().mkpath(QFileInfo(runtime).absolutePath());QDir().mkpath(base);const auto binary=read(QCoreApplication::applicationFilePath());
        const auto old=binary+"OLD-FIXTURE",fresh=binary+"NEW-FIXTURE";put(target,old);put(runtime,binary);put(download,fresh);put(root+"/config/drafts","unchanged");
        put(job,QJsonDocument(QJsonObject{{"runtime",runtime},{"download",download},{"base",base},{"handoff",handoff},{"mode",mode},{"hash",QString::fromLatin1(QCryptographicHash::hash(fresh,QCryptographicHash::Sha256).toHex())}}).toJson());
        QProcess parent;parent.start(target,{"--fixture-parent",job});QVERIFY(parent.waitForStarted(5000));QVERIFY(parent.waitForFinished(20000));QCOMPARE(parent.exitCode(),0);
        const auto receipt=json(handoff).value("receipt").toString();QVERIFY(!receipt.isEmpty());const auto dir=QFileInfo(receipt).absolutePath();
        const auto expected=mode=="success"?QString("complete"):QString("rolled-back");QVERIFY2(waitPhase(receipt,expected,15000,"real-"+QString::fromLatin1(QTest::currentDataTag())),qPrintable(QString::fromUtf8(read(receipt))));
        const auto label=QString::fromLatin1(QTest::currentDataTag());
        const bool helperEnded=awaitOwnedProcess(json(handoff).value("helper").toObject(),label+"-helper");
        const bool launcherEnded=mode!="success"||awaitOwnedProcess(json(dir+"/fixture-new-launcher.json"),label+"-new-launcher");
        QVERIFY(helperEnded);QVERIFY(launcherEnded);
        QCOMPARE(read(target),mode=="success"?fresh:old);QCOMPARE(read(root+"/config/drafts"),QByteArray("unchanged"));
        if(mode=="success"){QCOMPARE(json(dir+"/fixture-ack-result.json").value("code").toInt(-1),0);QCOMPARE(json(dir+"/fixture-ack-result.json").value("again").toInt(-1),0);QVERIFY(!QFileInfo::exists(json(receipt).value("backup").toString()));}
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(dir+"/payload.exe"),5000);
    }
    void liveWithoutAckKeepsBackup(){
        QTemporaryDir temp;const auto root=temp.path();const auto target=root+"/live 外层.exe",runtime=root+"/runtime/main.exe",download=root+"/new.exe",base=root+"/plans",handoff=root+"/handoff.json",job=root+"/job.json";
        QDir().mkpath(QFileInfo(runtime).absolutePath());QDir().mkpath(base);const auto binary=read(QCoreApplication::applicationFilePath()),old=binary+"OLD",fresh=binary+"NEW";
        put(target,old);put(runtime,binary);put(download,fresh);
        const auto eventName="Local\\FrogShellSelfUpdateLive-"+QUuid::createUuid().toString(QUuid::WithoutBraces);const auto nativeName=eventName.toStdWString();HANDLE stop=CreateEventW(nullptr,TRUE,FALSE,nativeName.c_str());QVERIFY(stop);
        put(job,QJsonDocument(QJsonObject{{"runtime",runtime},{"download",download},{"base",base},{"handoff",handoff},{"mode","live"},{"event",eventName},{"hash",QString::fromLatin1(QCryptographicHash::hash(fresh,QCryptographicHash::Sha256).toHex())}}).toJson());
        QProcess parent;parent.start(target,{"--fixture-parent",job});QVERIFY(parent.waitForStarted(5000));QVERIFY(parent.waitForFinished(20000));QCOMPARE(parent.exitCode(),0);
        const auto receipt=json(handoff).value("receipt").toString();QVERIFY(!receipt.isEmpty());const auto dir=QFileInfo(receipt).absolutePath();
        QElapsedTimer ready;ready.start();while(!QFileInfo::exists(dir+"/fixture-live-ready")&&ready.elapsed()<10000)QTest::qWait(50);
        evidence("live-start",receipt);QVERIFY2(QFileInfo::exists(dir+"/fixture-live-ready"),qPrintable(QString::fromUtf8(read(receipt))));
        const auto pid=json(dir+"/fixture-new-launcher.json").value("pid").toString().toUInt();HANDLE launcher=OpenProcess(SYNCHRONIZE,FALSE,pid);QVERIFY(launcher);
        QTest::qWait(31500);const auto state=json(receipt);evidence("live-after-timeout",receipt);QCOMPARE(state.value("phase").toString(),QString("installed-awaiting-startup"));
        QCOMPARE(read(target),fresh);QCOMPARE(read(state.value("backup").toString()),old);QVERIFY(!QFileInfo::exists(dir+"/payload.exe"));
        const bool helperEnded=awaitOwnedProcess(json(handoff).value("helper").toObject(),"live-helper");
        SetEvent(stop);const DWORD launcherWait=WaitForSingleObject(launcher,10000);CloseHandle(launcher);CloseHandle(stop);
        const bool launcherEnded=awaitOwnedProcess(json(dir+"/fixture-new-launcher.json"),"live-new-launcher");
        QVERIFY(helperEnded);QCOMPARE(launcherWait,DWORD(WAIT_OBJECT_0));QVERIFY(launcherEnded);
    }
};
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()==3&&args[1]==HelperArgument)return runInstallHelper(args[2]);
    if(args.size()==3&&args[1]=="--fixture-process"){
        auto name=args[2].toStdWString();HANDLE event=OpenEventW(SYNCHRONIZE,FALSE,name.c_str());if(!event)return 80;
        WaitForSingleObject(event,15000);CloseHandle(event);return 0;
    }
    if(args.size()==3&&args[1]=="--fixture-main")return fixtureMain(args[2]);
    if(args.size()==3&&args[1]=="--fixture-parent")return fixtureParent(args[2]);
    if(args.size()==4&&args[1]=="--fixture-ack-main")return fixtureAckMain(args[2],args[3]);
    if(args.size()==5&&args[1]==ReceiptArgument&&args[3]==ReceiptTokenArgument)return fixtureInstalledLauncher(args[2],args[4]);
    SelfUpdateTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_selfupdate.moc"
