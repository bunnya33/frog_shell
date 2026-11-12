#include "../src/core/serverclock.h"
#include "../src/core/session.h"
#include "../src/core/operationgate.h"
#include "../src/ui/serverclock.h"
#include "../src/ui/theme.h"
#include <QtTest>
namespace rs {
// The production Session already grants this existing test friend access.
class FileTransferTests {
public:
    static QList<Job> queuedSets(Session &session){QMutexLocker lock(&session.mutex);QList<Job> result;for(const auto &job:session.jobs)if(job.type=="server-clock"&&job.path=="set")result.append(job);return result;}
};
class ServerClockTests:public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    static QVariantMap sample(qint64 epoch=1704067200,int offset=0,QString zone="UTC",QString ntp="no"){
        return {{"ok",true},{"epoch",epoch},{"offset",offset},{"zone",zone},{"ntp",ntp}};
    }
    static Profile profile(QString name="测试服务器"){Profile p;p.name=name;p.host="fixture.invalid";return p;}
    static services::Result reply(int code=0,QByteArray out={},QByteArray err={},bool unknown=false){return {out,err,code,unknown};}
private slots:
    void initTestCase(){QVERIFY(temporary.isValid());QCoreApplication::setOrganizationName("ClockFixture");QCoreApplication::setApplicationName("Isolated");QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temporary.path());QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,temporary.path());QApplication::setStyle("Fusion");if(QFileInfo::exists("C:/Windows/Fonts/msyh.ttc"))QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");QApplication::setFont(QFont("Microsoft YaHei UI",10));}
    void cleanup(){OperationGate::setLocked(false);}
    void parsingAndValidation(){
        serverclock::Sample s;QString e;
        QVERIFY2(serverclock::parseSample("CLOCK|1704067200|2024-01-01 00:00:00|+0000|UTC\nNTP|yes\n",&s,&e),qPrintable(e));QCOMPARE(s.epoch,qint64(1704067200));QCOMPARE(s.ntp,QString("yes"));
        QVERIFY(serverclock::parseSample("CLOCK|1704067200|2023-12-31 19:00:00|-0500|EST\nNTP|unknown\n",&s,&e));QCOMPARE(s.offsetSeconds,-18000);
        QVERIFY(serverclock::parseSample(QString("CLOCK|1704067200|2024-01-01 05:30:00|+0530|印度时间\nNTP|no\n").toUtf8(),&s,&e));
        QCOMPARE(serverclock::offsetText(s.offsetSeconds),QString("UTC+05:30"));
        for(const auto &bad:{QByteArray("CLOCK|1704067200|2024-01-01 00:00:01|+0000|UTC\nNTP|yes\n"),QByteArray("CLOCK|1704067200|2024-01-01 00:00:00|+1460|UTC\nNTP|yes\n"),QByteArray("bad")})QVERIFY(!serverclock::parseSample(bad,&s,&e));
        QVERIFY(serverclock::validLocalDateTime("2024-02-29 23:59:59"));
        for(const auto &bad:{"2023-02-29 00:00:00","2024-01-01 24:00:00","2024-01-01 00:00:00;id","2024-01-01 00:00:00' $(id)","1969-12-31 23:59:59"})QVERIFY(!serverclock::validLocalDateTime(bad));
        const auto cmd=serverclock::sampleCommand();QVERIFY(!cmd.mutation);QVERIFY(cmd.timeoutMs<=10000);QVERIFY(cmd.outputLimit<=8192);
        QVERIFY(cmd.text.indexOf("timedatectl show")<cmd.text.indexOf("v=$(date "));QVERIFY(cmd.text.indexOf("v=$(date ")<cmd.text.indexOf("printf 'CLOCK|"));
    }
    void writesRequireConsentAndAreNeverReplayed(){
        QList<services::Command> calls;auto run=[&](QByteArray ntp,services::Result write,bool allow=true,bool guard=true,bool timedatectl=true,bool active=false,bool date=true){calls.clear();return serverclock::run("set",{{"local","2024-05-12 15:04:05"},{"allowUnknown",allow},{"allowActiveNtp",active}},[&](const auto &c){calls<<c;return c.mutation?write:reply(0,QByteArray("TOOL|")+(timedatectl?"yes":"no")+"|"+(date?"yes":"no")+"\nNTP|"+ntp);},[guard]{return guard;});};
        auto r=run("yes\n",reply());QVERIFY(!r["ok"].toBool());QCOMPARE(calls.size(),1);
        r=run("yes\n",reply(),false,true,true,true);QVERIFY(r["ok"].toBool());QCOMPARE(calls.size(),2);QVERIFY(calls.last().text.contains("date -s '2024-05-12 15:04:05'"));QVERIFY(!calls.last().text.contains("set-ntp"));
        r=run("yes\n",reply(),false,true,true,true,false);QVERIFY(!r["ok"].toBool());QCOMPARE(calls.size(),1);
        r=run("yes\n",reply(-1,{}, {},true),false,true,true,true);QVERIFY(r["unknown"].toBool());QCOMPARE(calls.size(),2);
        r=run("yes\n",reply(1,{},"permission denied"),false,true,true,true);QVERIFY(!r["ok"].toBool());QCOMPARE(calls.size(),2);
        r=run("no\n",reply(1,{},"permission denied"));QVERIFY(!r["ok"].toBool());QVERIFY(r["error"].toString().contains("permission denied"));QCOMPARE(calls.size(),2);QVERIFY(calls.last().mutation);QVERIFY(calls.last().text.contains("sudo -n"));QVERIFY(!calls.last().text.contains("set-ntp"));
        r=run("no\n",reply(-1,{}, {},true));QVERIFY(r["unknown"].toBool());QCOMPARE(calls.size(),2);
        r=run("unknown\n",reply(),false);QVERIFY(!r["ok"].toBool());QCOMPARE(calls.size(),1);
        r=run("unknown\n",reply(),true);QVERIFY(r["ok"].toBool());QCOMPARE(calls.size(),2);
        r=run("unknown\n",reply(),true,true,false);QVERIFY(r["ok"].toBool());QCOMPARE(calls.size(),2);QVERIFY(calls.last().text.contains("date -s '2024-05-12 15:04:05'"));QVERIFY(calls.last().text.contains("sudo -n"));
        r=run("unknown\n",reply(),false,true,false);QVERIFY(!r["ok"].toBool());QCOMPARE(calls.size(),1);
        r=run("no\n",reply(),true,false);QVERIFY(!r["ok"].toBool());QCOMPARE(calls.size(),1);
        calls.clear();r=serverclock::run("set",{{"local","2024-01-01 00:00:00;touch /tmp/x"}},[&](const auto &c){calls<<c;return reply();},[]{return true;});QVERIFY(!r["ok"].toBool());QVERIFY(calls.isEmpty());
        r=serverclock::run("sample",{},[](const auto &){return reply(-1,{}, {},true);});QVERIFY(r["unknown"].toBool());
    }
    void lifecycleAndStaleCallbacks(){
        Session first(profile("第一台"),{},nullptr,Session::Mode::Commands),second(profile("第二台"),{},nullptr,Session::Mode::Commands);
        ServerClock w;w.setSession(&first);w.setConnected(true);QVERIFY(w.busy);const int firstId=w.pendingId;
        first.completed("server-clock",firstId,sample());QVERIFY(w.valid);QCOMPARE(w.text(),QString("2024-01-01 00:00:00"));
        w.requestAge.invalidate();w.tick();QVERIFY(w.busy);const int oldId=w.pendingId;
        w.setSession(&second);w.setConnected(true);const int newId=w.pendingId;first.completed("server-clock",oldId,sample(1704067200,0,"OLD"));QVERIFY(!w.valid);
        second.completed("server-clock",newId,sample(1704067200,19800,"IST"));QVERIFY(w.valid);QCOMPARE(w.text(),QString("2024-01-01 05:30:00"));
        w.wallAtSample-=15000;w.tick();QVERIFY(!w.valid);QVERIFY(w.busy);QVERIFY(w.text().contains("服务器时间"));
        w.setConnected(false);QVERIFY(!w.busy);QVERIFY(!w.valid);second.completed("server-clock",w.pendingId,sample());QVERIFY(!w.valid);
    }
    void lockCancelsOpenConfirmation(){
        Session worker(profile(),{},nullptr,Session::Mode::Commands);
        ServerClock w;w.setSession(&worker);w.setConnected(true);
        worker.completed("server-clock",w.pendingId,sample());w.showPanel();QVERIFY(w.dialog);
        QTimer::singleShot(0,&w,[&]{OperationGate::setLocked(true);w.tick();});
        w.apply();
        QVERIFY(!w.dialog);QVERIFY(!w.busy);QVERIFY(!w.writePermit||!w.writePermit->load());
    }
    void confirmationSequence_data(){
        QTest::addColumn<QString>("environment");QTest::addColumn<int>("count");
        QTest::newRow("test")<<QString("测试")<<1;QTest::newRow("production")<<QString("生产")<<2;
        QTest::newRow("personal")<<QString("个人")<<2;QTest::newRow("alias")<<QString("正式环境")<<2;
    }
    void confirmationSequence(){
        QFETCH(QString,environment);QFETCH(int,count);auto p=profile("<b>fixture</b>");p.environment=environment;p.host="<fixture>";
        Session worker(p,{},nullptr,Session::Mode::Commands);ServerClock w;w.setSession(&worker);w.setConnected(true);worker.completed("server-clock",w.pendingId,sample(1704067200,0,"<UTC>","no"));w.showPanel();
        int seen=0;QString firstText,secondText;bool defaults=true,icons=true;
        std::function<void()> answer;answer=[&]{auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!box)return;seen++;defaults &= box->defaultButton()==box->button(QMessageBox::Cancel)&&box->escapeButton()==box->button(QMessageBox::Cancel)&&!box->checkBox();
            if(seen==1){icons &= box->icon()==QMessageBox::Warning;firstText=box->text();if(count==2)QTimer::singleShot(0,&w,answer);}
            else {icons &= box->icon()==QMessageBox::Critical;secondText=box->text();}
            box->button(QMessageBox::Yes)->click();};
        QTimer::singleShot(0,&w,answer);w.apply();QCOMPARE(seen,count);QVERIFY(defaults);QVERIFY(icons);QVERIFY(firstText.contains("<b>fixture</b>"));
        if(count==2){QVERIFY(secondText.contains("&lt;b&gt;fixture&lt;/b&gt;"));QVERIFY(secondText.contains("&lt;fixture&gt;"));QVERIFY(secondText.contains("&lt;UTC&gt;"));QVERIFY(secondText.contains("color:#e53935"));QVERIFY(secondText.contains("日志时间顺序"));}
        QVERIFY(w.busy);QCOMPARE(w.pendingAction,QString("set"));QVERIFY(w.writePermit&&w.writePermit->load());QCOMPARE(FileTransferTests::queuedSets(worker).size(),1);
    }
    void cancellationAndRevocation_data(){
        QTest::addColumn<int>("step");QTest::addColumn<QString>("action");
        for(int step:{1,2})for(const auto &action:{"cancel","escape","close","lock","disconnect","inactive","session","form"})QTest::newRow(qPrintable(QString("%1-%2").arg(step).arg(action)))<<step<<QString(action);
    }
    void cancellationAndRevocation(){
        QFETCH(int,step);QFETCH(QString,action);auto p=profile();p.environment="生产";
        Session worker(p,{},nullptr,Session::Mode::Commands),other(profile("other"),{},nullptr,Session::Mode::Commands);ServerClock w;w.setSession(&worker);w.setConnected(true);worker.completed("server-clock",w.pendingId,sample());w.showPanel();int seen=0;
        std::function<void()> respond;respond=[&]{auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!box)return;++seen;if(seen<step){QTimer::singleShot(0,&w,respond);box->button(QMessageBox::Yes)->click();return;}
            if(action=="cancel")box->button(QMessageBox::Cancel)->click();else if(action=="escape")QTest::keyClick(box,Qt::Key_Escape);else if(action=="close")box->close();
            else if(action=="lock"){OperationGate::setLocked(true);w.tick();}else if(action=="disconnect")w.setConnected(false);else if(action=="inactive")w.setActive(false);else if(action=="session")w.setSession(&other);else w.dialog->reject();};
        QTimer::singleShot(0,&w,respond);w.apply();QCOMPARE(seen,step);QVERIFY(!w.writePermit);QVERIFY(!w.busy||w.pendingAction!="set");QVERIFY(FileTransferTests::queuedSets(worker).isEmpty());QVERIFY(FileTransferTests::queuedSets(other).isEmpty());
    }
    void consentUsesOriginalNtpSnapshot_data(){
        QTest::addColumn<QString>("initial");QTest::addColumn<QString>("later");QTest::addColumn<bool>("activePermit");QTest::addColumn<bool>("unknownPermit");
        QTest::newRow("no-to-yes")<<QString("no")<<QString("yes")<<false<<false;
        QTest::newRow("no-to-unknown")<<QString("no")<<QString("unknown")<<false<<false;
        QTest::newRow("yes-to-no")<<QString("yes")<<QString("no")<<true<<false;
        QTest::newRow("unknown-to-yes")<<QString("unknown")<<QString("yes")<<false<<true;
    }
    void consentUsesOriginalNtpSnapshot(){
        QFETCH(QString,initial);QFETCH(QString,later);QFETCH(bool,activePermit);QFETCH(bool,unknownPermit);
        auto p=profile();p.environment="生产";Session worker(p,{},nullptr,Session::Mode::Commands);ServerClock w;w.setSession(&worker);w.setConnected(true);worker.completed("server-clock",w.pendingId,sample(1704067200,0,"UTC",initial));w.showPanel();
        QString shown;QTimer::singleShot(0,&w,[&]{auto first=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!first)return;shown=first->text();w.sample.ntp=later;QTimer::singleShot(0,&w,[&]{auto second=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(second)second->button(QMessageBox::Yes)->click();});first->button(QMessageBox::Yes)->click();});w.apply();
        const auto jobs=FileTransferTests::queuedSets(worker);QCOMPARE(jobs.size(),1);QCOMPARE(jobs[0].options.value("allowActiveNtp").toBool(),activePermit);QCOMPARE(jobs[0].options.value("allowUnknown").toBool(),unknownPermit);
        if(initial=="yes")QVERIFY(shown.contains("自动校时已开启"));if(initial=="unknown")QVERIFY(shown.contains("状态不确定"));
    }
    void staleLockTokenCannotCreateWrite(){
        Session worker(profile(),{},nullptr,Session::Mode::Commands);ServerClock w;w.setSession(&worker);w.setConnected(true);worker.completed("server-clock",w.pendingId,sample());
        const auto token=OperationGate::token();OperationGate::setLocked(true);OperationGate::setLocked(false);
        w.submit("set",{{"local","2024-05-12 15:04:05"},{"lockEpoch",QVariant::fromValue(token)}});
        QVERIFY(!w.busy);QVERIFY(!w.writePermit);QVERIFY(FileTransferTests::queuedSets(worker).isEmpty());
    }
    void criticalScreenshots(){
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temporary.path());QVERIFY(QDir().mkpath(output));
        for(bool light:{false,true}){applyTheme(light);auto p=profile("线上数据库");p.environment="生产";Session worker(p,{},nullptr,Session::Mode::Commands);ServerClock w;w.show();w.setSession(&worker);w.setConnected(true);worker.completed("server-clock",w.pendingId,sample(1704067200,28800,"CST","yes"));w.showPanel();QVERIFY(w.applyButton->isEnabled());bool saved=false;
            QTimer::singleShot(0,&w,[&]{auto first=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!first)return;QTimer::singleShot(0,&w,[&]{auto second=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(!second)return;QTest::qWait(50);saved=second->grab().save(output+(light?"/worker-clock-critical-light.png":"/worker-clock-critical-dark.png"));second->button(QMessageBox::Cancel)->click();});first->button(QMessageBox::Yes)->click();});w.apply();QVERIFY(saved);QVERIFY(!w.busy);}
    }
    void formLockAndScreenshots(){
        const auto output=qEnvironmentVariable("RABBITSHELL_TEST_REPORT_DIR",temporary.path());QVERIFY(QDir().mkpath(output));
        for(bool light:{false,true}){
            applyTheme(light);QWidget host;auto row=new QHBoxLayout(&host);row->addStretch();Session worker(profile(),{},nullptr,Session::Mode::Commands);auto w=new ServerClock(&host);row->addWidget(w);host.resize(780,50);host.show();w->setSession(&worker);w->setConnected(true);worker.completed("server-clock",w->pendingId,sample(1704067200,28800,"CST","unknown"));QTest::qWait(20);
            QVERIFY(host.grab().save(output+(light?"/worker-clock-footer-light.png":"/worker-clock-footer-dark.png")));
            w->click();QTRY_VERIFY(w->dialog);QTest::qWait(30);QVERIFY(w->dialog->grab().save(output+(light?"/worker-clock-dialog-light.png":"/worker-clock-dialog-dark.png")));
            auto chosen=QDate(2030,3,5);w->dateEdit->setDate(chosen);w->refresh(true);worker.completed("server-clock",w->pendingId,sample(1704067201,28800,"CST","unknown"));QCOMPARE(w->dateEdit->date(),chosen);
            QTimer::singleShot(0,[&]{auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());QVERIFY(box);QCOMPARE(box->defaultButton(),box->button(QMessageBox::Cancel));box->button(QMessageBox::Cancel)->click();});
            w->apply();QVERIFY(!w->busy);
            w->submit("set",{{"local","2030-03-05 00:00:00"},{"allowUnknown",true}});QVERIFY(w->busy);
            worker.completed("server-clock",w->pendingId,{{"ok",false},{"unknown",true},{"error","修改结果未知，不会自动重试。"}});
            QVERIFY(w->busy);QVERIFY(w->statusLabel->text().contains("结果未知"));worker.completed("server-clock",w->pendingId,sample(1704067202,28800,"CST","unknown"));
            QCOMPARE(w->dateEdit->date(),chosen);QVERIFY(w->statusLabel->text().contains("结果未知"));QVERIFY(w->statusLabel->text().contains("只读"));
            w->submit("set",{{"local","2030-03-05 00:00:00"},{"allowUnknown",true}});worker.completed("server-clock",w->pendingId,{{"ok",false},{"error","权限不足"}});
            worker.completed("server-clock",w->pendingId,sample(1704067203,28800,"CST","unknown"));QVERIFY(w->statusLabel->text().contains("权限不足"));
            w->submit("set",{{"local","2030-03-05 00:00:00"},{"allowUnknown",true}});auto permit=w->writePermit;QVERIFY(permit&&permit->load());
            OperationGate::setLocked(true);w->tick();QVERIFY(!w->dialog);QVERIFY(!w->valid);OperationGate::setLocked(false);w->setConnected(false);
            QVERIFY(!permit->load());
        }
    }
};
}
QTEST_MAIN(rs::ServerClockTests)
#include "test_serverclock.moc"
